use crate::{control, CacheRuntime, RecordingCacheStore, StoreError, TokenDigest};
use px_private_files::{BlobGuard, DeletedBlob};
use sqlx::PgConnection;
use uuid::Uuid;
impl RecordingCacheStore {
    async fn collect_locked(
        c: &mut PgConnection,
        run: &CacheRuntime,
        guard: &BlobGuard,
        explicit: Option<(Uuid, i64)>,
    ) -> Result<(), StoreError> {
        Self::current(c, run).await?;
        if guard.root_id() != run.root.id() {
            return Err(StoreError::Rejected);
        }
        let blob = Self::blob(c, guard.id()).await?;
        let entry = Self::entry(c, blob.recording_id)
            .await?
            .ok_or(StoreError::Rejected)?;
        if blob.root_id != run.root.id() {
            return Err(StoreError::Rejected);
        }
        if let Some((_, revision)) = explicit {
            if revision != entry.revision {
                return Err(StoreError::Rejected);
            }
        }
        if blob.state == "deleted" {
            return Ok(());
        }
        let eligible = sqlx::query_file_scalar!(
            "queries/cache_collection_eligible.sql",
            blob.id,
            run.id,
            run.options.ttl_seconds as f64,
            explicit.is_some()
        )
        .fetch_one(&mut *c)
        .await?;
        let readers = sqlx::query_file_scalar!(
            "queries/cache_live_reads.sql",
            run.id,
            Some(blob.id),
            None::<Uuid>
        )
        .fetch_one(&mut *c)
        .await?;
        if !eligible || readers != 0 {
            return Err(StoreError::Rejected);
        }
        if blob.state == "deleting" {
            return Ok(());
        }
        sqlx::query_file!("queries/mark_cache_deleting.sql", blob.id)
            .execute(&mut *c)
            .await?;
        let next = if entry.active_blob_id == Some(blob.id) {
            None
        } else {
            entry.active_blob_id
        };
        let changed = Self::moved(c, entry.recording_id, next).await?;
        Self::event(
            c,
            run,
            &changed,
            Some(blob.id),
            explicit.map(|(actor, _)| actor),
            "evicted",
        )
        .await?;
        Ok(())
    }
    /// First detach any Ready reference and commit irreversible deleting. File IO follows the commit.
    pub async fn begin_collection(
        &self,
        run: &CacheRuntime,
        guard: &BlobGuard,
    ) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::collect_locked(&mut tx, run, guard, None).await?;
        tx.commit().await?;
        Ok(())
    }
    /// Explicit removal may bypass TTL, never pin, live download, read lease or a stale page revision.
    pub async fn evict(
        &self,
        run: &CacheRuntime,
        admin: &TokenDigest,
        revision: i64,
        guard: &BlobGuard,
    ) -> Result<(), StoreError> {
        if revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        Self::collect_locked(&mut tx, run, guard, Some((actor, revision))).await?;
        tx.commit().await?;
        Ok(())
    }
    /// Capacity is released only while owning the actual deletion proof's exclusive lock.
    pub async fn finish_collection(
        &self,
        run: &CacheRuntime,
        proof: &DeletedBlob,
    ) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        Self::current(&mut tx, run).await?;
        let blob = Self::blob(&mut tx, proof.id()).await?;
        if proof.root_id() != run.root.id() || blob.root_id != run.root.id() {
            return Err(StoreError::Rejected);
        }
        if blob.state == "deleted" {
            tx.commit().await?;
            return Ok(());
        }
        if blob.state != "deleting" {
            return Err(StoreError::Rejected);
        }
        sqlx::query_file!("queries/mark_cache_deleted.sql", blob.id)
            .execute(&mut *tx)
            .await?;
        let entry = Self::entry(&mut tx, blob.recording_id)
            .await?
            .ok_or(StoreError::Rejected)?;
        if entry.active_blob_id == Some(blob.id) {
            return Err(StoreError::Rejected);
        }
        let changed = Self::moved(&mut tx, entry.recording_id, entry.active_blob_id).await?;
        Self::event(&mut tx, run, &changed, Some(blob.id), None, "collected").await?;
        tx.commit().await?;
        Ok(())
    }
}
