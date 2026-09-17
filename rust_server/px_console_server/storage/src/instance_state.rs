use crate::{
    instance_model::InstanceRow, node_lifecycle::NodeAuthority, InstanceStore, StoreError,
};
use sqlx::PgConnection;
use uuid::Uuid;

pub(crate) enum Transition {
    Starting,
    ReconciledStarting,
    ReconciledRunning,
    Running,
    StopRequested,
    StopUnknown,
    Unknown,
    Stopped,
    Failed,
}
impl Transition {
    fn fields(&self) -> (&'static str, &'static str) {
        match self {
            Self::ReconciledStarting => ("starting", "reconciled"),
            Self::ReconciledRunning => ("running", "reconciled"),
            Self::Starting => ("starting", "starting"),
            Self::Running => ("running", "running"),
            Self::StopRequested => ("stopping", "stop_requested"),
            Self::StopUnknown => ("reconcile_required", "stop_requested"),
            Self::Unknown => ("reconcile_required", "reconcile_required"),
            Self::Stopped => ("stopped", "stopped"),
            Self::Failed => ("failed", "failed"),
        }
    }
}
pub(crate) async fn lock(
    connection: &mut PgConnection,
    id: Uuid,
) -> Result<InstanceRow, StoreError> {
    sqlx::query_file_as!(InstanceRow, "queries/lock_instance_state.sql", id)
        .fetch_optional(connection)
        .await?
        .ok_or(StoreError::Rejected)
}
pub(crate) async fn owner_authorized(
    connection: &mut PgConnection,
    id: Uuid,
) -> Result<bool, StoreError> {
    Ok(
        sqlx::query_file_scalar!("queries/instance_owner_authorized.sql", id)
            .fetch_one(connection)
            .await?,
    )
}
pub(crate) async fn change(
    connection: &mut PgConnection,
    previous: &InstanceRow,
    node: Option<&NodeAuthority>,
    transition: Transition,
) -> Result<InstanceRow, StoreError> {
    if previous.ended_at.is_some() {
        return Err(StoreError::Rejected);
    }
    let desired = if matches!(
        transition,
        Transition::StopRequested
            | Transition::StopUnknown
            | Transition::Stopped
            | Transition::Failed
    ) {
        "stopped"
    } else {
        previous.desired_state.as_str()
    };
    let (state, event) = transition.fields();
    let generation = node.map_or(previous.node_generation, |node| node.generation);
    let epoch = node.map_or(previous.control_epoch, |node| node.control_epoch);
    let endpoint = node.map_or(previous.endpoint_revision, |node| node.endpoint_revision);
    let next = sqlx::query_file_as!(
        InstanceRow,
        "queries/move_instance_state.sql",
        previous.id,
        state,
        desired,
        generation,
        epoch,
        endpoint
    )
    .fetch_one(&mut *connection)
    .await?;
    InstanceStore::event(connection, &next, event).await?;
    Ok(next)
}
pub(crate) async fn cancel(
    connection: &mut PgConnection,
    instance: Uuid,
) -> Result<(), StoreError> {
    sqlx::query_file!("queries/cancel_instance_commands.sql", instance)
        .execute(connection)
        .await?;
    Ok(())
}
