use crate::{
    command_model::{CommandRow, LaunchRow},
    control,
    instance_state::{self, Transition},
    instance_stop, node_lifecycle, ApplicationInstance, CommandOutcome, CommandReceipt,
    InstanceStore, NodeCommand, NodeCommandAction, NodeConnection, StoreError,
};
use uuid::Uuid;

impl InstanceStore {
    /// Returns committed work for this authenticated connection only. Network delivery is
    /// outside this transaction; the node must persist command-ID/revision deduplication.
    pub async fn next_command(
        &self,
        connection: &NodeConnection,
    ) -> Result<Option<NodeCommand>, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let node = node_lifecycle::authorize(&mut tx, connection).await?;
        for _ in 0..16 {
            let Some(command) = sqlx::query_file_as!(
                CommandRow,
                "queries/lock_next_instance_command.sql",
                node.id,
                node.generation,
                node.control_epoch
            )
            .fetch_optional(&mut *tx)
            .await?
            else {
                tx.commit().await?;
                return Ok(None);
            };
            let mut instance = instance_state::lock(&mut tx, command.instance_id).await?;
            if instance.node_id != node.id
                || instance.revision != command.instance_revision
                || instance.ended_at.is_some()
            {
                sqlx::query_file!("queries/cancel_instance_command.sql", command.id)
                    .execute(&mut *tx)
                    .await?;
                continue;
            }
            if command.expired {
                instance_state::cancel(&mut tx, instance.id).await?;
                if command.kind == "start" && command.attempts == 0 && instance.state == "reserved"
                {
                    instance_state::change(&mut tx, &instance, Some(&node), Transition::Failed)
                        .await?;
                } else {
                    instance_state::change(&mut tx, &instance, Some(&node), Transition::Unknown)
                        .await?;
                    sqlx::query_file!("queries/mark_node_reconciling.sql", node.id)
                        .execute(&mut *tx)
                        .await?;
                }
                continue;
            }
            let action = match command.kind.as_str() {
                "start" => {
                    let eligible =
                        sqlx::query_file_scalar!("queries/start_command_eligible.sql", instance.id)
                            .fetch_one(&mut *tx)
                            .await?;
                    let authorized = instance_state::owner_authorized(&mut tx, instance.id).await?;
                    if !eligible || !authorized {
                        if command.attempts == 0 && instance.state == "reserved" {
                            instance_state::cancel(&mut tx, instance.id).await?;
                            instance_state::change(
                                &mut tx,
                                &instance,
                                Some(&node),
                                Transition::Failed,
                            )
                            .await?;
                        } else if !authorized {
                            // A previously delivered Start may be running. Revoke it with a
                            // higher-revision exact Stop; never infer absence from timeout.
                            instance_stop::enqueue(&mut tx, &instance, &node).await?;
                        } else {
                            // Configuration changes and drain close admission, not existing
                            // workloads. Inventory must settle a possibly executed Start.
                            instance_state::cancel(&mut tx, instance.id).await?;
                            instance_state::change(
                                &mut tx,
                                &instance,
                                Some(&node),
                                Transition::Unknown,
                            )
                            .await?;
                            sqlx::query_file!("queries/mark_node_reconciling.sql", node.id)
                                .execute(&mut *tx)
                                .await?;
                        }
                        continue;
                    }
                    if instance.state == "reserved" {
                        instance = instance_state::change(
                            &mut tx,
                            &instance,
                            Some(&node),
                            Transition::Starting,
                        )
                        .await?;
                    } else if instance.state != "starting" {
                        return Err(StoreError::Rejected);
                    }
                    sqlx::query_file_as!(LaunchRow, "queries/instance_launch.sql", instance.id)
                        .fetch_one(&mut *tx)
                        .await?
                        .action(instance.port)?
                }
                "stop" => {
                    if instance.state != "stopping" || instance.desired_state != "stopped" {
                        sqlx::query_file!("queries/cancel_instance_command.sql", command.id)
                            .execute(&mut *tx)
                            .await?;
                        continue;
                    }
                    NodeCommandAction::Stop
                }
                _ => return Err(StoreError::Database(px_pg::DatabaseError::Operation)),
            };
            let lease = Uuid::new_v4();
            let lease_until = sqlx::query_file_scalar!(
                "queries/claim_instance_command.sql",
                command.id,
                instance.revision,
                lease
            )
            .fetch_one(&mut *tx)
            .await?;
            let envelope = NodeCommand {
                id: command.id,
                instance_id: instance.id,
                launch_id: instance.launch_id,
                application_id: instance.application_id,
                deployment_id: instance.deployment_id,
                application_revision: instance.application_revision,
                deployment_revision: instance.deployment_revision,
                instance_revision: instance.revision,
                node_generation: node.generation,
                control_epoch: node.control_epoch,
                endpoint_revision: instance.endpoint_revision,
                lease_id: lease,
                lease_until,
                deadline: command.deadline,
                action,
            };
            tx.commit().await?;
            return Ok(Some(envelope));
        }
        tx.commit().await?;
        Ok(None)
    }
    pub async fn acknowledge_command(
        &self,
        connection: &NodeConnection,
        receipt: &CommandReceipt,
    ) -> Result<ApplicationInstance, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let node = node_lifecycle::authorize(&mut tx, connection).await?;
        let command = sqlx::query_file_as!(
            CommandRow,
            "queries/lock_instance_command.sql",
            receipt.command_id
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        if command.node_id != node.id
            || command.node_generation != node.generation
            || command.control_epoch != node.control_epoch
            || command.instance_id != receipt.instance_id
            || command.instance_revision != receipt.instance_revision
        {
            return Err(StoreError::Rejected);
        }
        let instance = instance_state::lock(&mut tx, command.instance_id).await?;
        if instance.launch_id != receipt.launch_id || instance.node_id != node.id {
            return Err(StoreError::Rejected);
        }
        if let CommandOutcome::Running { port } = receipt.outcome {
            if command.kind != "start" || i32::from(port) != instance.port {
                return Err(StoreError::Rejected);
            }
        }
        if command.state == "completed" {
            if command.completed_lease_id != Some(receipt.lease_id)
                || command.outcome.as_deref() != Some(receipt.outcome.name())
            {
                return Err(StoreError::Rejected);
            }
            let result = instance.view()?;
            tx.commit().await?;
            return Ok(result);
        }
        if command.state != "claimed"
            || command.lease_id != Some(receipt.lease_id)
            || command.lease_until.is_none()
            || command.expired
            || instance.revision != receipt.instance_revision
            || instance.ended_at.is_some()
        {
            return Err(StoreError::Rejected);
        }
        let transition = match receipt.outcome {
            CommandOutcome::Running { .. }
                if instance.state == "starting" && instance.desired_state == "running" =>
            {
                Transition::Running
            }
            CommandOutcome::Running { .. } => return Err(StoreError::Rejected),
            CommandOutcome::Absent if command.kind == "start" => Transition::Failed,
            CommandOutcome::Absent if command.kind == "stop" => Transition::Stopped,
            CommandOutcome::Absent => return Err(StoreError::Rejected),
            CommandOutcome::Unknown => Transition::Unknown,
        };
        let mut next = instance_state::change(&mut tx, &instance, Some(&node), transition).await?;
        sqlx::query_file!(
            "queries/finish_instance_command.sql",
            command.id,
            receipt.outcome.name(),
            receipt.lease_id
        )
        .execute(&mut *tx)
        .await?;
        if matches!(receipt.outcome, CommandOutcome::Running { .. })
            && !instance_state::owner_authorized(&mut tx, instance.id).await?
        {
            // A valid execution receipt is not a fresh authorization. Complete it and
            // persist the compensating exact Stop in the same transaction.
            next = instance_stop::enqueue(&mut tx, &next, &node).await?;
        }
        if receipt.outcome == CommandOutcome::Unknown {
            instance_state::cancel(&mut tx, instance.id).await?;
            sqlx::query_file!("queries/mark_node_reconciling.sql", node.id)
                .execute(&mut *tx)
                .await?;
        }
        let result = next.view()?;
        tx.commit().await?;
        Ok(result)
    }
}
