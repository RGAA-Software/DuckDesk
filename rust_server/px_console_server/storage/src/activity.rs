use crate::{
    activity_model::ChannelRow, control, node_lifecycle, ChannelKind, ChannelProgress,
    ChannelRecord, NodeConnection, OpenChannel, ResourceSessionStore, StoreError,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;
#[derive(Clone)]
pub struct ActivityStore {
    pub(crate) pool: PgPool,
}
impl ActivityStore {
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
    /// Observation only. Actual network admission remains subject to the frontend lease.
    pub async fn open_channel(
        &self,
        node: &NodeConnection,
        request: &OpenChannel,
    ) -> Result<ChannelRecord, StoreError> {
        let request_hash = request.digest()?;
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let authority = node_lifecycle::authorize(&mut transaction, node).await?;
        if let Some(previous) = sqlx::query_file_as!(
            ChannelRow,
            "queries/find_connection_source.sql",
            authority.id,
            request.source_id
        )
        .fetch_optional(&mut *transaction)
        .await?
        {
            if previous.source_id != request.source_id
                || previous.request_hash.as_slice() != request_hash
                || previous.node_generation != authority.generation
                || previous.control_epoch != authority.control_epoch
            {
                return Err(StoreError::Rejected);
            }
            let result = previous.view();
            transaction.commit().await?;
            return Ok(result);
        }
        let session = ResourceSessionStore::lock(&mut transaction, request.session_id).await?;
        if session.node_id != authority.id
            || session.node_generation != authority.generation
            || session.control_epoch != authority.control_epoch
        {
            return Err(StoreError::Rejected);
        }
        Self::live(&mut transaction, request.session_id, request.kind.name()).await?;
        let active_channel_count = sqlx::query_file_scalar!(
            "queries/connection_channel_capacity.sql",
            request.session_id
        )
        .fetch_one(&mut *transaction)
        .await?;
        if active_channel_count >= 16 {
            return Err(StoreError::NoCapacity);
        }
        let row = sqlx::query_file_as!(
            ChannelRow,
            "queries/create_connection_observation.sql",
            Uuid::new_v4(),
            request.source_id,
            request.session_id,
            authority.id,
            authority.generation,
            authority.control_epoch,
            request.kind.name(),
            request_hash.as_slice()
        )
        .fetch_one(&mut *transaction)
        .await?;
        Self::event(&mut transaction, &row).await?;
        let result = row.view();
        transaction.commit().await?;
        Ok(result)
    }
    pub async fn report_channel(
        &self,
        node: &NodeConnection,
        channel_id: Uuid,
        progress: &ChannelProgress,
    ) -> Result<ChannelRecord, StoreError> {
        let checked = progress.validate()?;
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let authority = node_lifecycle::authorize(&mut transaction, node).await?;
        let previous = sqlx::query_file_as!(
            ChannelRow,
            "queries/lock_connection_observation.sql",
            channel_id
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        if previous.node_id != authority.id
            || previous.node_generation != authority.generation
            || previous.control_epoch != authority.control_epoch
        {
            return Err(StoreError::Rejected);
        }
        if checked.sequence == previous.sequence
            && previous.report_hash.as_deref() == Some(checked.hash.as_slice())
        {
            let result = previous.view();
            transaction.commit().await?;
            return Ok(result);
        }
        let terminal_after_frontend_close = previous.state == "unknown"
            && previous.reason.as_deref() == Some("frontend_closed")
            && checked.state != "active";
        if (previous.state != "active" && !terminal_after_frontend_close)
            || checked.sequence <= previous.sequence
            || checked.sent < previous.sent_bytes
            || checked.received < previous.received_bytes
            || checked.elapsed < previous.elapsed_ms
        {
            return Err(StoreError::Rejected);
        }
        if checked.state == "active" {
            Self::live(&mut transaction, previous.session_id, &previous.kind).await?;
        }
        let row = sqlx::query_file_as!(
            ChannelRow,
            "queries/update_connection_observation.sql",
            channel_id,
            checked.state,
            checked.reason,
            checked.sent,
            checked.received,
            checked.elapsed,
            checked.sequence,
            checked.hash.as_slice()
        )
        .fetch_one(&mut *transaction)
        .await?;
        Self::event(&mut transaction, &row).await?;
        let result = row.view();
        transaction.commit().await?;
        Ok(result)
    }
    async fn live(
        connection: &mut PgConnection,
        session_id: Uuid,
        channel_kind: &str,
    ) -> Result<(), StoreError> {
        if !sqlx::query_file_scalar!("queries/connection_session_ready.sql", session_id)
            .fetch_one(&mut *connection)
            .await?
        {
            return Err(StoreError::Rejected);
        }
        let session = ResourceSessionStore::lock(connection, session_id).await?;
        let endpoint = ResourceSessionStore::live_endpoint(connection, &session).await?;
        if (endpoint.transport == "rdp" && !matches!(channel_kind, "rdp" | "control"))
            || (endpoint.transport != "rdp" && channel_kind == ChannelKind::Rdp.name())
            || (channel_kind == "file" && session.access_role != "controller")
        {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
    async fn event(connection: &mut PgConnection, channel: &ChannelRow) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/connection_observation_event.sql",
            Uuid::new_v4(),
            channel.id,
            channel.revision,
            channel.sequence,
            channel.state,
            channel.sent_bytes,
            channel.received_bytes,
            channel.elapsed_ms
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
pub(crate) async fn invalidate(
    connection: &mut PgConnection,
    node_id: Option<Uuid>,
    session_id: Option<Uuid>,
) -> Result<(), StoreError> {
    sqlx::query_file!(
        "queries/invalidate_connection_observations.sql",
        node_id,
        session_id
    )
    .execute(connection)
    .await?;
    Ok(())
}
