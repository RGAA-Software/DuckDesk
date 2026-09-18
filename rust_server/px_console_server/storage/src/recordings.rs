use crate::{
    control, node_lifecycle, recording_model::RecordingRow, resource_policy::resource_user,
    ClientType, NodeConnection, RecordingProfile, RecordingReport, ResourceSessionStore,
    StoreError, TokenDigest,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;
#[derive(Clone)]
pub struct RecordingStore {
    pub(crate) pool: PgPool,
}
impl RecordingStore {
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
    pub async fn report(
        &self,
        node: &NodeConnection,
        request: &RecordingReport,
    ) -> Result<RecordingProfile, StoreError> {
        let checked = request.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        let existing = sqlx::query_file_as!(
            RecordingRow,
            "queries/recording_by_source.sql",
            authority.id,
            request.source_id
        )
        .fetch_optional(&mut *tx)
        .await?;
        let row = if let Some(previous) = existing {
            if previous.metadata_hash.as_slice() != checked.hash {
                return Err(StoreError::Rejected);
            }
            if previous.node_generation == authority.generation
                && previous.control_epoch == authority.control_epoch
            {
                if checked.sequence == previous.source_sequence
                    && request.present == previous.reported_present
                {
                    let result = previous.view();
                    tx.commit().await?;
                    return Ok(result);
                }
                if checked.sequence <= previous.source_sequence {
                    return Err(StoreError::Rejected);
                }
            }
            sqlx::query_file_as!(
                RecordingRow,
                "queries/observe_recording.sql",
                previous.id,
                request.present,
                authority.generation,
                authority.control_epoch,
                checked.sequence
            )
            .fetch_one(&mut *tx)
            .await?
        } else {
            if !request.present {
                return Err(StoreError::Rejected);
            }
            if let Some(session) = request.session_id {
                let session = ResourceSessionStore::lock(&mut tx, session).await?;
                if session.node_id != authority.id {
                    return Err(StoreError::Rejected);
                }
            }
            sqlx::query_file_as!(
                RecordingRow,
                "queries/create_recording.sql",
                Uuid::new_v4(),
                authority.id,
                request.source_id,
                request.session_id,
                request.file_name,
                checked.size,
                checked.modified,
                request.codec.name(),
                checked.hash.as_slice(),
                authority.generation,
                authority.control_epoch,
                checked.sequence,
                request.source_sha256.as_slice()
            )
            .fetch_one(&mut *tx)
            .await?
        };
        Self::event(&mut tx, &row).await?;
        let result = row.view();
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_managed(
        &self,
        token: &TokenDigest,
        node: Option<Uuid>,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<RecordingProfile>, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, token, false).await?;
        let result = Self::list(&mut tx, node, after, limit).await?;
        tx.commit().await?;
        Ok(result)
    }
    /// A user sees only recordings attributed to resource sessions that they own.
    /// This never authorizes browsing unrelated recordings from the execution host.
    pub async fn list_owned(
        &self,
        token: &TokenDigest,
        client: ClientType,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<RecordingProfile>, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let user = resource_user(&mut tx, token, client).await?;
        let result = sqlx::query_file_as!(
            RecordingProfile,
            "queries/list_owned_recordings.sql",
            user,
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
    async fn list(
        connection: &mut PgConnection,
        node: Option<Uuid>,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<RecordingProfile>, StoreError> {
        Ok(sqlx::query_file_as!(
            RecordingProfile,
            "queries/list_recordings.sql",
            node,
            after,
            i64::from(limit)
        )
        .fetch_all(connection)
        .await?)
    }
    async fn event(connection: &mut PgConnection, row: &RecordingRow) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/recording_event.sql",
            Uuid::new_v4(),
            row.id,
            row.revision,
            row.node_generation,
            row.control_epoch,
            row.source_sequence,
            row.reported_present
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
fn valid_page(limit: u32) -> Result<(), StoreError> {
    if !(1..=100).contains(&limit) {
        return Err(StoreError::InvalidInput);
    }
    Ok(())
}
