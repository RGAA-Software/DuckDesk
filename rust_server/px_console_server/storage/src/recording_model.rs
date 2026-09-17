use crate::{file_metadata::valid_basename, StoreError};
use chrono::{DateTime, Utc};
use sha2::{Digest, Sha256};
use uuid::Uuid;
#[derive(Debug, Clone, Copy, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RecordingCodec {
    H264,
    H265,
    Av1,
    Unknown,
}
impl RecordingCodec {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::H264 => "h264",
            Self::H265 => "h265",
            Self::Av1 => "av1",
            Self::Unknown => "unknown",
        }
    }
}
/// Only finalized recordings may be catalogued. Node persists a new source UUID
/// for each immutable file version; neither filename nor mtime is its identity.
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RecordingReport {
    pub source_id: Uuid,
    pub source_sha256: [u8; 32],
    pub session_id: Option<Uuid>,
    pub file_name: String,
    pub size_bytes: u64,
    pub modified_unix_ms: i64,
    pub codec: RecordingCodec,
    pub sequence: u64,
    pub present: bool,
}
pub(crate) struct CheckedRecording {
    pub size: i64,
    pub modified: DateTime<Utc>,
    pub sequence: i64,
    pub hash: [u8; 32],
}
impl RecordingReport {
    pub(crate) fn validate(&self) -> Result<CheckedRecording, StoreError> {
        if self.source_id.is_nil()
            || self.session_id.is_some_and(|id| id.is_nil())
            || self.modified_unix_ms < 0
        {
            return Err(StoreError::InvalidInput);
        }
        valid_basename(&self.file_name)?;
        if !self.file_name.to_ascii_lowercase().ends_with(".mp4") {
            return Err(StoreError::InvalidInput);
        }
        let size = i64::try_from(self.size_bytes).map_err(|_| StoreError::InvalidInput)?;
        let sequence = i64::try_from(self.sequence).map_err(|_| StoreError::InvalidInput)?;
        let modified = DateTime::from_timestamp_millis(self.modified_unix_ms)
            .ok_or(StoreError::InvalidInput)?;
        if size == 0 || sequence == 0 {
            return Err(StoreError::InvalidInput);
        }
        let mut hash = Sha256::new();
        hash.update(b"Pixels-Recording-v1\0");
        hash.update(self.source_id.as_bytes());
        hash.update(self.source_sha256);
        match self.session_id {
            Some(id) => {
                hash.update([1]);
                hash.update(id.as_bytes());
            }
            None => hash.update([0]),
        }
        hash.update(self.file_name.as_bytes());
        hash.update([0]);
        hash.update(size.to_be_bytes());
        hash.update(self.modified_unix_ms.to_be_bytes());
        hash.update(self.codec.name().as_bytes());
        Ok(CheckedRecording {
            size,
            modified,
            sequence,
            hash: hash.finalize().into(),
        })
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct RecordingProfile {
    pub id: Uuid,
    pub node_id: Uuid,
    pub session_id: Option<Uuid>,
    pub file_name: String,
    pub size_bytes: i64,
    pub modified_at: DateTime<Utc>,
    pub codec: String,
    /// Last source observation only, NEVER authority that the node/file is online now.
    pub reported_present: bool,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub source_sequence: i64,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub observed_at: DateTime<Utc>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct RecordingRow {
    pub id: Uuid,
    pub node_id: Uuid,
    pub session_id: Option<Uuid>,
    pub file_name: String,
    pub size_bytes: i64,
    pub modified_at: DateTime<Utc>,
    pub codec: String,
    pub metadata_hash: Vec<u8>,
    pub reported_present: bool,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub source_sequence: i64,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub observed_at: DateTime<Utc>,
}
impl RecordingRow {
    pub(crate) fn view(&self) -> RecordingProfile {
        RecordingProfile {
            id: self.id,
            node_id: self.node_id,
            session_id: self.session_id,
            file_name: self.file_name.clone(),
            size_bytes: self.size_bytes,
            modified_at: self.modified_at,
            codec: self.codec.clone(),
            reported_present: self.reported_present,
            node_generation: self.node_generation,
            control_epoch: self.control_epoch,
            source_sequence: self.source_sequence,
            revision: self.revision,
            created_at: self.created_at,
            observed_at: self.observed_at,
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn recording_identity_is_not_filename_and_metadata_has_no_implicit_defaults() {
        let mut r = RecordingReport {
            source_id: Uuid::new_v4(),
            source_sha256: [18; 32],
            session_id: None,
            file_name: "屏幕 1.mp4".into(),
            size_bytes: 1,
            modified_unix_ms: 1,
            codec: RecordingCodec::H264,
            sequence: 1,
            present: true,
        };
        let first = r.validate().unwrap().hash;
        r.present = false;
        r.sequence = 2;
        assert_eq!(first, r.validate().unwrap().hash);
        r.source_id = Uuid::new_v4();
        assert_ne!(first, r.validate().unwrap().hash);
        r.size_bytes = 0;
        assert!(r.validate().is_err());
        r.size_bytes = u64::MAX;
        assert!(r.validate().is_err());
        r.size_bytes = 1;
        r.modified_unix_ms = i64::MAX;
        assert!(r.validate().is_err());
    }
}
