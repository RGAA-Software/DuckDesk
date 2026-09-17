use crate::{file_metadata::valid_basename, StoreError};
use chrono::{DateTime, Utc};
use sha2::{Digest, Sha256};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TransferDirection {
    ToNode,
    FromNode,
}
impl TransferDirection {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::ToNode => "to_node",
            Self::FromNode => "from_node",
        }
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BeginFileTransfer {
    pub request_id: Uuid,
    pub session_id: Uuid,
    pub direction: TransferDirection,
    pub file_name: String,
    pub total_bytes: u64,
    pub expected_sha256: [u8; 32],
}
impl BeginFileTransfer {
    pub(crate) fn validate(&self) -> Result<([u8; 32], i64), StoreError> {
        if self.request_id.is_nil() || self.session_id.is_nil() {
            return Err(StoreError::InvalidInput);
        }
        valid_basename(&self.file_name)?;
        let total = i64::try_from(self.total_bytes).map_err(|_| StoreError::InvalidInput)?;
        let mut digest = Sha256::new();
        digest.update(b"Pixels-FileTransfer-v1\0");
        digest.update(self.session_id.as_bytes());
        digest.update(self.direction.name().as_bytes());
        digest.update([0]);
        digest.update(self.file_name.as_bytes());
        digest.update([0]);
        digest.update(total.to_be_bytes());
        digest.update(self.expected_sha256);
        Ok((digest.finalize().into(), total))
    }
}
#[derive(Debug, Clone, Copy, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TransferFailure {
    TransportLost,
    HashMismatch,
    PolicyRevoked,
    IoError,
    SourceChanged,
}
impl TransferFailure {
    fn name(self) -> &'static str {
        match self {
            Self::TransportLost => "transport_lost",
            Self::HashMismatch => "hash_mismatch",
            Self::PolicyRevoked => "policy_revoked",
            Self::IoError => "io_error",
            Self::SourceChanged => "source_changed",
        }
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum TransferOutcome {
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Progress,
    Completed {
        received_sha256: [u8; 32],
    },
    Failed {
        reason: TransferFailure,
    },
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Cancelled,
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TransferProgress {
    pub sequence: u64,
    pub transferred_bytes: u64,
    pub outcome: TransferOutcome,
}
pub(crate) struct CheckedProgress {
    pub sequence: i64,
    pub bytes: i64,
    pub state: &'static str,
    pub reason: Option<&'static str>,
    pub received: Option<[u8; 32]>,
    pub hash: [u8; 32],
}
impl TransferProgress {
    pub(crate) fn validate(&self) -> Result<CheckedProgress, StoreError> {
        let sequence = i64::try_from(self.sequence).map_err(|_| StoreError::InvalidInput)?;
        let bytes = i64::try_from(self.transferred_bytes).map_err(|_| StoreError::InvalidInput)?;
        if sequence == 0 {
            return Err(StoreError::InvalidInput);
        }
        let (state, reason, received) = match self.outcome {
            TransferOutcome::Progress => ("active", None, None),
            TransferOutcome::Completed { received_sha256 } => {
                ("completed", None, Some(received_sha256))
            }
            TransferOutcome::Failed { reason } => ("failed", Some(reason.name()), None),
            TransferOutcome::Cancelled => ("cancelled", Some("cancelled"), None),
        };
        let mut digest = Sha256::new();
        digest.update(b"Pixels-TransferProgress-v1\0");
        digest.update(sequence.to_be_bytes());
        digest.update(bytes.to_be_bytes());
        digest.update(state.as_bytes());
        digest.update([0]);
        digest.update(reason.unwrap_or_default().as_bytes());
        if let Some(value) = received {
            digest.update(value);
        }
        Ok(CheckedProgress {
            sequence,
            bytes,
            state,
            reason,
            received,
            hash: digest.finalize().into(),
        })
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct FileTransferRecord {
    pub id: Uuid,
    pub session_id: Uuid,
    pub node_id: Uuid,
    pub direction: String,
    pub file_name: String,
    pub total_bytes: i64,
    pub transferred_bytes: i64,
    pub state: String,
    pub reason: Option<String>,
    pub sequence: i64,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub ended_at: Option<DateTime<Utc>>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct TransferRow {
    pub id: Uuid,
    pub session_id: Uuid,
    pub node_id: Uuid,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub request_hash: Vec<u8>,
    pub expected_sha256: Vec<u8>,
    pub report_hash: Option<Vec<u8>>,
    pub direction: String,
    pub file_name: String,
    pub total_bytes: i64,
    pub transferred_bytes: i64,
    pub state: String,
    pub reason: Option<String>,
    pub sequence: i64,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub ended_at: Option<DateTime<Utc>>,
}
impl TransferRow {
    pub(crate) fn view(&self) -> FileTransferRecord {
        FileTransferRecord {
            id: self.id,
            session_id: self.session_id,
            node_id: self.node_id,
            direction: self.direction.clone(),
            file_name: self.file_name.clone(),
            total_bytes: self.total_bytes,
            transferred_bytes: self.transferred_bytes,
            state: self.state.clone(),
            reason: self.reason.clone(),
            sequence: self.sequence,
            revision: self.revision,
            created_at: self.created_at,
            updated_at: self.updated_at,
            ended_at: self.ended_at,
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn file_names_and_numeric_boundaries_have_no_path_or_overflow_fallback() {
        for name in [
            "",
            ".",
            "..",
            "C:\\secret.txt",
            "a/b.txt",
            "a\\b.txt",
            "x:stream",
            "NUL.txt",
            "COM1",
            " bad.txt",
            "bad.",
            "bad\n.txt",
        ] {
            assert!(valid_basename(name).is_err(), "{name}");
        }
        assert!(valid_basename("中文 空格.txt").is_ok());
        assert!(valid_basename(&"字".repeat(86)).is_err());
        let mut begin = BeginFileTransfer {
            request_id: Uuid::new_v4(),
            session_id: Uuid::new_v4(),
            direction: TransferDirection::ToNode,
            file_name: "empty.bin".into(),
            total_bytes: 0,
            expected_sha256: [0; 32],
        };
        assert!(begin.validate().is_ok());
        begin.total_bytes = u64::MAX;
        assert!(begin.validate().is_err());
        assert!(TransferProgress {
            sequence: 0,
            transferred_bytes: 0,
            outcome: TransferOutcome::Progress
        }
        .validate()
        .is_err());
        assert!(TransferProgress {
            sequence: 1,
            transferred_bytes: u64::MAX,
            outcome: TransferOutcome::Progress
        }
        .validate()
        .is_err());
    }
}
