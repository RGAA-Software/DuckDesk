use nvml_wrapper::Nvml;
use px_node_protocol::NodeGpuTelemetry;
use sha2::{Digest, Sha256};
use std::ffi::c_void;
use windows::Win32::Foundation::LUID;

// NVML metrics enrich the WMI inventory only through an unambiguous PCI identity.
// Names are display data and must never select a physical adapter.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct PciIdentity {
    vendor_id: u16,
    device_id: u16,
    subsystem_id: Option<u32>,
}

#[derive(Debug)]
struct NvidiaMetrics {
    pci: PciIdentity,
    dedicated_memory_bytes: Option<u64>,
    used_memory_bytes: Option<u64>,
    utilization_per_mille: Option<u16>,
    encoder_utilization_per_mille: Option<u16>,
}

#[derive(Debug)]
struct RuntimeAdapterIdentity {
    stable_keys: Vec<String>,
}

type D3dkmtHandle = u32;

#[repr(C)]
struct D3dkmtOpenAdapterFromLuid {
    adapter_luid: LUID,
    adapter_handle: D3dkmtHandle,
}

#[repr(C)]
struct D3dkmtCloseAdapter {
    adapter_handle: D3dkmtHandle,
}

#[repr(C)]
struct D3dkmtQueryAdapterInfo {
    adapter_handle: D3dkmtHandle,
    query_type: i32,
    private_driver_data: *mut c_void,
    private_driver_data_size: u32,
}

#[repr(C)]
struct D3dkmtQueryPhysicalAdapterPnpKey {
    physical_adapter_index: u32,
    pnp_key_type: i32,
    destination: *mut u16,
    destination_character_count: *mut u32,
}

#[link(name = "gdi32")]
unsafe extern "system" {
    fn D3DKMTOpenAdapterFromLuid(open_adapter: *mut D3dkmtOpenAdapterFromLuid) -> i32;
    fn D3DKMTQueryAdapterInfo(query_adapter_info: *const D3dkmtQueryAdapterInfo) -> i32;
    fn D3DKMTCloseAdapter(close_adapter: *const D3dkmtCloseAdapter) -> i32;
}

const KMTQAITYPE_PHYSICALADAPTERCOUNT: i32 = 30;
const KMTQAITYPE_PHYSICALADAPTERPNPKEY: i32 = 41;
const D3DKMT_PNP_KEY_HARDWARE: i32 = 1;

pub(crate) struct EnumeratedGpu {
    pub(crate) pnp_identity: String,
    pub(crate) telemetry: NodeGpuTelemetry,
}

pub(crate) fn enrich_nvidia_metrics(gpus: &mut [EnumeratedGpu]) {
    let Ok(nvml) = Nvml::init() else {
        return;
    };
    let Ok(device_count) = nvml.device_count() else {
        return;
    };
    let metrics = (0..device_count)
        .filter_map(|device_index| {
            let device = nvml.device_by_index(device_index).ok()?;
            let pci = device.pci_info().ok()?;
            let memory = device.memory_info().ok();
            Some(NvidiaMetrics {
                pci: PciIdentity {
                    vendor_id: u16::try_from(pci.pci_device_id & 0xffff).ok()?,
                    device_id: u16::try_from(pci.pci_device_id >> 16).ok()?,
                    subsystem_id: pci.pci_sub_system_id,
                },
                dedicated_memory_bytes: memory.as_ref().map(|value| value.total),
                used_memory_bytes: memory.as_ref().map(|value| value.used),
                utilization_per_mille: device
                    .utilization_rates()
                    .ok()
                    .and_then(|value| percentage_to_per_mille(value.gpu)),
                encoder_utilization_per_mille: device
                    .encoder_utilization()
                    .ok()
                    .and_then(|value| percentage_to_per_mille(value.utilization)),
            })
        })
        .collect::<Vec<_>>();
    apply_unique_matches(gpus, &metrics);
}

