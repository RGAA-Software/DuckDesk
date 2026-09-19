use crate::{StoreError, TokenDigest};
use chrono::{DateTime, TimeDelta, Utc};
use std::collections::HashSet;
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum NodeProduct {
    CloudNode,
    Remote,
}
impl NodeProduct {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::CloudNode => "cloud_node",
            Self::Remote => "remote",
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeEpoch(pub(crate) i64);
impl RuntimeEpoch {
    pub fn value(self) -> i64 {
        self.0
    }
}
/// Server-side connection context, not a request DTO. Constructed only by node authentication.
#[derive(Debug, Clone)]
pub struct NodeConnection {
    pub(crate) id: Uuid,
    pub(crate) generation: i64,
    pub(crate) epoch: RuntimeEpoch,
    pub(crate) key: TokenDigest,
}
impl NodeConnection {
    pub fn id(&self) -> Uuid {
        self.id
    }
    pub fn generation(&self) -> i64 {
        self.generation
    }
    pub fn epoch(&self) -> RuntimeEpoch {
        self.epoch
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeConfiguration {
    pub draining: bool,
    pub disabled: bool,
    pub max_instances: u32,
}
impl NodeConfiguration {
    pub(crate) fn validate(&self) -> Result<(), StoreError> {
        if !(1..=64).contains(&self.max_instances) {
            return Err(StoreError::InvalidInput);
        }
        Ok(())
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeReport {
    pub sequence: u64,
    pub product_version_code: u32,
    pub public_host: String,
    pub desktop_port: u16,
    pub application_port_start: u16,
    pub application_port_end: u16,
    pub game_hook: bool,
    pub webview: bool,
    pub rdp: bool,
    pub telemetry: NodeTelemetry,
}
impl NodeReport {
    pub(crate) fn validate(&self) -> Result<ValidatedNodeReport, StoreError> {
        let sequence = i64::try_from(self.sequence).map_err(|_| StoreError::InvalidInput)?;
        if sequence < 1
            || self.product_version_code == 0
            || self.desktop_port == 0
            || self.application_port_start == 0
            || self.application_port_start > self.application_port_end
            || (self.application_port_start..=self.application_port_end)
                .contains(&self.desktop_port)
            || self.public_host.is_empty()
            || self.public_host.len() > 253
            || self.public_host.chars().any(char::is_whitespace)
            || self.public_host.chars().any(char::is_control)
            || self.public_host.contains(['/', '\\', '@', '?', '#'])
        {
            return Err(StoreError::InvalidInput);
        }
        let host = if let Ok(ip) = self.public_host.parse::<std::net::IpAddr>() {
            ip.to_string()
        } else {
            url::Host::parse(&self.public_host)
                .map_err(|_| StoreError::InvalidInput)?
                .to_string()
        };
        Ok(ValidatedNodeReport {
            sequence,
            host,
            telemetry: self.telemetry.validate()?,
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TelemetryProbeState {
    Ready,
    Partial,
    Unavailable,
}
impl TelemetryProbeState {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Ready => "ready",
            Self::Partial => "partial",
            Self::Unavailable => "unavailable",
        }
    }
}

#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeGpuTelemetry {
    pub stable_key: String,
    pub name: String,
    pub runtime_binding_ready: bool,
    pub dedicated_memory_bytes: Option<u64>,
    pub used_memory_bytes: Option<u64>,
    pub utilization_per_mille: Option<u16>,
    pub encoder_utilization_per_mille: Option<u16>,
}

#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeTelemetry {
    pub sampled_at: DateTime<Utc>,
    pub probe_state: TelemetryProbeState,
    pub logical_processors: Option<u16>,
    pub cpu_utilization_per_mille: Option<u16>,
    pub memory_total_bytes: Option<u64>,
    pub memory_available_bytes: Option<u64>,
    pub disk_total_bytes: Option<u64>,
    pub disk_free_bytes: Option<u64>,
    pub gpu_inventory_revision: Option<u64>,
    pub gpus: Vec<NodeGpuTelemetry>,
}

pub(crate) struct ValidatedNodeReport {
    pub sequence: i64,
    pub host: String,
    pub telemetry: ValidatedNodeTelemetry,
}

pub(crate) struct ValidatedNodeTelemetry {
    pub sampled_at: DateTime<Utc>,
    pub probe_state: &'static str,
    pub logical_processors: Option<i16>,
    pub cpu_utilization_per_mille: Option<i16>,
    pub memory_total_bytes: Option<i64>,
    pub memory_available_bytes: Option<i64>,
    pub disk_total_bytes: Option<i64>,
    pub disk_free_bytes: Option<i64>,
    pub gpu_inventory_revision: Option<i64>,
    pub gpus: Vec<ValidatedNodeGpuTelemetry>,
}

pub(crate) struct ValidatedNodeGpuTelemetry {
    pub stable_key: String,
    pub name: String,
    pub runtime_binding_ready: bool,
    pub dedicated_memory_bytes: Option<i64>,
    pub used_memory_bytes: Option<i64>,
    pub utilization_per_mille: Option<i16>,
    pub encoder_utilization_per_mille: Option<i16>,
}

impl NodeTelemetry {
    fn validate(&self) -> Result<ValidatedNodeTelemetry, StoreError> {
        let now = Utc::now();
        if self.sampled_at > now + TimeDelta::minutes(5)
            || self.sampled_at < now - TimeDelta::hours(1)
            || self.gpus.len() > 16
        {
            return Err(StoreError::InvalidInput);
        }
        let logical_processors = optional_i16(self.logical_processors, 1, 1024)?;
        let cpu_utilization_per_mille = optional_i16(self.cpu_utilization_per_mille, 0, 1000)?;
        let memory_total_bytes = optional_i64(self.memory_total_bytes)?;
        let memory_available_bytes = optional_i64(self.memory_available_bytes)?;
        let disk_total_bytes = optional_i64(self.disk_total_bytes)?;
        let disk_free_bytes = optional_i64(self.disk_free_bytes)?;
        let gpu_inventory_revision = optional_i64(self.gpu_inventory_revision)?;
        if memory_available_bytes
            .zip(memory_total_bytes)
            .is_some_and(|(available, total)| available > total)
            || memory_available_bytes.is_some() != memory_total_bytes.is_some()
            || disk_free_bytes
                .zip(disk_total_bytes)
                .is_some_and(|(free, total)| free > total)
            || disk_free_bytes.is_some() != disk_total_bytes.is_some()
            || (self.gpu_inventory_revision.is_none() && !self.gpus.is_empty())
        {
            return Err(StoreError::InvalidInput);
        }
        let has_machine_data = logical_processors.is_some()
            || cpu_utilization_per_mille.is_some()
            || memory_total_bytes.is_some()
            || memory_available_bytes.is_some()
            || disk_total_bytes.is_some()
            || disk_free_bytes.is_some()
            || gpu_inventory_revision.is_some();
        match self.probe_state {
            TelemetryProbeState::Ready
                if logical_processors.is_none()
                    || memory_total_bytes.is_none()
                    || memory_available_bytes.is_none()
                    || disk_total_bytes.is_none()
                    || disk_free_bytes.is_none()
                    || gpu_inventory_revision.is_none() =>
            {
                return Err(StoreError::InvalidInput);
            }
            TelemetryProbeState::Partial if !has_machine_data => {
                return Err(StoreError::InvalidInput);
            }
            TelemetryProbeState::Unavailable if has_machine_data || !self.gpus.is_empty() => {
                return Err(StoreError::InvalidInput);
            }
            _ => {}
        }
        let mut stable_keys = HashSet::with_capacity(self.gpus.len());
        let mut gpus = Vec::with_capacity(self.gpus.len());
        for gpu in &self.gpus {
            if gpu.stable_key.is_empty()
                || gpu.stable_key.len() > 128
                || gpu.stable_key.chars().any(char::is_whitespace)
                || gpu.name.is_empty()
                || gpu.name.len() > 256
                || gpu.name.chars().any(char::is_control)
                || !stable_keys.insert(gpu.stable_key.as_str())
            {
                return Err(StoreError::InvalidInput);
            }
            let dedicated_memory_bytes = optional_i64(gpu.dedicated_memory_bytes)?;
            let used_memory_bytes = optional_i64(gpu.used_memory_bytes)?;
            if used_memory_bytes
                .zip(dedicated_memory_bytes)
                .is_some_and(|(used, total)| used > total)
                || (used_memory_bytes.is_some() && dedicated_memory_bytes.is_none())
            {
                return Err(StoreError::InvalidInput);
            }
            gpus.push(ValidatedNodeGpuTelemetry {
                stable_key: gpu.stable_key.clone(),
                name: gpu.name.clone(),
                runtime_binding_ready: gpu.runtime_binding_ready,
                dedicated_memory_bytes,
                used_memory_bytes,
                utilization_per_mille: optional_i16(gpu.utilization_per_mille, 0, 1000)?,
                encoder_utilization_per_mille: optional_i16(
                    gpu.encoder_utilization_per_mille,
                    0,
                    1000,
                )?,
            });
        }
        Ok(ValidatedNodeTelemetry {
            sampled_at: self.sampled_at,
            probe_state: self.probe_state.name(),
            logical_processors,
            cpu_utilization_per_mille,
            memory_total_bytes,
            memory_available_bytes,
            disk_total_bytes,
            disk_free_bytes,
            gpu_inventory_revision,
            gpus,
        })
    }
}

fn optional_i16<T>(value: Option<T>, minimum: T, maximum: T) -> Result<Option<i16>, StoreError>
where
    T: Copy + PartialOrd,
    i16: TryFrom<T>,
{
    value
        .map(|number| {
            if number < minimum || number > maximum {
                return Err(StoreError::InvalidInput);
            }
            i16::try_from(number).map_err(|_| StoreError::InvalidInput)
        })
        .transpose()
}

fn optional_i64(value: Option<u64>) -> Result<Option<i64>, StoreError> {
    value
        .map(|number| i64::try_from(number).map_err(|_| StoreError::InvalidInput))
        .transpose()
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct NodeTelemetryProfile {
    pub node_id: Uuid,
    pub node_generation: i64,
    pub report_sequence: i64,
    pub probe_state: String,
    pub sampled_at: DateTime<Utc>,
    pub received_at: DateTime<Utc>,
    pub logical_processors: Option<i16>,
    pub cpu_utilization_per_mille: Option<i16>,
    pub memory_total_bytes: Option<i64>,
    pub memory_available_bytes: Option<i64>,
    pub disk_total_bytes: Option<i64>,
    pub disk_free_bytes: Option<i64>,
    pub gpu_inventory_revision: Option<i64>,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct NodeGpuProfile {
    pub node_id: Uuid,
    pub stable_key: String,
    pub inventory_revision: i64,
    pub name: String,
    pub runtime_binding_ready: bool,
    pub dedicated_memory_bytes: Option<i64>,
    pub used_memory_bytes: Option<i64>,
    pub utilization_per_mille: Option<i16>,
    pub encoder_utilization_per_mille: Option<i16>,
    pub sampled_at: DateTime<Utc>,
    pub received_at: DateTime<Utc>,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct NodeGpuHistoryProfile {
    pub node_id: Uuid,
    pub node_generation: i64,
    pub report_sequence: i64,
    pub stable_key: String,
    pub inventory_revision: i64,
    pub name: String,
    pub runtime_binding_ready: bool,
    pub dedicated_memory_bytes: Option<i64>,
    pub used_memory_bytes: Option<i64>,
    pub utilization_per_mille: Option<i16>,
    pub encoder_utilization_per_mille: Option<i16>,
    pub sampled_at: DateTime<Utc>,
    pub received_at: DateTime<Utc>,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ManagedNodeTelemetrySample {
    #[serde(flatten)]
    pub telemetry: NodeTelemetryProfile,
    pub gpus: Vec<NodeGpuHistoryProfile>,
}

#[derive(Debug, Clone, Copy)]
pub struct TelemetryHistoryCursor {
    pub received_at: DateTime<Utc>,
    pub node_generation: i64,
    pub report_sequence: i64,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ManagedNodeProfile {
    #[serde(flatten)]
    pub node: NodeProfile,
    pub telemetry: Option<NodeTelemetryProfile>,
    pub gpus: Vec<NodeGpuProfile>,
}
/// Administrative view only. Fresh means recent authenticated contact, not reconciled capacity.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct NodeProfile {
    pub id: Uuid,
    pub device_id: Uuid,
    pub product: String,
    pub revision: i64,
    pub generation: i64,
    pub control_epoch: Option<i64>,
    pub state: String,
    pub draining: bool,
    pub disabled: bool,
    pub max_instances: i32,
    pub report_sequence: i64,
    pub last_seen: Option<DateTime<Utc>>,
    pub product_version_code: Option<i64>,
    pub public_host: Option<String>,
    pub desktop_port: Option<i32>,
    pub application_port_start: Option<i32>,
    pub application_port_end: Option<i32>,
    pub game_hook: bool,
    pub webview: bool,
    pub rdp: bool,
    pub endpoint_revision: i64,
    pub fresh: bool,
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn node_reports_reject_overflow_uri_ports_and_unknown_endpoint_values() {
        let mut report = NodeReport {
            sequence: 1,
            product_version_code: 1,
            public_host: "node.example.test".into(),
            desktop_port: 4601,
            application_port_start: 4613,
            application_port_end: 4998,
            game_hook: true,
            webview: true,
            rdp: true,
            telemetry: NodeTelemetry {
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
            },
        };
        assert!(report.validate().is_ok());
        for host in [
            "",
            "https://node.example.test",
            "node.example.test:443",
            "x@y.test",
            "a b",
            "a/b",
            "a\\b",
            "a#fragment",
        ] {
            report.public_host = host.into();
            assert!(report.validate().is_err(), "{host}");
        }
        report.public_host = "::1".into();
        assert!(report.validate().is_ok());
        report.sequence = u64::MAX;
        assert!(report.validate().is_err());
        report.sequence = 1;
        report.desktop_port = 4613;
        assert!(report.validate().is_err());
        report.desktop_port = 4601;
        report.application_port_start = 0;
        assert!(report.validate().is_err());
        assert!(NodeConfiguration {
            draining: false,
            disabled: false,
            max_instances: u32::MAX
        }
        .validate()
        .is_err());
    }
}
