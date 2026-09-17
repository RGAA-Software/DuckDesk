use crate::{ClientType, RuntimeEpoch, StoreError, TokenDigest};
use chrono::{DateTime, Utc};
use px_private_files::{CacheRoot, ContentIdentity};
use std::sync::Arc;
use uuid::Uuid;

#[derive(Debug, Clone, Copy)]
pub struct CacheOptions {
    pub byte_limit: u64,
    pub maximum_downloads: u32,
    pub ttl_seconds: u32,
}
impl CacheOptions {
    pub(crate) fn validate(self) -> Result<(), StoreError> {
        if !(1_048_576..=1_099_511_627_776).contains(&self.byte_limit)
            || !(1..=32).contains(&self.maximum_downloads)
            || !(60..=604800).contains(&self.ttl_seconds)
        {
            return Err(StoreError::InvalidInput);
        }
        Ok(())
    }
}
/// An in-process capability; never accepted from JSON or reconstructed from a request ID.
#[derive(Clone)]
pub struct CacheRuntime {
    pub(crate) id: Uuid,
    pub(crate) root: Arc<CacheRoot>,
    pub(crate) epoch: RuntimeEpoch,
    pub(crate) options: CacheOptions,
}
impl CacheRuntime {
    pub fn id(&self) -> Uuid {
        self.id
    }
    pub fn root(&self) -> &Arc<CacheRoot> {
        &self.root
    }
}
pub enum CacheCredential<'a> {
    Managed(&'a TokenDigest),
    DeviceUser {
        token: &'a TokenDigest,
        client: ClientType,
    },
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct CacheProfile {
    pub recording_id: Uuid,
    pub state: String,
    pub pinned: bool,
    pub revision: i64,
    pub size_bytes: i64,
    pub received_bytes: i64,
    pub updated_at: DateTime<Utc>,
}
/// Private worker context. It is not a media token and is not serialized into user responses.
#[derive(Clone)]
pub struct CacheAttempt {
    pub(crate) id: Uuid,
    pub(crate) recording_id: Uuid,
    pub(crate) node_id: Uuid,
    pub(crate) source_id: Uuid,
    pub(crate) node_generation: i64,
    pub(crate) run_id: Uuid,
    pub(crate) lease_id: Uuid,
    pub(crate) content: ContentIdentity,
    pub(crate) expires_at: DateTime<Utc>,
    pub(crate) valid_for_ms: u32,
}
impl std::fmt::Debug for CacheAttempt {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("CacheAttempt(<redacted>)")
    }
}
impl CacheAttempt {
    pub fn id(&self) -> Uuid {
        self.id
    }
    pub fn recording_id(&self) -> Uuid {
        self.recording_id
    }
    pub fn node_id(&self) -> Uuid {
        self.node_id
    }
    pub fn source_id(&self) -> Uuid {
        self.source_id
    }
    pub fn content(&self) -> ContentIdentity {
        self.content
    }
    pub fn expires_at(&self) -> DateTime<Utc> {
        self.expires_at
    }
    /// Anchor this to request-send monotonic time, never receipt time or the local wall clock.
    pub fn valid_for_ms(&self) -> u32 {
        self.valid_for_ms
    }
}
#[derive(sqlx::FromRow)]
pub(crate) struct CacheEntry {
    pub recording_id: Uuid,
    pub active_blob_id: Option<Uuid>,
    pub pinned: bool,
    pub revision: i64,
    pub updated_at: DateTime<Utc>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct CacheSource {
    pub recording_id: Uuid,
    pub node_id: Uuid,
    pub device_id: Uuid,
    pub source_id: Uuid,
    pub source_sha256: Vec<u8>,
    pub size_bytes: i64,
    pub node_generation: i64,
    pub fetchable: bool,
}
impl CacheSource {
    pub(crate) fn content(&self) -> Result<ContentIdentity, StoreError> {
        let size = self
            .size_bytes
            .try_into()
            .map_err(|_| StoreError::InvalidInput)?;
        let hash = self
            .source_sha256
            .as_slice()
            .try_into()
            .map_err(|_| StoreError::InvalidInput)?;
        Ok(ContentIdentity::new(size, hash)?)
    }
}
#[derive(sqlx::FromRow)]
pub(crate) struct CacheBlob {
    pub id: Uuid,
    pub recording_id: Uuid,
    pub root_id: Uuid,
    pub run_id: Uuid,
    pub node_generation: i64,
    pub lease_id: Uuid,
    pub lease_until: DateTime<Utc>,
    pub state: String,
    pub size_bytes: i64,
    pub source_sha256: Vec<u8>,
    pub received_bytes: i64,
    pub verified_run: Option<Uuid>,
    pub expired: bool,
    pub remaining_ms: i64,
}
impl CacheBlob {
    pub(crate) fn attempt(&self, source: &CacheSource) -> Result<CacheAttempt, StoreError> {
        if self.state != "fetching"
            || self.expired
            || self.remaining_ms <= 0
            || self.recording_id != source.recording_id
        {
            return Err(StoreError::Rejected);
        }
        let content = source.content()?;
        if self.size_bytes != source.size_bytes || self.source_sha256 != source.source_sha256 {
            return Err(StoreError::Rejected);
        }
        Ok(CacheAttempt {
            id: self.id,
            recording_id: self.recording_id,
            node_id: source.node_id,
            source_id: source.source_id,
            node_generation: self.node_generation,
            run_id: self.run_id,
            lease_id: self.lease_id,
            content,
            expires_at: self.lease_until,
            valid_for_ms: self.remaining_ms.clamp(0, 30000) as u32,
        })
    }
}
#[derive(sqlx::FromRow)]
pub(crate) struct CacheOrigin {
    pub user_id: Uuid,
    pub session_id: Uuid,
    pub authorization_revision: i64,
    pub client_type: String,
}

/// Metadata for opening a private file, not a bearer capability or a public path.
#[derive(Debug, Clone)]
pub struct CachedFile {
    pub id: Uuid,
    pub recording_id: Uuid,
    pub content: ContentIdentity,
}
/// A process-local read grant. The transport must anchor its TTL to request-send monotonic time
/// and stop before expiry; possessing a reader never bypasses renewal failure.
#[derive(Clone, sqlx::FromRow)]
pub struct CacheReadLease {
    pub(crate) id: Uuid,
    pub(crate) blob_id: Uuid,
    pub(crate) run_id: Uuid,
    pub(crate) expires_at: DateTime<Utc>,
    pub(crate) remaining_ms: i64,
}
impl std::fmt::Debug for CacheReadLease {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("CacheReadLease(<redacted>)")
    }
}
impl CacheReadLease {
    pub fn valid_for_ms(&self) -> u32 {
        self.remaining_ms.clamp(0, 30000) as u32
    }
    pub fn expires_at(&self) -> DateTime<Utc> {
        self.expires_at
    }
}
