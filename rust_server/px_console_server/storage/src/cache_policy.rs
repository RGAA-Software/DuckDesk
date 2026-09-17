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
        c: &mut PgConnection,
        run: &CacheRuntime,
    ) -> Result<(), StoreError> {
        if !sqlx::query_file_scalar!(
            "queries/cache_runtime_matches.sql",
            run.id,
            run.root.id(),
            run.epoch.0
        )
        .fetch_one(c)
        .await?
        {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
    pub(crate) async fn source(
        c: &mut PgConnection,
        run: &CacheRuntime,
        id: Uuid,
    ) -> Result<CacheSource, StoreError> {
        sqlx::query_file_as!(CacheSource, "queries/cache_source.sql", id, run.epoch.0)
            .fetch_optional(c)
            .await?
            .ok_or(StoreError::Rejected)
    }
    pub(crate) async fn entry(
        c: &mut PgConnection,
        id: Uuid,
    ) -> Result<Option<CacheEntry>, StoreError> {
        Ok(
            sqlx::query_file_as!(CacheEntry, "queries/lock_recording_cache.sql", id)
                .fetch_optional(c)
                .await?,
        )
    }
    pub(crate) async fn blob(c: &mut PgConnection, id: Uuid) -> Result<CacheBlob, StoreError> {
        sqlx::query_file_as!(CacheBlob, "queries/lock_cache_blob.sql", id)
            .fetch_optional(c)
            .await?
            .ok_or(StoreError::Rejected)
    }
    pub(crate) async fn credential(
        c: &mut PgConnection,
        credential: &CacheCredential<'_>,
        source: &CacheSource,
    ) -> Result<(CacheOrigin, &'static str), StoreError> {
        let (token, scope) = match credential {
            CacheCredential::Managed(token) => {
                control::authorize(c, token, false).await?;
                (*token, "managed")
            }
            CacheCredential::DeviceUser { token, client } => {
                let user = resource_user(c, token, *client).await?;
                if DeviceStore::visible(c, user, None, 1, Some(source.device_id))
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
        .fetch_one(c)
        .await?;
        Ok((origin, scope))
    }
    pub(crate) async fn origin(c: &mut PgConnection, blob: Uuid) -> Result<bool, StoreError> {
        Ok(
            sqlx::query_file_scalar!("queries/cache_origin_authorized.sql", blob)
                .fetch_one(c)
                .await?,
        )
    }
    pub(crate) async fn event(
        c: &mut PgConnection,
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
        .execute(c)
        .await?;
        Ok(())
    }
    pub(crate) async fn moved(
        c: &mut PgConnection,
        id: Uuid,
        blob: Option<Uuid>,
    ) -> Result<CacheEntry, StoreError> {
        Ok(
            sqlx::query_file_as!(CacheEntry, "queries/move_recording_cache.sql", id, blob)
                .fetch_one(c)
                .await?,
        )
    }
    pub(crate) async fn abandon_locked(
        c: &mut PgConnection,
        run: &CacheRuntime,
        entry: &CacheEntry,
        blob: &CacheBlob,
    ) -> Result<(), StoreError> {
        if blob.state != "fetching" {
            return Err(StoreError::Rejected);
        }
        sqlx::query_file!("queries/abandon_cache_blob.sql", blob.id)
            .execute(&mut *c)
            .await?;
        if entry.active_blob_id == Some(blob.id) {
            let changed = Self::moved(c, entry.recording_id, None).await?;
            Self::event(c, run, &changed, Some(blob.id), None, "abandoned").await?;
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
            Some(b) if b.state == "published" && b.verified_run == Some(run.id) => "ready",
            Some(b) if b.state == "published" => "verifying",
            Some(b) if b.state == "fetching" && b.run_id == run.id && !b.expired => "fetching",
            _ => "retry_required",
        };
        CacheProfile {
            recording_id: entry.recording_id,
            state: state.into(),
            pinned: entry.pinned,
            revision: entry.revision,
            size_bytes: source.size_bytes,
            received_bytes: blob.map_or(0, |b| b.received_bytes),
            updated_at: entry.updated_at,
        }
    }
}
