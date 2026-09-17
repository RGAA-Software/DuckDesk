use crate::{
    control,
    instance_model::InstanceRow,
    instance_state::{self, Transition},
    node_lifecycle::NodeAuthority,
    ApplicationInstance, ClientType, InstanceStore, ResourceCredential, RuntimeEpoch, StoreError,
    TokenDigest,
};
use sqlx::PgConnection;
use uuid::Uuid;

impl InstanceStore {
    pub async fn stop(
        &self,
        credential: ResourceCredential<'_>,
        client: ClientType,
        epoch: RuntimeEpoch,
        id: Uuid,
        revision: i64,
    ) -> Result<ApplicationInstance, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        current_epoch(&mut tx, epoch).await?;
        let owner = Self::authorize(&mut tx, credential, client).await?.owner;
        let record = instance_state::lock(&mut tx, id).await?;
        if owner.columns() != (record.owner_user, record.owner_guest) {
            return Err(StoreError::Rejected);
        }
        let result = stop_requested(&mut tx, record, epoch, revision)
            .await?
            .view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn stop_managed(
        &self,
        admin: &TokenDigest,
        epoch: RuntimeEpoch,
        id: Uuid,
        revision: i64,
    ) -> Result<ApplicationInstance, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        current_epoch(&mut tx, epoch).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let record = instance_state::lock(&mut tx, id).await?;
        let result = stop_requested(&mut tx, record, epoch, revision)
            .await?
            .view()?;
        sqlx::query_file!(
            "queries/instance_admin_stop.sql",
            Uuid::new_v4(),
            id,
            actor,
            revision,
            result.revision
        )
        .execute(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
}
async fn current_epoch(
    connection: &mut PgConnection,
    epoch: RuntimeEpoch,
) -> Result<(), StoreError> {
    if !sqlx::query_file_scalar!("queries/runtime_epoch_matches.sql", epoch.0)
        .fetch_one(connection)
        .await?
    {
        return Err(StoreError::Rejected);
    }
    Ok(())
}
async fn stop_requested(
    connection: &mut PgConnection,
    record: InstanceRow,
    epoch: RuntimeEpoch,
    revision: i64,
) -> Result<InstanceRow, StoreError> {
    // Repeated stop of a terminal or already-stopping instance is idempotent and never
    // downgrades Stopped to Failed or touches a reused port/process.
    if record.ended_at.is_some() || record.desired_state == "stopped" {
        return Ok(record);
    }
    if revision < 1 || revision != record.revision {
        return Err(StoreError::Rejected);
    }
    let dispatched = sqlx::query_file_scalar!("queries/instance_was_dispatched.sql", record.id)
        .fetch_one(&mut *connection)
        .await?;
    if record.state == "reserved" && !dispatched {
        instance_state::cancel(connection, record.id).await?;
        return instance_state::change(connection, &record, None, Transition::Stopped).await;
    }
    let node = sqlx::query_file_as!(
        NodeAuthority,
        "queries/stop_target_node.sql",
        record.node_id,
        epoch.0
    )
    .fetch_optional(&mut *connection)
    .await?;
    match node {
        Some(node) => enqueue(connection, &record, &node).await,
        None => {
            instance_state::cancel(connection, record.id).await?;
            instance_state::change(connection, &record, None, Transition::StopUnknown).await
        }
    }
}
pub(crate) async fn enqueue(
    connection: &mut PgConnection,
    record: &InstanceRow,
    node: &NodeAuthority,
) -> Result<InstanceRow, StoreError> {
    instance_state::cancel(connection, record.id).await?;
    let next =
        instance_state::change(connection, record, Some(node), Transition::StopRequested).await?;
    InstanceStore::command(connection, &next, "stop").await?;
    Ok(next)
}
