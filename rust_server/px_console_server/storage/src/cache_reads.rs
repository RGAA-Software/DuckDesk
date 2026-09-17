use crate::{
    cache_work::valid_page, control, CacheCredential, CacheProfile, CacheReadLease, CacheRuntime,
    CachedFile, RecordingCacheStore, StoreError, TokenDigest,
};
use px_private_files::BlobReader;
use uuid::Uuid;
impl RecordingCacheStore {
    /// The caller still has to open/hash this exact file and obtain a read lease.
    pub async fn cached_file(
        &self,
        run: &CacheRuntime,
        credential: CacheCredential<'_>,
        recording: Uuid,
    ) -> Result<CachedFile, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let source = Self::source(&mut tx, run, recording).await?;
        Self::credential(&mut tx, &credential, &source).await?;
        let entry = Self::entry(&mut tx, recording)
            .await?
            .ok_or(StoreError::Rejected)?;
        let blob = Self::blob(&mut tx, entry.active_blob_id.ok_or(StoreError::Rejected)?).await?;
        if blob.state != "published" || blob.root_id != run.root.id() {
            return Err(StoreError::Rejected);
        }
        let result = CachedFile {
            id: blob.id,
            recording_id: recording,
            content: source.content()?,
        };
        tx.commit().await?;
        Ok(result)
    }
    pub async fn open_read(
        &self,
        run: &CacheRuntime,
        credential: CacheCredential<'_>,
        recording: Uuid,
        reader: &BlobReader,
    ) -> Result<CacheReadLease, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let source = Self::source(&mut tx, run, recording).await?;
        let (origin, scope) = Self::credential(&mut tx, &credential, &source).await?;
        let entry = Self::entry(&mut tx, recording)
            .await?
            .ok_or(StoreError::Rejected)?;
        let blob = Self::blob(&mut tx, entry.active_blob_id.ok_or(StoreError::Rejected)?).await?;
        if reader.root_id() != run.root.id()
            || reader.id() != blob.id
            || reader.content() != source.content()?
            || blob.state != "published"
            || blob.verified_run != Some(run.id)
        {
            return Err(StoreError::Rejected);
        }
        let active = sqlx::query_file_scalar!(
            "queries/cache_live_reads.sql",
            run.id,
            None::<Uuid>,
            Some(origin.session_id)
        )
        .fetch_one(&mut *tx)
        .await?;
        if active >= 32 {
            return Err(StoreError::Rejected);
        }
        let result = sqlx::query_file_as!(
            CacheReadLease,
            "queries/create_cache_read_lease.sql",
            Uuid::new_v4(),
            blob.id,
            run.id,
            origin.user_id,
            origin.session_id,
            origin.authorization_revision,
            origin.client_type,
            scope
        )
        .fetch_one(&mut *tx)
        .await?;
        sqlx::query_file!("queries/touch_recording_cache.sql", recording)
            .execute(&mut *tx)
            .await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn renew_read(
        &self,
        run: &CacheRuntime,
        lease: &CacheReadLease,
        reader: &BlobReader,
    ) -> Result<CacheReadLease, StoreError> {
        if lease.run_id != run.id
            || reader.root_id() != run.root.id()
            || reader.id() != lease.blob_id
        {
            return Err(StoreError::Rejected);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let result = sqlx::query_file_as!(
            CacheReadLease,
            "queries/renew_cache_read_lease.sql",
            lease.id,
            run.id,
            lease.blob_id
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        let blob = Self::blob(&mut tx, lease.blob_id).await?;
        if reader.content().size() != blob.size_bytes as u64
            || reader.content().sha256().as_slice() != blob.source_sha256.as_slice()
        {
            return Err(StoreError::Rejected);
        }
        sqlx::query_file!("queries/touch_recording_cache.sql", blob.recording_id)
            .execute(&mut *tx)
            .await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn close_read(
        &self,
        run: &CacheRuntime,
        lease: &CacheReadLease,
    ) -> Result<(), StoreError> {
        if lease.run_id != run.id {
            return Err(StoreError::Rejected);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        sqlx::query_file!(
            "queries/close_cache_read_lease.sql",
            lease.id,
            run.id,
            lease.blob_id
        )
        .execute(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(())
    }
    /// Administrative write permission, explicit revision CAS; no automatic unpin under pressure.
    pub async fn retain(
        &self,
        run: &CacheRuntime,
        admin: &TokenDigest,
        recording: Uuid,
        revision: i64,
        retained: bool,
    ) -> Result<CacheProfile, StoreError> {
        if revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let source = Self::source(&mut tx, run, recording).await?;
        let entry = Self::entry(&mut tx, recording)
            .await?
            .ok_or(StoreError::Rejected)?;
        if entry.revision != revision {
            return Err(StoreError::Rejected);
        }
        let blob = match entry.active_blob_id {
            Some(id) => Some(Self::blob(&mut tx, id).await?),
            None => None,
        };
        if retained
            && !blob
                .as_ref()
                .is_some_and(|b| b.state == "published" && b.verified_run == Some(run.id))
        {
            return Err(StoreError::Rejected);
        }
        if entry.pinned == retained {
            let result = Self::profile(run, &entry, &source, blob.as_ref());
            tx.commit().await?;
            return Ok(result);
        }
        let changed = sqlx::query_file_as!(
            crate::cache_model::CacheEntry,
            "queries/retain_recording_cache.sql",
            recording,
            retained
        )
        .fetch_one(&mut *tx)
        .await?;
        sqlx::query_file!(
            "queries/retain_cache_blobs.sql",
            recording,
            retained,
            entry.active_blob_id
        )
        .execute(&mut *tx)
        .await?;
        Self::event(
            &mut tx,
            run,
            &changed,
            entry.active_blob_id,
            Some(actor),
            if retained { "retained" } else { "released" },
        )
        .await?;
        let result = Self::profile(run, &changed, &source, blob.as_ref());
        tx.commit().await?;
        Ok(result)
    }
    /// Internal bounded GC discovery. Rechecked with an exclusive file guard by begin_collection.
    pub async fn collection_candidates(
        &self,
        run: &CacheRuntime,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<Uuid>, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let result = sqlx::query_file_scalar!(
            "queries/cache_collection_candidates.sql",
            run.root.id(),
            run.id,
            run.options.ttl_seconds as f64,
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
}
