use crate::{
    cache_model::{CacheBlob, CacheEntry},
    control, CacheCredential, CacheOptions, CacheProfile, CacheRuntime, RuntimeEpoch, StoreError,
    TokenDigest,
};
use px_private_files::CacheRoot;
use sqlx::PgPool;
use std::sync::Arc;
use uuid::Uuid;

#[derive(Clone)]
pub struct RecordingCacheStore {
    pub(crate) pool: PgPool,
    pub(crate) deployment: Uuid,
}
impl RecordingCacheStore {
    #[cfg(feature = "pg-integration")]
    pub async fn connect(
        config: &px_pg::DatabaseConfig,
        deployment: Uuid,
    ) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
            deployment,
        })
    }
    #[cfg(feature = "pg-integration")]
    pub async fn close(&self) {
        self.pool.close().await;
    }
    /// Composition-root activation only. Never expose this operation as a user API.
    pub async fn begin_runtime(
        &self,
        root: Arc<CacheRoot>,
        epoch: RuntimeEpoch,
        options: CacheOptions,
    ) -> Result<CacheRuntime, StoreError> {
        options.validate()?;
        if root.deployment() != self.deployment {
            return Err(StoreError::Rejected);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        if !sqlx::query_file_scalar!("queries/runtime_epoch_matches.sql", epoch.0)
            .fetch_one(&mut *tx)
            .await?
        {
            return Err(StoreError::Rejected);
        }
        sqlx::query_file!("queries/bind_cache_root.sql", root.id(), self.deployment)
            .execute(&mut *tx)
            .await?;
        if !sqlx::query_file_scalar!("queries/cache_root_matches.sql", root.id(), self.deployment)
            .fetch_one(&mut *tx)
            .await?
        {
            return Err(StoreError::CacheRecoveryRequired);
        }
        let id = sqlx::query_file_scalar!(
            "queries/open_cache_run.sql",
            Uuid::new_v4(),
            root.id(),
            epoch.0,
            options.byte_limit as i64,
            options.maximum_downloads as i32,
            options.ttl_seconds as i32
        )
        .fetch_one(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(CacheRuntime {
            id,
            root,
            epoch,
            options,
        })
    }
    /// Transient fetch. Retention and media delivery require separate explicit policy.
    pub async fn request(
        &self,
        run: &CacheRuntime,
        credential: CacheCredential<'_>,
        recording: Uuid,
    ) -> Result<CacheProfile, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let source = Self::source(&mut tx, run, recording).await?;
        let (origin, scope) = Self::credential(&mut tx, &credential, &source).await?;
        let mut entry = match Self::entry(&mut tx, recording).await? {
            Some(e) => e,
            None => {
                let e = sqlx::query_file_as!(
                    CacheEntry,
                    "queries/create_recording_cache.sql",
                    recording
                )
                .fetch_one(&mut *tx)
                .await?;
                Self::event(&mut tx, run, &e, None, Some(origin.user_id), "created").await?;
                e
            }
        };
        if let Some(id) = entry.active_blob_id {
            let blob = Self::blob(&mut tx, id).await?;
            if blob.state == "published"
                || (blob.state == "fetching"
                    && blob.run_id == run.id
                    && !blob.expired
                    && blob.node_generation == source.node_generation
                    && source.fetchable
                    && Self::origin(&mut tx, id).await?)
            {
                let result = Self::profile(run, &entry, &source, Some(&blob));
                tx.commit().await?;
                return Ok(result);
            }
            if blob.state == "fetching" {
                Self::abandon_locked(&mut tx, run, &entry, &blob).await?;
            }
            entry = Self::entry(&mut tx, recording)
                .await?
                .ok_or(StoreError::Rejected)?;
        }
        if !source.fetchable {
            return Err(StoreError::Rejected);
        }
        let budget = sqlx::query_file!("queries/cache_budget.sql", run.root.id(), run.id)
            .fetch_one(&mut *tx)
            .await?;
        if budget.reserved.saturating_add(source.size_bytes) > run.options.byte_limit as i64
            || budget.active >= i64::from(run.options.maximum_downloads)
        {
            return Err(StoreError::Rejected);
        }
        let blob = sqlx::query_file_as!(
            CacheBlob,
            "queries/create_cache_blob.sql",
            Uuid::new_v4(),
            recording,
            run.root.id(),
            run.id,
            origin.user_id,
            origin.session_id,
            origin.authorization_revision,
            origin.client_type,
            scope,
            Uuid::new_v4()
        )
        .fetch_one(&mut *tx)
        .await?;
        entry = Self::moved(&mut tx, entry.recording_id, Some(blob.id)).await?;
        Self::event(
            &mut tx,
            run,
            &entry,
            Some(blob.id),
            Some(origin.user_id),
            "requested",
        )
        .await?;
        let result = Self::profile(run, &entry, &source, Some(&blob));
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_managed(
        &self,
        run: &CacheRuntime,
        token: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<CacheProfile>, StoreError> {
        crate::cache_work::valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        control::authorize(&mut tx, token, false).await?;
        let result = sqlx::query_file_as!(
            CacheProfile,
            "queries/list_recording_cache.sql",
            run.id,
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
}
