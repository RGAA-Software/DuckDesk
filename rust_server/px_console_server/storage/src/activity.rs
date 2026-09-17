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
        let hash = request.digest()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        if let Some(previous) = sqlx::query_file_as!(
            ChannelRow,
            "queries/find_connection_source.sql",
            authority.id,
            request.source_id
        )
        .fetch_optional(&mut *tx)
        .await?
        {
            if previous.source_id != request.source_id
                || previous.request_hash.as_slice() != hash
                || previous.node_generation != authority.generation
                || previous.control_epoch != authority.control_epoch
            {
                return Err(StoreError::Rejected);
            }
            let result = previous.view();
            tx.commit().await?;
            return Ok(result);
        }
        let session = ResourceSessionStore::lock(&mut tx, request.session_id).await?;
        if session.node_id != authority.id
            || session.node_generation != authority.generation
            || session.control_epoch != authority.control_epoch
        {
            return Err(StoreError::Rejected);
        }
        Self::live(&mut tx, request.session_id, request.kind.name()).await?;
        let count = sqlx::query_file_scalar!(
            "queries/connection_channel_capacity.sql",
            request.session_id
        )
        .fetch_one(&mut *tx)
        .await?;
        if count >= 16 {
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
            hash.as_slice()
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, &row).await?;
        let result = row.view();
        tx.commit().await?;
        Ok(result)
    }
    pub async fn report_channel(
        &self,
        node: &NodeConnection,
        id: Uuid,
        progress: &ChannelProgress,
    ) -> Result<ChannelRecord, StoreError> {
        let checked = progress.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        let previous =
            sqlx::query_file_as!(ChannelRow, "queries/lock_connection_observation.sql", id)
                .fetch_optional(&mut *tx)
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
            tx.commit().await?;
            return Ok(result);
        }
        if previous.state != "active"
            || checked.sequence <= previous.sequence
            || checked.sent < previous.sent_bytes
            || checked.received < previous.received_bytes
            || checked.elapsed < previous.elapsed_ms
        {
            return Err(StoreError::Rejected);
        }
        if checked.state == "active" {
            Self::live(&mut tx, previous.session_id, &previous.kind).await?;
        }
        let row = sqlx::query_file_as!(
            ChannelRow,
            "queries/update_connection_observation.sql",
            id,
            checked.state,
            checked.reason,
            checked.sent,
            checked.received,
            checked.elapsed,
            checked.sequence,
            checked.hash.as_slice()
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, &row).await?;
        let result = row.view();
        tx.commit().await?;
        Ok(result)
    }
    async fn live(c: &mut PgConnection, session: Uuid, kind: &str) -> Result<(), StoreError> {
        if !sqlx::query_file_scalar!("queries/connection_session_ready.sql", session)
            .fetch_one(&mut *c)
            .await?
        {
            return Err(StoreError::Rejected);
        }
        let session = ResourceSessionStore::lock(c, session).await?;
        let endpoint = ResourceSessionStore::live_endpoint(c, &session).await?;
        if (endpoint.transport == "rdp" && !matches!(kind, "rdp" | "control"))
            || (endpoint.transport != "rdp" && kind == ChannelKind::Rdp.name())
            || (kind == "file" && session.access_role != "controller")
        {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
    async fn event(c: &mut PgConnection, row: &ChannelRow) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/connection_observation_event.sql",
            Uuid::new_v4(),
            row.id,
            row.revision,
            row.sequence,
            row.state,
            row.sent_bytes,
            row.received_bytes,
            row.elapsed_ms
        )
        .execute(c)
        .await?;
        Ok(())
    }
}
pub(crate) async fn invalidate(
    c: &mut PgConnection,
    node: Option<Uuid>,
    session: Option<Uuid>,
) -> Result<(), StoreError> {
    sqlx::query_file!(
        "queries/invalidate_connection_observations.sql",
        node,
        session
    )
    .execute(c)
    .await?;
    Ok(())
}
