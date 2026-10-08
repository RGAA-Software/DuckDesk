//! NVIDIA-first GPU collection. Hardware identity and metrics do not depend on WMI.

use crate::{
    hardware_probe::GpuSnapshot,
    node_gpu_runtime_binding::{self, PciLocation},
};
use chrono::Utc;
use nvml_wrapper::Nvml;
use px_node_protocol::NodeGpuTelemetry;
use sha2::{Digest, Sha256};

pub(crate) fn sample() -> Result<GpuSnapshot, String> {
    let nvml = Nvml::init().map_err(|error| format!("nvml_init: {error}"))?;
    let device_count = nvml
        .device_count()
        .map_err(|error| format!("nvml_device_count: {error}"))?;
    if device_count > 16 {
        return Err("NVIDIA GPU count exceeds node protocol limit".into());
    }
    let mut discovered = Vec::new();
    for device_index in 0..device_count {
        let device = match nvml.device_by_index(device_index) {
            Ok(device) => device,
            Err(error) => {
                eprintln!("nvml_device_by_index {device_index}: {error}");
                continue;
            }
        };
        let uuid = match device.uuid() {
            Ok(uuid) if !uuid.trim().is_empty() => uuid,
            result => {
                eprintln!("nvml_device_uuid {device_index}: {result:?}");
                continue;
            }
        };
        let name = match device.name() {
            Ok(name) if !name.trim().is_empty() && name.len() <= 256 => name,
            result => {
                eprintln!("nvml_device_name {uuid}: {result:?}");
                continue;
            }
        };
        let location = match device.pci_info() {
            Ok(pci) => {
                node_gpu_runtime_binding::parse_pci_location(&pci.bus_id).filter(|location| {
                    *location
                        == PciLocation {
                            domain: pci.domain,
                            bus: pci.bus,
                            device: pci.device,
                            function: location.function,
                        }
                })
            }
            Err(error) => {
                eprintln!("nvml_device_pci_info {uuid}: {error}");
                None
            }
        };
        let memory = match device.memory_info() {
            Ok(memory) if memory.total > 0 && memory.used <= memory.total => Some(memory),
            Ok(_) => {
                eprintln!("nvml_device_memory_info {uuid}: invalid counters");
                None
            }
            Err(error) => {
                eprintln!("nvml_device_memory_info {uuid}: {error}");
                None
            }
        };
        let utilization = match device.utilization_rates() {
            Ok(utilization) => percentage_to_per_mille(utilization.gpu),
            Err(error) => {
                eprintln!("nvml_device_utilization {uuid}: {error}");
                None
            }
        };
        let encoder = match device.encoder_utilization() {
            Ok(encoder) => percentage_to_per_mille(encoder.utilization),
            Err(error) => {
                eprintln!("nvml_device_encoder_utilization {uuid}: {error}");
                None
            }
        };
        discovered.push((
            location,
            NodeGpuTelemetry {
                stable_key: format!("nvidia-uuid:{uuid}"),
                name,
                runtime_binding_ready: false,
                dedicated_memory_bytes: memory.as_ref().map(|memory| memory.total),
                used_memory_bytes: memory.as_ref().map(|memory| memory.used),
                utilization_per_mille: utilization,
                encoder_utilization_per_mille: encoder,
            },
        ));
    }
    if device_count > 0 && discovered.is_empty() {
        return Err("NVML found NVIDIA devices but none had a usable identity".into());
    }
    let bindings = node_gpu_runtime_binding::enumerate();
    let mut gpus = Vec::with_capacity(discovered.len());
    for (location, mut gpu) in discovered {
        if let Some(stable_key) = location
            .and_then(|location| node_gpu_runtime_binding::resolve_stable_key(&bindings, location))
        {
            gpu.stable_key = stable_key;
            gpu.runtime_binding_ready = true;
        } else {
            eprintln!(
                "nvidia_render_binding {}: exact physical adapter binding unavailable",
                gpu.stable_key
            );
        }
        gpus.push(gpu);
    }
    gpus.sort_by(|left, right| left.stable_key.cmp(&right.stable_key));
    if gpus
        .windows(2)
        .any(|pair| pair[0].stable_key == pair[1].stable_key)
    {
        return Err("NVIDIA devices resolved to the same physical GPU identity".into());
    }
    Ok(GpuSnapshot {
        sampled_at: Utc::now(),
        gpu_inventory_revision: Some(inventory_revision(&gpus)),
        gpus,
    })
}

fn percentage_to_per_mille(value: u32) -> Option<u16> {
    (value <= 100)
        .then(|| u16::try_from(value * 10).ok())
        .flatten()
}

fn inventory_revision(gpus: &[NodeGpuTelemetry]) -> u64 {
    let mut hasher = Sha256::new();
    for gpu in gpus {
        hasher.update(gpu.stable_key.as_bytes());
        hasher.update([0]);
        hasher.update(gpu.name.as_bytes());
        hasher.update([u8::from(gpu.runtime_binding_ready)]);
        hasher.update([0xff]);
    }
    let digest = hasher.finalize();
    let mut prefix = [0_u8; 8];
    prefix.copy_from_slice(&digest[..8]);
    (u64::from_be_bytes(prefix) & i64::MAX as u64).max(1)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn invalid_or_unsupported_utilization_is_not_fabricated_as_zero() {
        assert_eq!(percentage_to_per_mille(0), Some(0));
        assert_eq!(percentage_to_per_mille(25), Some(250));
        assert_eq!(percentage_to_per_mille(100), Some(1000));
        assert_eq!(percentage_to_per_mille(101), None);
    }

    #[test]
    #[ignore = "requires a physical NVIDIA adapter with NVML and Windows Render binding"]
    fn nvidia_probe_reports_real_memory_gpu_encoder_and_runtime_binding() {
        let snapshot = sample().unwrap();
        assert!(snapshot.gpu_inventory_revision.is_some());
        assert!(!snapshot.gpus.is_empty());
        for gpu in snapshot.gpus {
            assert!(gpu.runtime_binding_ready);
            assert!(gpu.dedicated_memory_bytes.is_some());
            assert!(gpu.used_memory_bytes.is_some());
            assert!(gpu.utilization_per_mille.is_some());
            assert!(gpu.encoder_utilization_per_mille.is_some());
        }
    }
}
