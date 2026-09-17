use crate::StoreError;
use sqlx::PgConnection;
use uuid::Uuid;

#[derive(sqlx::FromRow)]
pub(crate) struct NodeAuthority {
    pub id: Uuid,
    pub generation: i64,
    pub control_epoch: i64,
    pub endpoint_revision: i64,
}
pub(crate) async fn authorize(
    connection: &mut PgConnection,
    node: &crate::NodeConnection,
) -> Result<NodeAuthority, StoreError> {
    sqlx::query_file_as!(
        NodeAuthority,
        "queries/authorize_node_connection.sql",
        node.id,
        node.key.0.as_slice(),
        node.generation,
        node.epoch.0
    )
    .fetch_optional(connection)
    .await?
    .ok_or(StoreError::Rejected)
}

// Caller holds the control gate and has already changed the node's connection identity.
// No process cleanup and no resource release: previously sent work may still be running.
pub(crate) async fn invalidate(
    connection: &mut PgConnection,
    node: Option<Uuid>,
) -> Result<(), StoreError> {
    sqlx::query_file!("queries/invalidate_instance_control.sql", node)
        .execute(&mut *connection)
        .await?;
    sqlx::query_file!("queries/invalidate_resource_sessions.sql", node)
        .execute(&mut *connection)
        .await?;
    crate::file_transfers::invalidate(connection, node, None).await?;
    crate::activity::invalidate(connection, node, None).await?;
    sqlx::query_file!("queries/cancel_node_instance_commands.sql", node)
        .execute(connection)
        .await?;
    Ok(())
}
