use crate::node_gpu_telemetry::{
    enrich_nvidia_metrics, enrich_windows_metrics, stable_gpu_key, EnumeratedGpu,
};
use chrono::Utc;
use px_node_protocol::{NodeGpuTelemetry, NodeTelemetry, TelemetryProbeState};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use wmi::{COMLibrary, WMIConnection};

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct ProcessorRow {
    load_percentage: Option<u16>,
    number_of_logical_processors: Option<u32>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct OperatingSystemRow {
    total_visible_memory_size: Option<u64>,
    free_physical_memory: Option<u64>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct LogicalDiskRow {
    size: Option<u64>,
    free_space: Option<u64>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct VideoControllerRow {
    pnp_device_id: Option<String>,
    name: Option<String>,
}

pub(crate) fn sample() -> NodeTelemetry {
    sample_inner().unwrap_or_else(|error| {
        tracing::warn!(error = %error, "node telemetry sampling failed");
        unavailable()
    })
}

fn sample_inner() -> Result<NodeTelemetry, String> {
    let com = COMLibrary::new().map_err(|error| error.to_string())?;
    let wmi = WMIConnection::new(com).map_err(|error| error.to_string())?;
    let processors: Vec<ProcessorRow> = wmi
        .raw_query("SELECT LoadPercentage, NumberOfLogicalProcessors FROM Win32_Processor")
        .map_err(|error| error.to_string())?;
    let operating_systems: Vec<OperatingSystemRow> = wmi
        .raw_query("SELECT TotalVisibleMemorySize, FreePhysicalMemory FROM Win32_OperatingSystem")
        .map_err(|error| error.to_string())?;
    let disks: Vec<LogicalDiskRow> = wmi
        .raw_query("SELECT Size, FreeSpace FROM Win32_LogicalDisk WHERE DriveType=3")
        .map_err(|error| error.to_string())?;
    let gpu_result: Result<Vec<VideoControllerRow>, _> =
        wmi.raw_query("SELECT PNPDeviceID, Name FROM Win32_VideoController");

    let logical_processors = processors
        .iter()
        .filter_map(|processor| processor.number_of_logical_processors)
        .try_fold(0_u32, |total, count| total.checked_add(count))
        .and_then(|total| u16::try_from(total).ok())
        .filter(|total| *total > 0);
    let cpu_samples = processors
        .iter()
        .filter_map(|processor| processor.load_percentage)
        .filter(|percentage| *percentage <= 100)
        .collect::<Vec<_>>();
    let cpu_utilization_per_mille = (!cpu_samples.is_empty()).then(|| {
        let total = cpu_samples
            .iter()
            .map(|value| u32::from(*value))
            .sum::<u32>();
        u16::try_from((total * 10) / u32::try_from(cpu_samples.len()).unwrap_or(1)).unwrap_or(1000)
    });
    let memory = operating_systems.first().and_then(|row| {
        let total = row.total_visible_memory_size?.checked_mul(1024)?;
        let available = row.free_physical_memory?.checked_mul(1024)?;
        (available <= total).then_some((total, available))
    });
    let disk = disks
        .iter()
        .try_fold((0_u64, 0_u64), |(total, free), row| {
            Some((
                total.checked_add(row.size?)?,
                free.checked_add(row.free_space?)?,
            ))
        })
        .filter(|(total, free)| *total > 0 && free <= total);
    let (gpu_inventory_revision, gpus, gpu_inventory_ready) = match gpu_result {
        Ok(rows) => {
            let mut enumerated_gpus = rows
                .into_iter()
                .filter_map(|row| {
                    let identity = row.pnp_device_id?.trim().to_uppercase();
                    let name = row.name?.trim().to_string();
                    if identity.is_empty() || name.is_empty() || name.len() > 256 {
                        return None;
                    }
                    Some(EnumeratedGpu {
                        pnp_identity: identity.clone(),
                        telemetry: NodeGpuTelemetry {
                            stable_key: stable_gpu_key(&identity),
                            name,
                            runtime_binding_ready: false,
                            dedicated_memory_bytes: None,
                            used_memory_bytes: None,
                            utilization_per_mille: None,
                            encoder_utilization_per_mille: None,
                        },
                    })
                })
                .collect::<Vec<_>>();
            enrich_windows_metrics(&mut enumerated_gpus, &wmi);
            enrich_nvidia_metrics(&mut enumerated_gpus);
            let mut gpus = enumerated_gpus
                .into_iter()
                .map(|gpu| gpu.telemetry)
                .collect::<Vec<_>>();
            gpus.sort_by(|left, right| left.stable_key.cmp(&right.stable_key));
            gpus.dedup_by(|left, right| left.stable_key == right.stable_key);
            (Some(gpu_inventory_revision(&gpus)), gpus, true)
        }
        Err(_) => (None, Vec::new(), false),
    };
    let machine_ready = logical_processors.is_some() && memory.is_some() && disk.is_some();
    let has_data = logical_processors.is_some()
        || cpu_utilization_per_mille.is_some()
        || memory.is_some()
        || disk.is_some()
        || gpu_inventory_revision.is_some();
    let probe_state = if machine_ready && gpu_inventory_ready {
        TelemetryProbeState::Ready
    } else if has_data {
        TelemetryProbeState::Partial
    } else {
        TelemetryProbeState::Unavailable
    };
    Ok(NodeTelemetry {
        sampled_at: Utc::now(),
        probe_state,
        logical_processors,
        cpu_utilization_per_mille,
        memory_total_bytes: memory.map(|value| value.0),
        memory_available_bytes: memory.map(|value| value.1),
        disk_total_bytes: disk.map(|value| value.0),
        disk_free_bytes: disk.map(|value| value.1),
        gpu_inventory_revision,
        gpus,
    })
}

pub(crate) fn unavailable() -> NodeTelemetry {
    NodeTelemetry {
        sampled_at: Utc::now(),
        probe_state: TelemetryProbeState::Unavailable,
        logical_processors: None,
        cpu_utilization_per_mille: None,
        memory_total_bytes: None,
        memory_available_bytes: None,
        disk_total_bytes: None,
        disk_free_bytes: None,
        gpu_inventory_revision: None,
        gpus: Vec::new(),
    }
}

fn gpu_inventory_revision(gpus: &[NodeGpuTelemetry]) -> u64 {
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
    (u64::from_be_bytes(prefix) & u64::try_from(i64::MAX).expect("i64::MAX fits in u64")).max(1)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn gpu_identity_is_stable_private_and_inventory_order_independent() {
        let first = stable_gpu_key("PCI\\VEN_10DE&DEV_2487&SUBSYS_TEST");
        let second = stable_gpu_key("PCI\\VEN_1002&DEV_73BF&SUBSYS_TEST");
        assert_ne!(first, second);
        assert!(first.starts_with("pnp-sha256:"));
        assert!(!first.contains("VEN_"));
        let mut forward = vec![gpu(&first, "GPU A"), gpu(&second, "GPU B")];
        let mut reverse = vec![gpu(&second, "GPU B"), gpu(&first, "GPU A")];
        forward.sort_by(|left, right| left.stable_key.cmp(&right.stable_key));
        reverse.sort_by(|left, right| left.stable_key.cmp(&right.stable_key));
        assert_eq!(
            gpu_inventory_revision(&forward),
            gpu_inventory_revision(&reverse)
        );
    }

    #[test]
    fn windows_probe_reports_a_consistent_machine_snapshot() {
        let telemetry = sample();
        assert_ne!(telemetry.probe_state, TelemetryProbeState::Unavailable);
        assert!(telemetry.logical_processors.is_some());
        assert!(telemetry.memory_total_bytes.is_some());
        assert!(telemetry.memory_available_bytes <= telemetry.memory_total_bytes);
        assert!(telemetry.disk_total_bytes.is_some());
        assert!(telemetry.disk_free_bytes <= telemetry.disk_total_bytes);
        assert!(telemetry.gpu_inventory_revision.is_some());
    }

    #[test]
    #[ignore = "requires a physical NVIDIA adapter and NVML driver"]
    fn nvidia_probe_reports_real_memory_gpu_and_encoder_metrics() {
        let telemetry = sample();
        let gpu = telemetry
            .gpus
            .iter()
            .find(|gpu| gpu.name.to_ascii_lowercase().contains("nvidia"))
            .expect("a physical NVIDIA adapter is required");
        assert!(gpu.runtime_binding_ready);
        assert!(gpu.dedicated_memory_bytes.is_some());
        assert!(gpu.used_memory_bytes <= gpu.dedicated_memory_bytes);
        assert!(gpu.utilization_per_mille.is_some());
        assert!(gpu.encoder_utilization_per_mille.is_some());
    }

    fn gpu(stable_key: &str, name: &str) -> NodeGpuTelemetry {
        NodeGpuTelemetry {
            stable_key: stable_key.to_string(),
            name: name.to_string(),
            runtime_binding_ready: false,
            dedicated_memory_bytes: None,
            used_memory_bytes: None,
            utilization_per_mille: None,
            encoder_utilization_per_mille: None,
        }
    }
}