pub(crate) fn enrich_runtime_adapter_bindings(gpus: &mut [EnumeratedGpu]) {
    use windows::Win32::Graphics::Dxgi::{CreateDXGIFactory1, IDXGIFactory1};

    let Ok(factory) = (unsafe { CreateDXGIFactory1::<IDXGIFactory1>() }) else {
        return;
    };
    let mut adapters = Vec::new();
    for adapter_index in 0..64 {
        let Ok(adapter) = (unsafe { factory.EnumAdapters1(adapter_index) }) else {
            break;
        };
        let Ok(description) = (unsafe { adapter.GetDesc1() }) else {
            continue;
        };
        adapters.push(RuntimeAdapterIdentity {
            stable_keys: physical_adapter_stable_keys(description.AdapterLuid),
        });
    }
    apply_runtime_adapter_bindings(gpus, &adapters);
}

fn physical_adapter_stable_keys(adapter_luid: LUID) -> Vec<String> {
    let mut open_adapter = D3dkmtOpenAdapterFromLuid {
        adapter_luid,
        adapter_handle: 0,
    };
    let open_status = unsafe { D3DKMTOpenAdapterFromLuid(&mut open_adapter) };
    if open_status < 0 || open_adapter.adapter_handle == 0 {
        return Vec::new();
    }
    let adapter_handle = open_adapter.adapter_handle;
    let stable_keys = query_physical_adapter_count(adapter_handle)
        .map(|physical_adapter_count| {
            (0..physical_adapter_count)
                .filter_map(|physical_adapter_index| {
                    query_physical_adapter_pnp_key(adapter_handle, physical_adapter_index)
                })
                .map(|pnp_identity| stable_gpu_key(&pnp_identity))
                .collect::<Vec<_>>()
        })
        .unwrap_or_default();
    let close_adapter = D3dkmtCloseAdapter { adapter_handle };
    let _ = unsafe { D3DKMTCloseAdapter(&close_adapter) };
    stable_keys
}

fn query_physical_adapter_count(adapter_handle: D3dkmtHandle) -> Option<u32> {
    let mut physical_adapter_count = 0_u32;
    let query = D3dkmtQueryAdapterInfo {
        adapter_handle,
        query_type: KMTQAITYPE_PHYSICALADAPTERCOUNT,
        private_driver_data: std::ptr::from_mut(&mut physical_adapter_count).cast(),
        private_driver_data_size: u32::try_from(std::mem::size_of_val(&physical_adapter_count))
            .ok()?,
    };
    let status = unsafe { D3DKMTQueryAdapterInfo(&query) };
    (status >= 0 && physical_adapter_count > 0).then_some(physical_adapter_count)
}

fn query_physical_adapter_pnp_key(
    adapter_handle: D3dkmtHandle,
    physical_adapter_index: u32,
) -> Option<String> {
    const MAX_PNP_KEY_CHARACTERS: usize = 1024;
    let mut destination = [0_u16; MAX_PNP_KEY_CHARACTERS];
    let mut destination_character_count = u32::try_from(destination.len()).ok()?;
    let mut pnp_key = D3dkmtQueryPhysicalAdapterPnpKey {
        physical_adapter_index,
        pnp_key_type: D3DKMT_PNP_KEY_HARDWARE,
        destination: destination.as_mut_ptr(),
        destination_character_count: &mut destination_character_count,
    };
    let query = D3dkmtQueryAdapterInfo {
        adapter_handle,
        query_type: KMTQAITYPE_PHYSICALADAPTERPNPKEY,
        private_driver_data: std::ptr::from_mut(&mut pnp_key).cast(),
        private_driver_data_size: u32::try_from(std::mem::size_of_val(&pnp_key)).ok()?,
    };
    let status = unsafe { D3DKMTQueryAdapterInfo(&query) };
    if status < 0 {
        return None;
    }
    let length = destination
        .iter()
        .position(|character| *character == 0)
        .unwrap_or(destination.len());
    let identity = canonicalize_pnp_identity(&String::from_utf16(&destination[..length]).ok()?)?;
    (!identity.is_empty()).then_some(identity)
}

pub(crate) fn stable_gpu_key(identity: &str) -> String {
    let canonical_identity =
        canonicalize_pnp_identity(identity).unwrap_or_else(|| identity.trim().to_uppercase());
    let digest = Sha256::digest(canonical_identity.as_bytes());
    format!("pnp-sha256:{}", lowercase_hex(&digest))
}

