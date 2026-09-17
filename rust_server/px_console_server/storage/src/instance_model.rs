use crate::{ClientType, StoreError, TokenDigest};
use chrono::{DateTime, Utc};
use sha2::{Digest, Sha256};
use uuid::Uuid;

/// Supplied by the HTTP/WS authentication adapter, never deserialized from an owner ID.
/// The repository checks the selected identity kind; it never tries the other token table.
pub enum ResourceCredential<'a> {
    User(&'a TokenDigest),
    Guest(&'a TokenDigest),
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum ResourceOwner {
    User { user_id: Uuid },
    Guest { guest_id: Uuid },
}
impl ResourceOwner {
    pub(crate) fn columns(self) -> (Option<Uuid>, Option<Uuid>) {
        match self {
            Self::User { user_id } => (Some(user_id), None),
            Self::Guest { guest_id } => (None, Some(guest_id)),
        }
    }
}
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct StartApplication {
    pub request_id: Uuid,
    pub application_id: Uuid,
    /// A hard constraint, not a bypass of readiness/authorization/capacity.
    pub deployment_id: Option<Uuid>,
}
impl StartApplication {
    pub(crate) fn digest(&self, client: ClientType) -> Result<[u8; 32], StoreError> {
        if self.request_id.is_nil()
            || self.application_id.is_nil()
            || self.deployment_id.is_some_and(|id| id.is_nil())
            || client == ClientType::AdminWeb
        {
            return Err(StoreError::InvalidInput);
        }
        let mut hash = Sha256::new();
        hash.update(b"Pixels-Start-v1\0");
        hash.update(self.application_id.as_bytes());
        match self.deployment_id {
            Some(id) => {
                hash.update([1]);
                hash.update(id.as_bytes());
            }
            None => hash.update([0]),
        }
        hash.update(client.name().as_bytes());
        Ok(hash.finalize().into())
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize)]
pub struct ApplicationInstance {
    pub id: Uuid,
    pub application_id: Uuid,
    pub owner: ResourceOwner,
    pub client_type: String,
    pub state: String,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub ended_at: Option<DateTime<Utc>>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct InstanceRow {
    pub id: Uuid,
    pub application_id: Uuid,
    pub node_id: Uuid,
    pub owner_user: Option<Uuid>,
    pub owner_guest: Option<Uuid>,
    pub client_type: String,
    pub request_hash: Vec<u8>,
    pub state: String,
    pub revision: i64,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub deployment_id: Uuid,
    pub launch_id: Uuid,
    pub desired_state: String,
    pub application_revision: i64,
    pub deployment_revision: i64,
    pub endpoint_revision: i64,
    pub port: i32,
    pub created_at: DateTime<Utc>,
    pub ended_at: Option<DateTime<Utc>>,
}
impl InstanceRow {
    pub(crate) fn view(&self) -> Result<ApplicationInstance, StoreError> {
        let owner = match (self.owner_user, self.owner_guest) {
            (Some(user_id), None) => ResourceOwner::User { user_id },
            (None, Some(guest_id)) => ResourceOwner::Guest { guest_id },
            _ => return Err(StoreError::Database(px_pg::DatabaseError::Operation)),
        };
        Ok(ApplicationInstance {
            id: self.id,
            application_id: self.application_id,
            owner,
            client_type: self.client_type.clone(),
            state: self.state.clone(),
            revision: self.revision,
            created_at: self.created_at,
            ended_at: self.ended_at,
        })
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn request_digest_binds_application_placement_and_client_without_owner_fallback() {
        let mut request = StartApplication {
            request_id: Uuid::new_v4(),
            application_id: Uuid::new_v4(),
            deployment_id: None,
        };
        let first = request.digest(ClientType::Android).unwrap();
        request.request_id = Uuid::new_v4();
        assert_eq!(first, request.digest(ClientType::Android).unwrap());
        assert_ne!(first, request.digest(ClientType::Panel).unwrap());
        request.deployment_id = Some(Uuid::new_v4());
        assert_ne!(first, request.digest(ClientType::Android).unwrap());
        assert!(request.digest(ClientType::AdminWeb).is_err());
        request.deployment_id = Some(Uuid::nil());
        assert!(request.digest(ClientType::Android).is_err());
    }
}
