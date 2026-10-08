//! Typed, isolated hardware probe protocol. No product settings or persistent files.

use chrono::{DateTime, Utc};
use clap::ValueEnum;
use px_node_protocol::NodeGpuTelemetry;
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Eq, PartialEq, ValueEnum)]
pub(crate) enum ProbeKind {
    Gpu,
    Cpu,
    Memory,
    Disk,
}

impl ProbeKind {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Gpu => "gpu",
            Self::Cpu => "cpu",
            Self::Memory => "memory",
            Self::Disk => "disk",
        }
    }
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct GpuSnapshot {
    pub sampled_at: DateTime<Utc>,
    pub gpu_inventory_revision: Option<u64>,
    pub gpus: Vec<NodeGpuTelemetry>,
}

impl GpuSnapshot {
    pub(crate) fn unavailable() -> Self {
        Self {
            sampled_at: Utc::now(),
            gpu_inventory_revision: None,
            gpus: Vec::new(),
        }
    }
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct CpuSnapshot {
    pub logical_processors: u16,
    pub utilization_per_mille: Option<u16>,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct MemorySnapshot {
    pub total_bytes: u64,
    pub available_bytes: u64,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct DiskSnapshot {
    pub total_bytes: u64,
    pub free_bytes: u64,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(tag = "kind", content = "snapshot", rename_all = "snake_case")]
pub(crate) enum ProbeSnapshot {
    Gpu(GpuSnapshot),
    Cpu(CpuSnapshot),
    Memory(MemorySnapshot),
    Disk(DiskSnapshot),
}

impl ProbeSnapshot {
    pub(crate) fn kind(&self) -> ProbeKind {
        match self {
            Self::Gpu(_) => ProbeKind::Gpu,
            Self::Cpu(_) => ProbeKind::Cpu,
            Self::Memory(_) => ProbeKind::Memory,
            Self::Disk(_) => ProbeKind::Disk,
        }
    }
}

pub(crate) fn run_worker(kind: ProbeKind) -> Result<(), String> {
    let snapshot = match kind {
        ProbeKind::Gpu => ProbeSnapshot::Gpu(crate::node_gpu_telemetry::sample()?),
        ProbeKind::Cpu => ProbeSnapshot::Cpu(crate::node_hardware_telemetry::sample_cpu()?),
        ProbeKind::Memory => {
            ProbeSnapshot::Memory(crate::node_hardware_telemetry::sample_memory()?)
        }
        ProbeKind::Disk => ProbeSnapshot::Disk(crate::node_hardware_telemetry::sample_disk()?),
    };
    let payload = serde_json::to_string(&snapshot).map_err(|error| error.to_string())?;
    println!("{payload}");
    Ok(())
}
