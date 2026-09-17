use crate::{
    cache_model::{CacheBlob, CacheEntry, CacheOrigin, CacheSource},
    control,
    resource_policy::resource_user,
    CacheCredential, CacheProfile, CacheRuntime, DeviceStore, RecordingCacheStore, StoreError,
};
use sqlx::PgConnection;
use uuid::Uuid;

impl RecordingCacheStore {
    pub(crate) async fn current(
        connection: &mut PgConnection,
        run: &CacheRuntime,
    ) -> Result<(), StoreError> {
        if !sqlx::query_file_scalar!(
            "queries/cache_runtime_matches.sql",
            run.id,
            run.root.id(),
            run.epoch.0
        )
        .fetch_one(connection)
        .await?
        {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
    pub(crate) async fn source(
        connection: &mut PgConnection,
        run: &CacheRuntime,
        id: Uuid,
    ) -> Result<CacheSource, StoreError> {
        sqlx::query_file_as!(CacheSource, "queries/cache_source.sql", id, run.epoch.0)
            .fetch_optional(connection)
            .await?
            .ok_or(StoreError::Rejected)
    }
    pub(crate) async fn entry(
        connection: &mut PgConnection,
        id: Uuid,
    ) -> Result<Option<CacheEntry>, StoreError> {
        Ok(
            sqlx::query_file_as!(CacheEntry, "queries/lock_recording_cache.sql", id)
                .fetch_optional(connection)
                .await?,
        )
    }
    pub(crate) async fn blob(
        connection: &mut PgConnection,
        id: Uuid,
    ) -> Result<CacheBlob, StoreError> {
        sqlx::query_file_as!(CacheBlob, "queries/lock_cache_blob.sql", id)
            .fetch_optional(connection)
            .await?
            .ok_or(StoreError::Rejected)
    }
    pub(crate) async fn credential(
        connection: &mut PgConnection,
        credential: &CacheCredential<'_>,
        source: &CacheSource,
    ) -> Result<(CacheOrigin, &'static str), StoreError> {
        let (token, scope) = match credential {
            CacheCredential::Managed(token) => {
                control::authorize(connection, token, false).await?;
                (*token, "managed")
            }
            CacheCredential::DeviceUser { token, client } => {
                let user = resource_user(connection, token, *client).await?;
                if DeviceStore::visible(connection, user, None, 1, Some(source.device_id))
                    .await?
                    .is_empty()
                {
                    return Err(StoreError::Rejected);
                }
                (*token, "device")
            }
        };
        let origin = sqlx::query_file_as!(
            CacheOrigin,
            "queries/cache_login_origin.sql",
            token.0.as_slice()
        )
        .fetch_one(connection)
        .await?;
        Ok((origin, scope))
    }
    pub(crate) async fn origin(
        connection: &mut PgConnection,
        blob: Uuid,
    ) -> Result<bool, StoreError> {
        Ok(
            sqlx::query_file_scalar!("queries/cache_origin_authorized.sql", blob)
                .fetch_one(connection)
                .await?,
        )
    }
    pub(crate) async fn event(
        connection: &mut PgConnection,
        run: &CacheRuntime,
        entry: &CacheEntry,
        blob: Option<Uuid>,
        actor: Option<Uuid>,
        kind: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/cache_event.sql",
            Uuid::new_v4(),
            entry.recording_id,
            entry.revision,
            run.id,
            blob,
            actor,
            kind
        )
        .execute(connection)
        .await?;
        Ok(())
    }
    pub(crate) async fn moved(
        connection: &mut PgConnection,
        id: Uuid,
        blob: Option<Uuid>,
    ) -> Result<CacheEntry, StoreError> {
        Ok(
            sqlx::query_file_as!(CacheEntry, "queries/move_recording_cache.sql", id, blob)
                .fetch_one(connection)
                .await?,
        )
    }
    pub(crate) async fn abandon_locked(
        connection: &mut PgConnection,
        run: &CacheRuntime,
        entry: &CacheEntry,
        blob: &CacheBlob,
    ) -> Result<(), StoreError> {
        if blob.state != "fetching" {
            return Err(StoreError::Rejected);
        }
        sqlx::query_file!("queries/abandon_cache_blob.sql", blob.id)
            .execute(&mut *connection)
            .await?;
        if entry.active_blob_id == Some(blob.id) {
            let changed = Self::moved(connection, entry.recording_id, None).await?;
            Self::event(connection, run, &changed, Some(blob.id), None, "abandoned").await?;
        }
        Ok(())
    }
    pub(crate) fn profile(
        run: &CacheRuntime,
        entry: &CacheEntry,
        source: &CacheSource,
        blob: Option<&CacheBlob>,
    ) -> CacheProfile {
        let state = match blob {
            None => "missing",
            Some(cache_blob)
                if cache_blob.state == "published" && cache_blob.verified_run == Some(run.id) =>
            {
                "ready"
            }
            Some(cache_blob) if cache_blob.state == "published" => "verifying",
            Some(cache_blob)
                if cache_blob.state == "fetching"
                    && cache_blob.run_id == run.id
                    && !cache_blob.expired =>
            {
                "fetching"
            }
            _ => "retry_required",
        };
        CacheProfile {
            recording_id: entry.recording_id,
            state: state.into(),
            pinned: entry.pinned,
            revision: entry.revision,
            size_bytes: source.size_bytes,
            received_bytes: blob.map_or(0, |cache_blob| cache_blob.received_bytes),
            updated_at: entry.updated_at,
        }
    }
}