fn canonicalize_pnp_identity(identity: &str) -> Option<String> {
    const ENUM_MARKER: &str = "\\ENUM\\";
    const DEVICE_PARAMETERS_SUFFIX: &str = "\\DEVICE PARAMETERS";
    let mut canonical = identity.trim().replace('/', "\\").to_uppercase();
    if let Some(marker_offset) = canonical.find(ENUM_MARKER) {
        canonical = canonical[(marker_offset + ENUM_MARKER.len())..].to_owned();
    }
    if let Some(without_suffix) = canonical.strip_suffix(DEVICE_PARAMETERS_SUFFIX) {
        canonical = without_suffix.to_owned();
    }
    (!canonical.is_empty()).then_some(canonical)
}

fn lowercase_hex(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let mut encoded = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        encoded.push(char::from(DIGITS[usize::from(byte >> 4)]));
        encoded.push(char::from(DIGITS[usize::from(byte & 0x0f)]));
    }
    encoded
}

fn apply_runtime_adapter_bindings(gpus: &mut [EnumeratedGpu], adapters: &[RuntimeAdapterIdentity]) {
    for gpu in gpus {
        gpu.telemetry.runtime_binding_ready = adapters
            .iter()
            .any(|adapter| adapter.stable_keys.contains(&gpu.telemetry.stable_key));
    }
}

fn apply_unique_matches(gpus: &mut [EnumeratedGpu], metrics: &[NvidiaMetrics]) {
    let identities = gpus
        .iter()
        .map(|gpu| parse_pnp_identity(&gpu.pnp_identity))
        .collect::<Vec<_>>();
    for (gpu_index, identity) in identities.iter().enumerate() {
        let Some(identity) = identity else {
            continue;
        };
        let candidates = metrics
            .iter()
            .filter(|sample| pci_matches(*identity, sample.pci))
            .collect::<Vec<_>>();
        if candidates.len() != 1 {
            continue;
        }
        let sample = candidates[0];
        let reverse_matches = identities
            .iter()
            .filter(|candidate| {
                candidate.is_some_and(|candidate| pci_matches(candidate, sample.pci))
            })
            .count();
        if reverse_matches != 1 {
            continue;
        }
        let telemetry = &mut gpus[gpu_index].telemetry;
        telemetry.dedicated_memory_bytes = sample.dedicated_memory_bytes;
        telemetry.used_memory_bytes = sample.used_memory_bytes;
        telemetry.utilization_per_mille = sample.utilization_per_mille;
        telemetry.encoder_utilization_per_mille = sample.encoder_utilization_per_mille;
    }
}

fn pci_matches(windows: PciIdentity, nvidia: PciIdentity) -> bool {
    windows.vendor_id == nvidia.vendor_id
        && windows.device_id == nvidia.device_id
        && match (windows.subsystem_id, nvidia.subsystem_id) {
            (Some(windows_subsystem), Some(nvidia_subsystem)) => {
                windows_subsystem == nvidia_subsystem
            }
            _ => true,
        }
}

fn parse_pnp_identity(identity: &str) -> Option<PciIdentity> {
    let vendor_id = parse_component(identity, "VEN_", 4)?;
    let device_id = parse_component(identity, "DEV_", 4)?;
    let subsystem_id = parse_component(identity, "SUBSYS_", 8);
    Some(PciIdentity {
        vendor_id: u16::try_from(vendor_id).ok()?,
        device_id: u16::try_from(device_id).ok()?,
        subsystem_id,
    })
}

fn parse_component(identity: &str, prefix: &str, digits: usize) -> Option<u32> {
    let start = identity.find(prefix)?.checked_add(prefix.len())?;
    let end = start.checked_add(digits)?;
    let value = identity.get(start..end)?;
    value
        .bytes()
        .all(|byte| byte.is_ascii_hexdigit())
        .then(|| u32::from_str_radix(value, 16).ok())
        .flatten()
}

