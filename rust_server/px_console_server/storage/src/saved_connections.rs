use crate::{
    control, resource_policy::resource_user, saved_connection_model::SavedConnectionRow,
    ApplicationStore, ClientType, CreateSavedConnection, DeviceStore, SavedConnection,
    SavedConnectionSettings, SavedConnectionTarget, StoreError, TokenDigest,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct SavedConnectionStore {
    pub(crate) pool: PgPool,
}
impl SavedConnectionStore {
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

    pub async fn create(
        &self,
        token: &TokenDigest,
        client: ClientType,
        request: &CreateSavedConnection,
    ) -> Result<SavedConnection, StoreError> {
        let hash = request.digest()?;
        let (device, application) = request.target.ids()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let owner = resource_user(&mut tx, token, client).await?;
        let previous = sqlx::query_file_as!(
            SavedConnectionRow,
            "queries/saved_connection_by_request.sql",
            owner,
            client.name(),
            request.request_id
        )
        .fetch_optional(&mut *tx)
        .await?;
        if let Some(row) = previous {
            if row.request_id != request.request_id
                || row.request_hash.as_slice() != hash
                || row.deleted_at.is_some()
            {
                return Err(StoreError::Rejected);
            }
            // Retry returns the existing current preference, never creates a new grant.
            let view = row.view()?;
            tx.commit().await?;
            return Ok(view);
        }
        Self::authorize_target(&mut tx, owner, &request.target).await?;
        let count =
            sqlx::query_file_scalar!("queries/count_saved_connections.sql", owner, client.name())
                .fetch_one(&mut *tx)
                .await?;
        if count >= 128 {
            return Err(StoreError::NoCapacity);
        }
        let settings = &request.settings;
        let row = sqlx::query_file_as!(
            SavedConnectionRow,
            "queries/create_saved_connection.sql",
            Uuid::new_v4(),
            owner,
            client.name(),
            request.request_id,
            hash.as_slice(),
            settings.name,
            device,
            application,
            i64::from(settings.video_bitrate_bps),
            settings.video_fps as i32,
            settings.audio_enabled,
            settings.clipboard_enabled,
            settings.view_only,
            settings.maximize,
            settings.split_windows,
            settings.prefer_peer_to_peer,
            settings.audio_capture.name(),
            settings.background_rgb as i32
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, &row, "created").await?;
        let result = row.view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn get(
        &self,
        token: &TokenDigest,
        client: ClientType,
        id: Uuid,
    ) -> Result<SavedConnection, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let owner = resource_user(&mut tx, token, client).await?;
        let result = Self::lock(&mut tx, owner, client, id).await?.view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list(
        &self,
        token: &TokenDigest,
        client: ClientType,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<SavedConnection>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let owner = resource_user(&mut tx, token, client).await?;
        let rows = sqlx::query_file_as!(
            SavedConnectionRow,
            "queries/list_saved_connections.sql",
            owner,
            client.name(),
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let result = rows
            .iter()
            .map(SavedConnectionRow::view)
            .collect::<Result<Vec<_>, _>>()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn update(
        &self,
        token: &TokenDigest,
        client: ClientType,
        id: Uuid,
        expected_revision: i64,
        settings: &SavedConnectionSettings,
    ) -> Result<SavedConnection, StoreError> {
        settings.validate()?;
        if expected_revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let owner = resource_user(&mut tx, token, client).await?;
        let previous = Self::lock(&mut tx, owner, client, id).await?;
        if previous.revision != expected_revision || previous.deleted_at.is_some() {
            return Err(StoreError::Rejected);
        }
        Self::authorize_target(&mut tx, owner, &previous.target()?).await?;
        let row = sqlx::query_file_as!(
            SavedConnectionRow,
            "queries/update_saved_connection.sql",
            id,
            settings.name,
            i64::from(settings.video_bitrate_bps),
            settings.video_fps as i32,
            settings.audio_enabled,
            settings.clipboard_enabled,
            settings.view_only,
            settings.maximize,
            settings.split_windows,
            settings.prefer_peer_to_peer,
            settings.audio_capture.name(),
            settings.background_rgb as i32
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, &row, "updated").await?;
        let result = row.view()?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn delete(
        &self,
        token: &TokenDigest,
        client: ClientType,
        id: Uuid,
        expected_revision: i64,
    ) -> Result<SavedConnection, StoreError> {
        if expected_revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let owner = resource_user(&mut tx, token, client).await?;
        let previous = Self::lock(&mut tx, owner, client, id).await?;
        if previous.deleted_at.is_some() {
            if expected_revision.checked_add(1) != Some(previous.revision) {
                return Err(StoreError::Rejected);
            }
            let result = previous.view()?;
            tx.commit().await?;
            return Ok(result);
        }
        if previous.revision != expected_revision {
            return Err(StoreError::Rejected);
        }
        let row = sqlx::query_file_as!(
            SavedConnectionRow,
            "queries/delete_saved_connection.sql",
            id
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::event(&mut tx, &row, "deleted").await?;
        let result = row.view()?;
        tx.commit().await?;
        Ok(result)
    }
    async fn lock(
        connection: &mut PgConnection,
        owner: Uuid,
        client: ClientType,
        id: Uuid,
    ) -> Result<SavedConnectionRow, StoreError> {
        sqlx::query_file_as!(
            SavedConnectionRow,
            "queries/saved_connection_by_id.sql",
            owner,
            client.name(),
            id
        )
        .fetch_optional(connection)
        .await?
        .ok_or(StoreError::Rejected)
    }
    async fn authorize_target(
        connection: &mut PgConnection,
        owner: Uuid,
        target: &SavedConnectionTarget,
    ) -> Result<(), StoreError> {
        let visible = match target {
            SavedConnectionTarget::Desktop { device_id } => {
                !DeviceStore::visible(connection, owner, None, 1, Some(*device_id))
                    .await?
                    .is_empty()
            }
            SavedConnectionTarget::CloudApplication { application_id } => {
                !ApplicationStore::visible(connection, Some(owner), None, 1, Some(*application_id))
                    .await?
                    .is_empty()
            }
        };
        if visible {
            Ok(())
        } else {
            Err(StoreError::Rejected)
        }
    }
    async fn event(
        connection: &mut PgConnection,
        row: &SavedConnectionRow,
        kind: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/saved_connection_event.sql",
            Uuid::new_v4(),
            row.id,
            row.revision,
            kind
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
