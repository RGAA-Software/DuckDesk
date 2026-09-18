use crate::{
    control,
    node_model::ValidatedNodeTelemetry,
    telemetry_alert_model::{TelemetryAlertCondition, TelemetryObservation},
    StoreError, TelemetryAlertEvent, TelemetryAlertFilter, TelemetryAlertMetric,
    TelemetryAlertPolicy, TelemetryAlertPolicyProfile, TelemetryAlertSeverity, TokenDigest,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct TelemetryAlertStore {
    pub(crate) pool: PgPool,
}

impl TelemetryAlertStore {
    #[cfg(feature = "pg-integration")]
    pub async fn connect(
        config: &px_pg::DatabaseConfig,
        deployment: Uuid,
    ) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
        })
    }

    #[cfg(feature = "pg-integration")]
    pub async fn close(&self) {
        self.pool.close().await;
    }

    pub async fn list(
        &self,
        admin: &TokenDigest,
        filter: TelemetryAlertFilter,
        limit: u32,
    ) -> Result<Vec<TelemetryAlertEvent>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        control::authorize(&mut transaction, admin, false).await?;
        let metric = filter.metric.map(TelemetryAlertMetric::name);
        let severity = filter.severity.map(TelemetryAlertSeverity::name);
        let state = filter.state.map(crate::TelemetryAlertState::name);
        let before_updated_at = filter.before.map(|cursor| cursor.updated_at);
        let before_id = filter.before.map(|cursor| cursor.id);
        let events = sqlx::query_file_as!(
            TelemetryAlertEvent,
            "queries/managed_node_alert_events.sql",
            filter.node_id,
            metric,
            severity,
            state,
            before_updated_at,
            before_id,
            i64::from(limit)
        )
        .fetch_all(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(events)
    }

    pub async fn get(
        &self,
        admin: &TokenDigest,
        event_id: Uuid,
    ) -> Result<TelemetryAlertEvent, StoreError> {
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        control::authorize(&mut transaction, admin, false).await?;
        let event = sqlx::query_file_as!(
            TelemetryAlertEvent,
            "queries/managed_node_alert_event.sql",
            event_id
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        transaction.commit().await?;
        Ok(event)
    }

    pub async fn policy(
        &self,
        admin: &TokenDigest,
        node_id: Uuid,
    ) -> Result<TelemetryAlertPolicyProfile, StoreError> {
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        control::authorize(&mut transaction, admin, false).await?;
        let policy = load_policy(&mut transaction, node_id).await?;
        transaction.commit().await?;
        Ok(policy)
    }

    pub async fn configure_policy(
        &self,
        admin: &TokenDigest,
        node_id: Uuid,
        policy: TelemetryAlertPolicy,
    ) -> Result<TelemetryAlertPolicyProfile, StoreError> {
        let policy = policy.validate()?.policy;
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let actor = control::authorize(&mut transaction, admin, true).await?;
        let updated = sqlx::query_file_as!(
            TelemetryAlertPolicyProfile,
            "queries/update_node_alert_policy.sql",
            node_id,
            policy.revision,
            i16::try_from(policy.cpu_warning_per_mille).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.cpu_critical_per_mille).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.memory_warning_per_mille).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.memory_critical_per_mille)
                .map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.disk_warning_per_mille).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.disk_critical_per_mille).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.gpu_warning_per_mille).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.gpu_critical_per_mille).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.trigger_samples).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.recovery_samples).map_err(|_| StoreError::InvalidInput)?,
            i16::try_from(policy.recovery_hysteresis_per_mille)
                .map_err(|_| StoreError::InvalidInput)?
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        sqlx::query_file!(
            "queries/audit_node_alert_policy.sql",
            Uuid::new_v4(),
            node_id,
            actor,
            updated.revision
        )
        .execute(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(updated)
    }

    pub async fn acknowledge(
        &self,
        admin: &TokenDigest,
        event_id: Uuid,
        revision: i64,
    ) -> Result<TelemetryAlertEvent, StoreError> {
        if revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let actor = control::authorize(&mut transaction, admin, true).await?;
        let event = sqlx::query_file_as!(
            TelemetryAlertEvent,
            "queries/acknowledge_node_alert_event.sql",
            event_id,
            revision,
            actor
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        sqlx::query_file!(
            "queries/audit_node_alert_event.sql",
            Uuid::new_v4(),
            event.id,
            actor,
            event.revision
        )
        .execute(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(event)
    }

    pub async fn prune(&self) -> Result<u64, StoreError> {
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let removed = sqlx::query_file!("queries/prune_node_alert_events.sql")
            .execute(&mut *transaction)
            .await?
            .rows_affected();
        transaction.commit().await?;
        Ok(removed)
    }
}

pub(crate) async fn insert_default_policy(
    connection: &mut PgConnection,
    node_id: Uuid,
) -> Result<(), StoreError> {
    sqlx::query_file!("queries/insert_node_alert_policy.sql", node_id)
        .execute(connection)
        .await?;
    Ok(())
}

pub(crate) async fn observe(
    connection: &mut PgConnection,
    node_id: Uuid,
    node_generation: i64,
    report_sequence: i64,
    telemetry: &ValidatedNodeTelemetry,
) -> Result<(), StoreError> {
    let policy = load_policy(connection, node_id).await?;
    if let Some(value_per_mille) = telemetry.cpu_utilization_per_mille {
        observe_metric(
            connection,
            node_id,
            node_generation,
            report_sequence,
            telemetry.sampled_at,
            &policy,
            TelemetryObservation {
                metric: TelemetryAlertMetric::Cpu,
                resource_key: "cpu",
                resource_name: "CPU",
                value_per_mille,
            },
        )
        .await?;
    }
    if let Some(value_per_mille) = utilization(
        telemetry.memory_total_bytes,
        telemetry.memory_available_bytes,
    ) {
        observe_metric(
            connection,
            node_id,
            node_generation,
            report_sequence,
            telemetry.sampled_at,
            &policy,
            TelemetryObservation {
                metric: TelemetryAlertMetric::Memory,
                resource_key: "memory",
                resource_name: "Memory",
                value_per_mille,
            },
        )
        .await?;
    }
    if let Some(value_per_mille) =
        utilization(telemetry.disk_total_bytes, telemetry.disk_free_bytes)
    {
        observe_metric(
            connection,
            node_id,
            node_generation,
            report_sequence,
            telemetry.sampled_at,
            &policy,
            TelemetryObservation {
                metric: TelemetryAlertMetric::Disk,
                resource_key: "fixed_disks",
                resource_name: "Fixed disks",
                value_per_mille,
            },
        )
        .await?;
    }
    for gpu in &telemetry.gpus {
        if let Some(value_per_mille) = gpu.utilization_per_mille {
            observe_metric(
                connection,
                node_id,
                node_generation,
                report_sequence,
                telemetry.sampled_at,
                &policy,
                TelemetryObservation {
                    metric: TelemetryAlertMetric::Gpu,
                    resource_key: &gpu.stable_key,
                    resource_name: &gpu.name,
                    value_per_mille,
                },
            )
            .await?;
        }
    }
    Ok(())
}

async fn load_policy(
    connection: &mut PgConnection,
    node_id: Uuid,
) -> Result<TelemetryAlertPolicyProfile, StoreError> {
    sqlx::query_file_as!(
        TelemetryAlertPolicyProfile,
        "queries/node_alert_policy.sql",
        node_id
    )
    .fetch_optional(connection)
    .await?
    .ok_or(StoreError::Rejected)
}

#[allow(clippy::too_many_arguments)]
async fn observe_metric(
    connection: &mut PgConnection,
    node_id: Uuid,
    node_generation: i64,
    report_sequence: i64,
    sampled_at: chrono::DateTime<chrono::Utc>,
    policy: &TelemetryAlertPolicyProfile,
    observation: TelemetryObservation<'_>,
) -> Result<(), StoreError> {
    let previous = sqlx::query_file_as!(
        TelemetryAlertCondition,
        "queries/lock_node_alert_condition.sql",
        node_id,
        observation.metric.name(),
        observation.resource_key
    )
    .fetch_optional(&mut *connection)
    .await?;
    let (warning_threshold, critical_threshold) = policy.thresholds(observation.metric);
    let open_event_id = previous
        .as_ref()
        .and_then(|condition| condition.open_event_id);
    let above_warning = observation.value_per_mille >= warning_threshold;
    let below_recovery =
        observation.value_per_mille <= warning_threshold - policy.recovery_hysteresis_per_mille;
    let mut breach_samples = if above_warning {
        previous
            .as_ref()
            .map_or(1, |condition| condition.breach_samples.saturating_add(1))
            .min(60)
    } else {
        0
    };
    let mut recovery_samples = if open_event_id.is_some() && below_recovery {
        previous
            .as_ref()
            .map_or(1, |condition| condition.recovery_samples.saturating_add(1))
            .min(60)
    } else {
        0
    };
    let mut next_event_id = open_event_id;
    if let Some(event_id) = open_event_id {
        if recovery_samples >= policy.recovery_samples {
            sqlx::query_file!(
                "queries/recover_node_alert_event.sql",
                event_id,
                observation.value_per_mille,
                sampled_at
            )
            .execute(&mut *connection)
            .await?;
            next_event_id = None;
            breach_samples = 0;
            recovery_samples = 0;
        } else if above_warning {
            let severity = severity(observation.value_per_mille, critical_threshold);
            let threshold = if severity == TelemetryAlertSeverity::Critical {
                critical_threshold
            } else {
                warning_threshold
            };
            sqlx::query_file!(
                "queries/refresh_node_alert_event.sql",
                event_id,
                observation.resource_name,
                severity.name(),
                threshold,
                observation.value_per_mille,
                sampled_at
            )
            .execute(&mut *connection)
            .await?;
        }
    } else if breach_samples >= policy.trigger_samples {
        let event_id = Uuid::new_v4();
        let severity = severity(observation.value_per_mille, critical_threshold);
        let threshold = if severity == TelemetryAlertSeverity::Critical {
            critical_threshold
        } else {
            warning_threshold
        };
        sqlx::query_file!(
            "queries/insert_node_alert_event.sql",
            event_id,
            node_id,
            observation.metric.name(),
            observation.resource_key,
            observation.resource_name,
            severity.name(),
            threshold,
            observation.value_per_mille,
            sampled_at
        )
        .execute(&mut *connection)
        .await?;
        next_event_id = Some(event_id);
    }
    sqlx::query_file!(
        "queries/upsert_node_alert_condition.sql",
        node_id,
        observation.metric.name(),
        observation.resource_key,
        observation.resource_name,
        breach_samples,
        recovery_samples,
        next_event_id,
        observation.value_per_mille,
        sampled_at,
        node_generation,
        report_sequence
    )
    .execute(connection)
    .await?;
    Ok(())
}

fn utilization(total: Option<i64>, available: Option<i64>) -> Option<i16> {
    let (total, available) = total.zip(available)?;
    if total <= 0 {
        return None;
    }
    let used = i128::from(total - available);
    i16::try_from((used * 1000 / i128::from(total)).clamp(0, 1000)).ok()
}

fn severity(value_per_mille: i16, critical_threshold: i16) -> TelemetryAlertSeverity {
    if value_per_mille >= critical_threshold {
        TelemetryAlertSeverity::Critical
    } else {
        TelemetryAlertSeverity::Warning
    }
}
