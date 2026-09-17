use crate::{ClientType, ResourceOwner, StoreError};
use chrono::{DateTime, Utc};
use sha2::{Digest, Sha256};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum SessionTarget {
    Desktop {
        device_id: Uuid,
    },
    CloudApplication {
        application_id: Uuid,
        instance_id: Uuid,
    },
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SessionAccess {
    Controller,
    Observer,
}
impl SessionAccess {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Controller => "controller",
            Self::Observer => "observer",
        }
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct OpenResourceSession {
    pub request_id: Uuid,
    pub target: SessionTarget,
    pub access: SessionAccess,
}
impl OpenResourceSession {
    pub(crate) fn digest(&self, client: ClientType) -> Result<[u8; 32], StoreError> {
        if self.request_id.is_nil() || client == ClientType::AdminWeb {
            return Err(StoreError::InvalidInput);
        }
        let mut hash = Sha256::new();
        hash.update(b"Pixels-ResourceSession-v1\0");
        match self.target {
            SessionTarget::Desktop { device_id } => {
                if device_id.is_nil() {
                    return Err(StoreError::InvalidInput);
                }
                hash.update([0]);
                hash.update(device_id.as_bytes());
            }
            SessionTarget::CloudApplication {
                application_id,
                instance_id,
            } => {
                if application_id.is_nil() || instance_id.is_nil() {
                    return Err(StoreError::InvalidInput);
                }
                hash.update([1]);
                hash.update(application_id.as_bytes());
                hash.update(instance_id.as_bytes());
            }
        }
        hash.update(client.name().as_bytes());
        hash.update([0]);
        hash.update(self.access.name().as_bytes());
        Ok(hash.finalize().into())
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ResourceSession {
    pub id: Uuid,
    pub target: SessionTarget,
    pub owner: ResourceOwner,
    pub client_type: String,
    pub access_role: String,
    pub state: String,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub closed_at: Option<DateTime<Utc>>,
}
/// Endpoint metadata is not a bearer grant. The composition adapter delivers its
/// separately generated secret only to the intended frontend, never in management DTOs.
#[derive(Debug, Clone, serde::Serialize)]
pub struct ResourceDescriptor {
    pub session: ResourceSession,
    pub node_id: Uuid,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub endpoint_revision: i64,
    pub host: String,
    pub port: u16,
    pub transport: String,
    pub expires_at: DateTime<Utc>,
}
#[derive(Debug, Clone, serde::Serialize)]
pub struct FrontendRetirement {
    pub session_id: Uuid,
    pub challenge_id: Uuid,
    pub reject_through_revision: i64,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub deadline: DateTime<Utc>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct SessionRow {
    pub id: Uuid,
    pub target_kind: String,
    pub device_id: Option<Uuid>,
    pub application_id: Option<Uuid>,
    pub instance_id: Option<Uuid>,
    pub node_id: Uuid,
    pub owner_user: Option<Uuid>,
    pub owner_guest: Option<Uuid>,
    pub login_session_id: Option<Uuid>,
    pub owner_revision: i64,
    pub client_type: String,
    pub access_role: String,
    pub request_hash: Vec<u8>,
    pub state: String,
    pub revision: i64,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub endpoint_revision: i64,
    pub created_at: DateTime<Utc>,
    pub closed_at: Option<DateTime<Utc>>,
}
impl SessionRow {
    pub(crate) fn view(&self) -> Result<ResourceSession, StoreError> {
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
        Ok(ResourceSession {
            id: self.id,
            target,
            owner,
            client_type: self.client_type.clone(),
            access_role: self.access_role.clone(),
            state: self.state.clone(),
            revision: self.revision,
            created_at: self.created_at,
            closed_at: self.closed_at,
        })
    }
}
#[derive(sqlx::FromRow)]
pub(crate) struct SessionEndpoint {
    pub node_id: Uuid,
    pub generation: i64,
    pub control_epoch: i64,
    pub endpoint_revision: i64,
    pub host: String,
    pub port: i32,
    pub transport: String,
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn target_and_role_digest_are_explicit_and_never_fall_back() {
        let mut request = OpenResourceSession {
            request_id: Uuid::new_v4(),
            target: SessionTarget::Desktop {
                device_id: Uuid::new_v4(),
            },
            access: SessionAccess::Controller,
        };
        let first = request.digest(ClientType::Android).unwrap();
        request.request_id = Uuid::new_v4();
        assert_eq!(first, request.digest(ClientType::Android).unwrap());
        assert_ne!(first, request.digest(ClientType::Panel).unwrap());
        request.access = SessionAccess::Observer;
        assert_ne!(first, request.digest(ClientType::Android).unwrap());
        request.target = SessionTarget::CloudApplication {
            application_id: Uuid::new_v4(),
            instance_id: Uuid::nil(),
        };
        assert!(request.digest(ClientType::Android).is_err());
        assert!(request.digest(ClientType::AdminWeb).is_err());
    }
}
