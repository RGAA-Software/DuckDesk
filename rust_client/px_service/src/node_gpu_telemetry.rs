use nvml_wrapper::Nvml;
use px_node_protocol::NodeGpuTelemetry;

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
                stable_key: identity.to_owned(),
                name: "Synthetic GPU".into(),
                dedicated_memory_bytes: None,
                used_memory_bytes: None,
                utilization_per_mille: None,
                encoder_utilization_per_mille: None,
            },
        }
    }
}
