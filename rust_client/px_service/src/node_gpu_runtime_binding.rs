//! Exact NVML PCI location to Render's existing PNP stable-key binding. No WMI.

use sha2::{Digest, Sha256};
use std::ffi::c_void;
use windows::Win32::{
    Foundation::LUID,
    Graphics::Dxgi::{CreateDXGIFactory1, IDXGIFactory1},
};

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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct PciLocation {
    pub domain: u32,
    pub bus: u32,
    pub device: u32,
    pub function: u32,
}

pub(crate) struct RuntimeAdapterBinding {
    location: PciLocation,
    stable_keys: Vec<String>,
}

#[repr(C)]
#[derive(Default)]
struct AdapterAddress {
    bus_number: u32,
    device_number: u32,
    function_number: u32,
}

struct AdapterHandle(D3dkmtHandle);
impl Drop for AdapterHandle {
    fn drop(&mut self) {
        let close_adapter = D3dkmtCloseAdapter {
            adapter_handle: self.0,
        };
        let _ = unsafe { D3DKMTCloseAdapter(&close_adapter) };
    }
}

pub(crate) fn enumerate() -> Vec<RuntimeAdapterBinding> {
    let Ok(factory) = (unsafe { CreateDXGIFactory1::<IDXGIFactory1>() }) else {
        return Vec::new();
    };
    let mut bindings = Vec::new();
    for adapter_index in 0..64 {
        let Ok(adapter) = (unsafe { factory.EnumAdapters1(adapter_index) }) else {
            break;
        };
        let Ok(description) = (unsafe { adapter.GetDesc1() }) else {
            continue;
        };
        if description.VendorId != 0x10de {
            continue;
        }
        let mut open_adapter = D3dkmtOpenAdapterFromLuid {
            adapter_luid: description.AdapterLuid,
            adapter_handle: 0,
        };
        if unsafe { D3DKMTOpenAdapterFromLuid(&mut open_adapter) } < 0
            || open_adapter.adapter_handle == 0
        {
            continue;
        }
        let handle = AdapterHandle(open_adapter.adapter_handle);
        let mut address = AdapterAddress::default();
        let query = D3dkmtQueryAdapterInfo {
            adapter_handle: handle.0,
            query_type: 6, // KMTQAITYPE_ADAPTERADDRESS in the Windows SDK.
            private_driver_data: std::ptr::from_mut(&mut address).cast(),
            private_driver_data_size: std::mem::size_of::<AdapterAddress>() as u32,
        };
        if unsafe { D3DKMTQueryAdapterInfo(&query) } < 0 {
            continue;
        }
        let stable_keys = query_physical_adapter_count(handle.0)
            .map(|count| {
                (0..count.min(16))
                    .filter_map(|index| query_physical_adapter_pnp_key(handle.0, index))
                    .map(|identity| stable_gpu_key(&identity))
                    .collect()
            })
            .unwrap_or_default();
        bindings.push(RuntimeAdapterBinding {
            location: PciLocation {
                domain: 0,
                bus: address.bus_number,
                device: address.device_number,
                function: address.function_number,
            },
            stable_keys,
        });
    }
    bindings
}

pub(crate) fn resolve_stable_key(
    bindings: &[RuntimeAdapterBinding],
    location: PciLocation,
) -> Option<String> {
    let mut stable_keys = bindings
        .iter()
        .filter(|binding| binding.location == location)
        .flat_map(|binding| binding.stable_keys.iter().cloned())
        .collect::<Vec<_>>();
    stable_keys.sort();
    stable_keys.dedup();
    let [stable_key] = stable_keys.as_slice() else {
        return None;
    };
    Some(stable_key.clone())
}

pub(crate) fn parse_pci_location(bus_id: &str) -> Option<PciLocation> {
    let mut components = bus_id.split(':');
    let domain = u32::from_str_radix(components.next()?, 16).ok()?;
    let bus = u32::from_str_radix(components.next()?, 16).ok()?;
    let (device, function) = components.next()?.split_once('.')?;
    if components.next().is_some() {
        return None;
    }
    let device = u32::from_str_radix(device, 16).ok()?;
    let function = u32::from_str_radix(function, 16).ok()?;
    (bus <= 255 && device <= 31 && function <= 7).then_some(PciLocation {
        domain,
        bus,
        device,
        function,
    })
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn identical_models_on_distinct_pci_locations_never_share_a_binding() {
        let first = parse_pci_location("00000000:01:00.0").unwrap();
        let second = parse_pci_location("00000000:02:00.0").unwrap();
        let bindings = vec![
            RuntimeAdapterBinding {
                location: first,
                stable_keys: vec!["first-physical-gpu".into()],
            },
            RuntimeAdapterBinding {
                location: second,
                stable_keys: vec!["second-physical-gpu".into()],
            },
        ];
        assert_eq!(
            resolve_stable_key(&bindings, first).as_deref(),
            Some("first-physical-gpu")
        );
        assert_eq!(
            resolve_stable_key(&bindings, second).as_deref(),
            Some("second-physical-gpu")
        );
        assert!(resolve_stable_key(&bindings, PciLocation { domain: 1, ..first }).is_none());
    }

    #[test]
    fn duplicate_luids_for_one_physical_gpu_are_allowed_but_ambiguous_identity_is_not() {
        let location = parse_pci_location("0000:01:00.0").unwrap();
        let mut bindings = vec![
            RuntimeAdapterBinding {
                location,
                stable_keys: vec!["physical-gpu".into()],
            },
            RuntimeAdapterBinding {
                location,
                stable_keys: vec!["physical-gpu".into()],
            },
        ];
        assert_eq!(
            resolve_stable_key(&bindings, location).as_deref(),
            Some("physical-gpu")
        );
        bindings[1].stable_keys = vec!["different-physical-gpu".into()];
        assert!(resolve_stable_key(&bindings, location).is_none());
    }

    #[test]
    fn invalid_pci_location_is_never_guessed_from_a_device_name_or_index() {
        for invalid in [
            "",
            "NVIDIA GPU",
            "0000:100:00.0",
            "0000:01:20.0",
            "0000:01:00.8",
            "0000:01:00.0:extra",
        ] {
            assert!(parse_pci_location(invalid).is_none());
        }
        assert_eq!(
            stable_gpu_key("PCI\\VEN_10DE&DEV_TEST"),
            stable_gpu_key("pci/ven_10de&dev_test")
        );
    }
}
