use crate::{
    control,
    instance_model::InstanceRow,
    instance_state::{self, Transition},
    instance_stop, node_lifecycle, InstanceStore, NodeConnection, StoreError,
};
use chrono::{DateTime, Utc};
use std::collections::BTreeMap;
use uuid::Uuid;

#[derive(Debug, Clone, serde::Serialize)]
pub struct ExpectedLaunch {
    pub instance_id: Uuid,
    pub launch_id: Uuid,
    pub reject_through_revision: i64,
    pub port: u16,
    pub desired_state: String,
}
/// The agent must first persist these per-launch revision fences, then gather a complete
/// inventory of its owned runtimes. It must also report unknown runtimes, never adopt/kill
/// processes by matching only a PID, executable path or port.
#[derive(Debug, Clone, serde::Serialize)]
pub struct ReconciliationChallenge {
    pub id: Uuid,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub deadline: DateTime<Utc>,
    pub launches: Vec<ExpectedLaunch>,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ObservedRuntimePhase {
    Starting,
    Running,
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ObservedRuntime {
    pub instance_id: Uuid,
    pub launch_id: Uuid,
    pub port: u16,
    pub phase: ObservedRuntimePhase,
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RuntimeInventory {
    pub challenge_id: Uuid,
    pub runtimes: Vec<ObservedRuntime>,
}
impl InstanceStore {
    pub async fn begin_reconciliation(
        &self,
        connection: &NodeConnection,
    ) -> Result<ReconciliationChallenge, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let node = node_lifecycle::authorize(&mut tx, connection).await?;
        node_lifecycle::invalidate(&mut tx, Some(node.id)).await?;
        let id = Uuid::new_v4();
        let deadline =
            sqlx::query_file_scalar!("queries/start_node_reconciliation.sql", node.id, id)
                .fetch_one(&mut *tx)
                .await?;
        let records =
            sqlx::query_file_as!(InstanceRow, "queries/active_node_instances.sql", node.id)
                .fetch_all(&mut *tx)
                .await?;
        if records.len() > 128 {
            tx.commit().await?; // retain closed admission even for a corrupt oversized inventory
            return Err(StoreError::Rejected);
        }
        let mut launches = Vec::with_capacity(records.len());
        for record in records {
            launches.push(ExpectedLaunch {
                instance_id: record.id,
                launch_id: record.launch_id,
                reject_through_revision: record.revision,
                port: record
                    .port
                    .try_into()
                    .map_err(|_| StoreError::InvalidInput)?,
                desired_state: record.desired_state,
            });
        }
        tx.commit().await?;
        Ok(ReconciliationChallenge {
            id,
            node_generation: node.generation,
            control_epoch: node.control_epoch,
            deadline,
            launches,
        })
    }
    pub async fn reconcile(
        &self,
        connection: &NodeConnection,
        inventory: &RuntimeInventory,
    ) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let node = node_lifecycle::authorize(&mut tx, connection).await?;
        if !sqlx::query_file_scalar!(
            "queries/check_node_reconciliation.sql",
            node.id,
            inventory.challenge_id
        )
        .fetch_one(&mut *tx)
        .await?
        {
            return Err(StoreError::Rejected);
        }
        if inventory.runtimes.len() > 128 {
            return Err(StoreError::InvalidInput);
        }
        let mut observed = BTreeMap::new();
        for runtime in &inventory.runtimes {
            if runtime.instance_id.is_nil()
                || runtime.launch_id.is_nil()
                || runtime.port == 0
                || observed.insert(runtime.instance_id, runtime).is_some()
            {
                return Err(StoreError::InvalidInput);
            }
        }
        let records =
            sqlx::query_file_as!(InstanceRow, "queries/active_node_instances.sql", node.id)
                .fetch_all(&mut *tx)
                .await?;
        if records.len() > 128 {
            return Err(StoreError::Rejected);
        }
        // Validate the entire inventory before changing any instance. Unknown/terminal
        // identities or mismatched launch/port leave the node Reconciling, not Ready.
        for (id, runtime) in &observed {
            let Some(record) = records.iter().find(|record| record.id == *id) else {
                return Err(StoreError::Rejected);
            };
            if record.launch_id != runtime.launch_id || record.port != i32::from(runtime.port) {
                return Err(StoreError::Rejected);
            }
        }
        for record in records {
            match observed.get(&record.id) {
                Some(_) if record.desired_state == "stopped" => {
                    instance_stop::enqueue(&mut tx, &record, &node).await?;
                }
                Some(runtime) => {
                    if !instance_state::owner_authorized(&mut tx, record.id).await? {
                        instance_stop::enqueue(&mut tx, &record, &node).await?;
                        continue;
                    }
                    let transition = match runtime.phase {
                        ObservedRuntimePhase::Running => Transition::ReconciledRunning,
                        ObservedRuntimePhase::Starting => Transition::ReconciledStarting,
                    };
                    instance_state::change(&mut tx, &record, Some(&node), transition).await?;
                }
                None => {
                    let transition = if record.desired_state == "stopped" {
                        Transition::Stopped
                    } else {
                        Transition::Failed
                    };
                    instance_state::cancel(&mut tx, record.id).await?;
                    instance_state::change(&mut tx, &record, Some(&node), transition).await?;
                }
            }
        }
        if sqlx::query_file!(
            "queries/finish_node_reconciliation.sql",
            node.id,
            inventory.challenge_id
        )
        .execute(&mut *tx)
        .await?
        .rows_affected()
            != 1
        {
            return Err(StoreError::Rejected);
        }
        tx.commit().await?;
        Ok(())
    }
}