fn percentage_to_per_mille(percentage: u32) -> Option<u16> {
    (percentage <= 100)
        .then(|| percentage.checked_mul(10))
        .flatten()
        .and_then(|value| u16::try_from(value).ok())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_unique_pci_identity_receives_metrics() {
        let mut gpus = vec![gpu("PCI\\VEN_10DE&DEV_2684&SUBSYS_14573842&REV_A1")];
        apply_unique_matches(&mut gpus, &[metrics()]);
        assert_eq!(gpus[0].telemetry.dedicated_memory_bytes, Some(24));
        assert_eq!(gpus[0].telemetry.used_memory_bytes, Some(8));
        assert_eq!(gpus[0].telemetry.utilization_per_mille, Some(250));
        assert_eq!(gpus[0].telemetry.encoder_utilization_per_mille, Some(120));
    }

    #[test]
    fn ambiguous_identical_adapters_remain_unknown() {
        let identity = "PCI\\VEN_10DE&DEV_2684&SUBSYS_14573842&REV_A1";
        let mut gpus = vec![gpu(identity), gpu(identity)];
        apply_unique_matches(&mut gpus, &[metrics()]);
        assert!(gpus
            .iter()
            .all(|gpu| gpu.telemetry.utilization_per_mille.is_none()));
    }

    #[test]
    fn runtime_adapter_binding_accepts_equivalent_luids_for_one_physical_gpu() {
        let first_identity = "PCI\\VEN_10DE&DEV_2684&SUBSYS_14573842&REV_A1";
        let second_identity = "PCI\\VEN_1002&DEV_73BF&SUBSYS_0E3A1002&REV_C1";
        let mut gpus = vec![gpu(first_identity), gpu(second_identity)];
        let adapters = vec![
            RuntimeAdapterIdentity {
                stable_keys: vec![stable_gpu_key(first_identity)],
            },
            RuntimeAdapterIdentity {
                stable_keys: vec![stable_gpu_key(second_identity)],
            },
        ];

        apply_runtime_adapter_bindings(&mut gpus, &adapters);

        assert!(gpus[0].telemetry.runtime_binding_ready);
        assert!(gpus[1].telemetry.runtime_binding_ready);
        let mut equivalent_luid_gpus = vec![gpu(first_identity), gpu(second_identity)];
        let mut equivalent_luid_adapters = adapters;
        equivalent_luid_adapters.push(RuntimeAdapterIdentity {
            stable_keys: vec![stable_gpu_key(first_identity)],
        });
        apply_runtime_adapter_bindings(&mut equivalent_luid_gpus, &equivalent_luid_adapters);
        assert!(equivalent_luid_gpus[0].telemetry.runtime_binding_ready);
        assert!(equivalent_luid_gpus[1].telemetry.runtime_binding_ready);
    }

    #[test]
    fn registry_and_wmi_pnp_forms_produce_the_same_stable_key() {
        let wmi = "PCI\\VEN_10DE&DEV_2504&SUBSYS_250410DE&REV_A1\\4&2283F625&0&0019";
        let d3dkmt = "\\Registry\\Machine\\System\\ControlSet001\\Enum\\PCI\\VEN_10DE&DEV_2504&SUBSYS_250410DE&REV_A1\\4&2283F625&0&0019\\Device Parameters";
        assert_eq!(stable_gpu_key(wmi), stable_gpu_key(d3dkmt));
    }

    #[test]
    fn malformed_or_different_pci_identities_never_receive_metrics() {
        assert_eq!(parse_pnp_identity("not-pci"), None);
        assert_eq!(percentage_to_per_mille(101), None);
        let mut gpus = vec![gpu("PCI\\VEN_1002&DEV_73BF&SUBSYS_0E3A1002")];
        apply_unique_matches(&mut gpus, &[metrics()]);
        assert!(gpus[0].telemetry.dedicated_memory_bytes.is_none());
    }

    fn metrics() -> NvidiaMetrics {
        NvidiaMetrics {
            pci: PciIdentity {
                vendor_id: 0x10de,
                device_id: 0x2684,
                subsystem_id: Some(0x14573842),
            },
            dedicated_memory_bytes: Some(24),
            used_memory_bytes: Some(8),
            utilization_per_mille: Some(250),
            encoder_utilization_per_mille: Some(120),
        }
    }

    fn gpu(identity: &str) -> EnumeratedGpu {
        EnumeratedGpu {
            pnp_identity: identity.to_owned(),
            telemetry: NodeGpuTelemetry {
                stable_key: stable_gpu_key(identity),
                name: "Synthetic GPU".into(),
                runtime_binding_ready: false,
                dedicated_memory_bytes: None,
                used_memory_bytes: None,
                utilization_per_mille: None,
                encoder_utilization_per_mille: None,
            },
        }
    }
}
