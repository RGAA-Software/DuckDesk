use crate::StoreError;
use chrono::{DateTime, Utc};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TelemetryAlertMetric {
    Cpu,
    Memory,
    Disk,
    Gpu,
}

impl TelemetryAlertMetric {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Cpu => "cpu",
            Self::Memory => "memory",
            Self::Disk => "disk",
            Self::Gpu => "gpu",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TelemetryAlertSeverity {
    Warning,
    Critical,
}

impl TelemetryAlertSeverity {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Warning => "warning",
            Self::Critical => "critical",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TelemetryAlertState {
    Active,
    Acknowledged,
    Recovered,
}

impl TelemetryAlertState {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Acknowledged => "acknowledged",
            Self::Recovered => "recovered",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TelemetryAlertPolicy {
    pub revision: i64,
    pub cpu_warning_per_mille: u16,
    pub cpu_critical_per_mille: u16,
    pub memory_warning_per_mille: u16,
    pub memory_critical_per_mille: u16,
    pub disk_warning_per_mille: u16,
    pub disk_critical_per_mille: u16,
    pub gpu_warning_per_mille: u16,
    pub gpu_critical_per_mille: u16,
    pub trigger_samples: u16,
    pub recovery_samples: u16,
    pub recovery_hysteresis_per_mille: u16,
}

impl TelemetryAlertPolicy {
    pub(crate) fn validate(self) -> Result<ValidatedTelemetryAlertPolicy, StoreError> {
        for (warning, critical) in [
            (self.cpu_warning_per_mille, self.cpu_critical_per_mille),
            (
                self.memory_warning_per_mille,
                self.memory_critical_per_mille,
            ),
            (self.disk_warning_per_mille, self.disk_critical_per_mille),
            (self.gpu_warning_per_mille, self.gpu_critical_per_mille),
        ] {
            if warning == 0
                || warning >= critical
                || critical > 1000
                || self.recovery_hysteresis_per_mille >= warning
            {
                return Err(StoreError::InvalidInput);
            }
        }
        if self.revision < 1
            || !(1..=60).contains(&self.trigger_samples)
            || !(1..=60).contains(&self.recovery_samples)
            || !(1..=250).contains(&self.recovery_hysteresis_per_mille)
        {
            return Err(StoreError::InvalidInput);
        }
        Ok(ValidatedTelemetryAlertPolicy { policy: self })
    }
}

pub(crate) struct ValidatedTelemetryAlertPolicy {
    pub policy: TelemetryAlertPolicy,
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct TelemetryAlertPolicyProfile {
    pub node_id: Uuid,
    pub revision: i64,
    pub cpu_warning_per_mille: i16,
    pub cpu_critical_per_mille: i16,
    pub memory_warning_per_mille: i16,
    pub memory_critical_per_mille: i16,
    pub disk_warning_per_mille: i16,
    pub disk_critical_per_mille: i16,
    pub gpu_warning_per_mille: i16,
    pub gpu_critical_per_mille: i16,
    pub trigger_samples: i16,
    pub recovery_samples: i16,
    pub recovery_hysteresis_per_mille: i16,
    pub updated_at: DateTime<Utc>,
}

impl TelemetryAlertPolicyProfile {
    pub(crate) fn thresholds(&self, metric: TelemetryAlertMetric) -> (i16, i16) {
        match metric {
            TelemetryAlertMetric::Cpu => (self.cpu_warning_per_mille, self.cpu_critical_per_mille),
            TelemetryAlertMetric::Memory => (
                self.memory_warning_per_mille,
                self.memory_critical_per_mille,
            ),
            TelemetryAlertMetric::Disk => {
                (self.disk_warning_per_mille, self.disk_critical_per_mille)
            }
            TelemetryAlertMetric::Gpu => (self.gpu_warning_per_mille, self.gpu_critical_per_mille),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct TelemetryAlertEvent {
    pub id: Uuid,
    pub node_id: Uuid,
    pub metric: String,
    pub resource_key: String,
    pub resource_name: String,
    pub severity: String,
    pub state: String,
    pub threshold_per_mille: i16,
    pub first_value_per_mille: i16,
    pub latest_value_per_mille: i16,
    pub peak_value_per_mille: i16,
    pub occurrence_count: i64,
    pub first_sampled_at: DateTime<Utc>,
    pub last_sampled_at: DateTime<Utc>,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub acknowledged_by: Option<Uuid>,
    pub acknowledged_at: Option<DateTime<Utc>>,
    pub recovered_at: Option<DateTime<Utc>>,
    pub revision: i64,
}

#[derive(Debug, Clone, Copy)]
pub struct TelemetryAlertCursor {
    pub updated_at: DateTime<Utc>,
    pub id: Uuid,
}

#[derive(Debug, Clone, Copy, Default)]
pub struct TelemetryAlertFilter {
    pub node_id: Option<Uuid>,
    pub metric: Option<TelemetryAlertMetric>,
    pub severity: Option<TelemetryAlertSeverity>,
    pub state: Option<TelemetryAlertState>,
    pub before: Option<TelemetryAlertCursor>,
}

#[derive(Debug, Clone, sqlx::FromRow)]
pub(crate) struct TelemetryAlertCondition {
    pub breach_samples: i16,
    pub recovery_samples: i16,
    pub open_event_id: Option<Uuid>,
}

pub(crate) struct TelemetryObservation<'a> {
    pub metric: TelemetryAlertMetric,
    pub resource_key: &'a str,
    pub resource_name: &'a str,
    pub value_per_mille: i16,
}
