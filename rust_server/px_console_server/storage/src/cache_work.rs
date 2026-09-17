use crate::{
    cache_model::{CacheBlob, CacheEntry, CacheSource},
    control, node_lifecycle, CacheAttempt, CacheProfile, CacheRuntime, NodeConnection,
    RecordingCacheStore, StoreError,
};
use px_private_files::{BlobReader, BlobWriter, PublishedBlob};
use sqlx::PgConnection;
use uuid::Uuid;
pub(crate) fn valid_page(limit: u32) -> Result<(), StoreError> {
    if !(1..=100).contains(&limit) {
        return Err(StoreError::InvalidInput);
    }
    Ok(())
}
impl RecordingCacheStore {
    /// Internal authenticated node worker API; these contexts never authorize browser media access.
    pub async fn pending(
        &self,
        run: &CacheRuntime,
        node: &NodeConnection,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<CacheAttempt>, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        let records = sqlx::query_file_scalar!(
            "queries/pending_node_cache.sql",
            authority.id,
            run.id,
            authority.generation,
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let mut result = Vec::new();
        for id in records {
            let source = Self::source(&mut tx, run, id).await?;
            let entry = Self::entry(&mut tx, id)
                .await?
                .ok_or(StoreError::Rejected)?;
            let blob =
                Self::blob(&mut tx, entry.active_blob_id.ok_or(StoreError::Rejected)?).await?;
            if source.fetchable
                && Self::origin(&mut tx, blob.id).await?
                && !blob.expired
                && blob.remaining_ms > 0
            {
                result.push(blob.attempt(&source)?);
            }
        }
        tx.commit().await?;
        Ok(result)
    }
    async fn worker(
        c: &mut PgConnection,
        run: &CacheRuntime,
        node: &NodeConnection,
        attempt: &CacheAttempt,
        require_origin: bool,
    ) -> Result<(CacheEntry, CacheSource, CacheBlob), StoreError> {
        Self::current(c, run).await?;
        let authority = node_lifecycle::authorize(c, node).await?;
        let source = Self::source(c, run, attempt.recording_id).await?;
        let entry = Self::entry(c, attempt.recording_id)
            .await?
            .ok_or(StoreError::Rejected)?;
        let blob = Self::blob(c, attempt.id).await?;
        if authority.id != attempt.node_id
            || authority.generation != attempt.node_generation
            || blob.node_generation != authority.generation
            || blob.run_id != run.id
            || attempt.run_id != run.id
            || blob.lease_id != attempt.lease_id
            || blob.root_id != run.root.id()
            || source.node_id != authority.id
            || source.source_id != attempt.source_id
            || source.content()? != attempt.content
            || blob.recording_id != attempt.recording_id
            || (require_origin && !Self::origin(c, blob.id).await?)
        {
            return Err(StoreError::Rejected);
        }
        Ok((entry, source, blob))
    }
    pub async fn renew(
        &self,
        run: &CacheRuntime,
        node: &NodeConnection,
        attempt: &CacheAttempt,
        writer: &BlobWriter,
    ) -> Result<CacheAttempt, StoreError> {
        if writer.id() != attempt.id
            || writer.root_id() != run.root.id()
            || writer.content() != attempt.content
        {
            return Err(StoreError::Rejected);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let (entry, source, blob) = Self::worker(&mut tx, run, node, attempt, true).await?;
        let received = i64::try_from(writer.written()).map_err(|_| StoreError::InvalidInput)?;
        if entry.active_blob_id != Some(blob.id)
            || blob.state != "fetching"
            || blob.expired
            || !source.fetchable
            || received < blob.received_bytes
            || received > blob.size_bytes
        {
            return Err(StoreError::Rejected);
        }
        let changed =
            sqlx::query_file_as!(CacheBlob, "queries/renew_cache_blob.sql", blob.id, received)
                .fetch_one(&mut *tx)
                .await?;
        let result = changed.attempt(&source)?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn publish(
        &self,
        run: &CacheRuntime,
        node: &NodeConnection,
        attempt: &CacheAttempt,
        proof: &PublishedBlob,
    ) -> Result<CacheProfile, StoreError> {
        if proof.id() != attempt.id
            || proof.root_id() != run.root.id()
            || proof.content() != attempt.content
        {
            return Err(StoreError::Rejected);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let (entry, source, blob) = Self::worker(&mut tx, run, node, attempt, true).await?;
        if entry.active_blob_id != Some(blob.id) {
            return Err(StoreError::Rejected);
        }
        if blob.state == "published" && blob.verified_run == Some(run.id) {
            let result = Self::profile(run, &entry, &source, Some(&blob));
            tx.commit().await?;
            return Ok(result);
        }
        if blob.state != "fetching" || blob.expired || !source.fetchable {
            return Err(StoreError::Rejected);
        }
        let changed =
            sqlx::query_file_as!(CacheBlob, "queries/publish_cache_blob.sql", blob.id, run.id)
                .fetch_one(&mut *tx)
                .await?;
        let entry = Self::moved(&mut tx, entry.recording_id, Some(blob.id)).await?;
        Self::event(&mut tx, run, &entry, Some(blob.id), None, "published").await?;
        let result = Self::profile(run, &entry, &source, Some(&changed));
        tx.commit().await?;
        Ok(result)
    }
    /// Revalidate an already-published archive after startup using a live, hash-checked shared file guard.
    pub async fn verify_cached(
        &self,
        run: &CacheRuntime,
        recording: Uuid,
        reader: &BlobReader,
    ) -> Result<CacheProfile, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let source = Self::source(&mut tx, run, recording).await?;
        let entry = Self::entry(&mut tx, recording)
            .await?
            .ok_or(StoreError::Rejected)?;
        let blob = Self::blob(&mut tx, entry.active_blob_id.ok_or(StoreError::Rejected)?).await?;
        if blob.state != "published"
            || blob.root_id != run.root.id()
            || reader.root_id() != run.root.id()
            || reader.id() != blob.id
            || reader.content() != source.content()?
        {
            return Err(StoreError::Rejected);
        }
        if blob.verified_run == Some(run.id) {
            let result = Self::profile(run, &entry, &source, Some(&blob));
            tx.commit().await?;
            return Ok(result);
        }
        let changed =
            sqlx::query_file_as!(CacheBlob, "queries/verify_cache_blob.sql", blob.id, run.id)
                .fetch_one(&mut *tx)
                .await?;
        let entry = Self::moved(&mut tx, recording, Some(blob.id)).await?;
        Self::event(&mut tx, run, &entry, Some(blob.id), None, "verified").await?;
        let result = Self::profile(run, &entry, &source, Some(&changed));
        tx.commit().await?;
        Ok(result)
    }
    pub async fn abandon(
        &self,
        run: &CacheRuntime,
        node: &NodeConnection,
        attempt: &CacheAttempt,
    ) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let (entry, _, blob) = Self::worker(&mut tx, run, node, attempt, false).await?;
        if blob.state != "abandoned" {
            Self::abandon_locked(&mut tx, run, &entry, &blob).await?;
        }
        tx.commit().await?;
        Ok(())
    }
    /// Timeout retires work, not its on-disk bytes or reserved capacity.
    pub async fn expire(&self, run: &CacheRuntime, limit: u32) -> Result<u32, StoreError> {
        valid_page(limit)?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let ids = sqlx::query_file_scalar!(
            "queries/expired_cache_records.sql",
            run.id,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let count = ids.len() as u32;
        for id in ids {
            let entry = Self::entry(&mut tx, id)
                .await?
                .ok_or(StoreError::Rejected)?;
            let blob =
                Self::blob(&mut tx, entry.active_blob_id.ok_or(StoreError::Rejected)?).await?;
            Self::abandon_locked(&mut tx, run, &entry, &blob).await?;
        }
        tx.commit().await?;
        Ok(count)
    }
}
