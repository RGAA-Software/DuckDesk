use crate::{ResourceOwner, ResourceSession, SessionTarget, StoreError};
use chrono::{DateTime, Utc};
use sha2::{Digest, Sha256};
use uuid::Uuid;
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelKind {
    Control,
    Media,
    Audio,
    File,
    Rdp,
}
impl ChannelKind {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Control => "control",
            Self::Media => "media",
            Self::Audio => "audio",
            Self::File => "file",
            Self::Rdp => "rdp",
        }
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct OpenChannel {
    pub source_id: Uuid,
    pub session_id: Uuid,
    pub kind: ChannelKind,
}
impl OpenChannel {
    pub(crate) fn digest(&self) -> Result<[u8; 32], StoreError> {
        if self.source_id.is_nil() || self.session_id.is_nil() {
            return Err(StoreError::InvalidInput);
        }
        let mut hasher = Sha256::new();
        hasher.update(b"Pixels-Connection-v1\0");
        hasher.update(self.session_id.as_bytes());
        hasher.update(self.kind.name().as_bytes());
        Ok(hasher.finalize().into())
    }
}
#[derive(Debug, Clone, Copy, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelFailure {
    TransportLost,
    PolicyRevoked,
    IoError,
}
#[derive(Debug, Clone, Copy, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelClose {
    PeerClosed,
    UserStopped,
}
#[derive(Debug, Clone, Copy, serde::Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum ChannelOutcome {
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Progress,
    Closed {
        reason: ChannelClose,
    },
    Failed {
        reason: ChannelFailure,
    },
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ChannelProgress {
    pub sequence: u64,
    pub sent_bytes: u64,
    pub received_bytes: u64,
    pub elapsed_ms: u64,
    pub outcome: ChannelOutcome,
}
pub(crate) struct CheckedChannel {
    pub sequence: i64,
    pub sent: i64,
    pub received: i64,
    pub elapsed: i64,
    pub state: &'static str,
    pub reason: Option<&'static str>,
    pub hash: [u8; 32],
}
impl ChannelProgress {
    pub(crate) fn validate(&self) -> Result<CheckedChannel, StoreError> {
        let checked_i64 = |value| i64::try_from(value).map_err(|_| StoreError::InvalidInput);
        let sequence = checked_i64(self.sequence)?;
        if sequence == 0 {
            return Err(StoreError::InvalidInput);
        }
        let sent = checked_i64(self.sent_bytes)?;
        let received = checked_i64(self.received_bytes)?;
        let elapsed = checked_i64(self.elapsed_ms)?;
        let (state, reason) = match self.outcome {
            ChannelOutcome::Progress => ("active", None),
            ChannelOutcome::Closed { reason } => (
                "closed",
                Some(match reason {
                    ChannelClose::PeerClosed => "peer_closed",
                    ChannelClose::UserStopped => "user_stopped",
                }),
            ),
            ChannelOutcome::Failed { reason } => (
                "failed",
                Some(match reason {
                    ChannelFailure::TransportLost => "transport_lost",
                    ChannelFailure::PolicyRevoked => "policy_revoked",
                    ChannelFailure::IoError => "io_error",
                }),
            ),
        };
        let mut hasher = Sha256::new();
        hasher.update(b"Pixels-ChannelProgress-v1\0");
        hasher.update(sequence.to_be_bytes());
        hasher.update(sent.to_be_bytes());
        hasher.update(received.to_be_bytes());
        hasher.update(elapsed.to_be_bytes());
        hasher.update(state.as_bytes());
        hasher.update([0]);
        hasher.update(reason.unwrap_or("").as_bytes());
        Ok(CheckedChannel {
            sequence,
            sent,
            received,
            elapsed,
            state,
            reason,
            hash: hasher.finalize().into(),
        })
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ChannelRecord {
    pub id: Uuid,
    pub session_id: Uuid,
    pub node_id: Uuid,
    pub kind: String,
    pub state: String,
    pub reason: Option<String>,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub sequence: i64,
    pub revision: i64,
    pub sent_bytes: i64,
    pub received_bytes: i64,
    pub elapsed_ms: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub ended_at: Option<DateTime<Utc>>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct ChannelRow {
    pub id: Uuid,
    pub source_id: Uuid,
    pub session_id: Uuid,
    pub node_id: Uuid,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub kind: String,
    pub request_hash: Vec<u8>,
    pub state: String,
    pub reason: Option<String>,
    pub sent_bytes: i64,
    pub received_bytes: i64,
    pub elapsed_ms: i64,
    pub sequence: i64,
    pub report_hash: Option<Vec<u8>>,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
    pub ended_at: Option<DateTime<Utc>>,
}
impl ChannelRow {
    pub(crate) fn view(self) -> ChannelRecord {
        ChannelRecord {
            id: self.id,
            session_id: self.session_id,
            node_id: self.node_id,
            kind: self.kind,
            state: self.state,
            reason: self.reason,
            node_generation: self.node_generation,
            control_epoch: self.control_epoch,
            sequence: self.sequence,
            revision: self.revision,
            sent_bytes: self.sent_bytes,
            received_bytes: self.received_bytes,
            elapsed_ms: self.elapsed_ms,
            created_at: self.created_at,
            updated_at: self.updated_at,
            ended_at: self.ended_at,
        }
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct VisitRecord {
    pub session: ResourceSession,
    pub node_id: Uuid,
    pub first_connected_at: Option<DateTime<Utc>>,
    pub channel_count: i64,
}
#[derive(sqlx::FromRow)]
pub(crate) struct VisitRow {
    pub id: Uuid,
    pub target_kind: String,
    pub device_id: Option<Uuid>,
    pub application_id: Option<Uuid>,
    pub instance_id: Option<Uuid>,
    pub node_id: Uuid,
    pub owner_user: Option<Uuid>,
    pub owner_guest: Option<Uuid>,
    pub client_type: String,
    pub access_role: String,
    pub state: String,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub closed_at: Option<DateTime<Utc>>,
    pub first_connected_at: Option<DateTime<Utc>>,
    pub channel_count: i64,
}
impl VisitRow {
    pub(crate) fn view(self) -> Result<VisitRecord, StoreError> {
        let target = match (
            self.target_kind.as_str(),
            self.device_id,
            self.application_id,
            self.instance_id,
        ) {
            ("desktop", Some(device_id), None, None) => SessionTarget::Desktop { device_id },
            ("cloud_application", None, Some(application_id), Some(instance_id)) => {
                SessionTarget::CloudApplication {
                    application_id,
                    instance_id,
                }
            }
            _ => return Err(StoreError::Rejected),
        };
        let owner = match (self.owner_user, self.owner_guest) {
            (Some(user_id), None) => ResourceOwner::User { user_id },
            (None, Some(guest_id)) => ResourceOwner::Guest { guest_id },
            _ => return Err(StoreError::Rejected),
        };
        Ok(VisitRecord {
            session: ResourceSession {
                id: self.id,
                target,
                owner,
                client_type: self.client_type,
                access_role: self.access_role,
                state: self.state,
                revision: self.revision,
                created_at: self.created_at,
                closed_at: self.closed_at,
            },
            node_id: self.node_id,
            first_connected_at: self.first_connected_at,
            channel_count: self.channel_count,
        })
    }
}
pub(crate) fn page(limit: u32) -> Result<(), StoreError> {
    if !(1..=100).contains(&limit) {
        return Err(StoreError::InvalidInput);
    }
    Ok(())
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn channel_identity_progress_bounds_and_body_hash_have_no_defaults() {
        let mut open_request = OpenChannel {
            source_id: Uuid::new_v4(),
            session_id: Uuid::new_v4(),
            kind: ChannelKind::Control,
        };
        let control_hash = open_request.digest().unwrap();
        open_request.kind = ChannelKind::Media;
        assert_ne!(control_hash, open_request.digest().unwrap());
        open_request.session_id = Uuid::nil();
        assert!(open_request.digest().is_err());
        let mut progress = ChannelProgress {
            sequence: 1,
            sent_bytes: 0,
            received_bytes: 1,
            elapsed_ms: 1,
            outcome: ChannelOutcome::Progress,
        };
        let initial_progress_hash = progress.validate().unwrap().hash;
        progress.received_bytes = 2;
        assert_ne!(initial_progress_hash, progress.validate().unwrap().hash);
        progress.elapsed_ms = u64::MAX;
        assert!(progress.validate().is_err());
        progress.elapsed_ms = 1;
        progress.sequence = 0;
        assert!(progress.validate().is_err());
        progress.sequence = 1;
        progress.sent_bytes = u64::MAX;
        assert!(progress.validate().is_err());
    }
}
