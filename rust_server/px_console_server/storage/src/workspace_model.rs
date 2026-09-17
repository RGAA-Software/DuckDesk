use crate::workspace_vault::{SealedCredential, SecretBinding};
use chrono::{DateTime, Utc};
use uuid::Uuid;
use zeroize::Zeroizing;

#[derive(Debug, Clone, Copy)]
pub struct WorkspaceCommandLease {
    pub command_id: Uuid,
    pub lease_id: Uuid,
}
/// Deliberately not Debug, Clone or Serialize. Only a protected credential adapter
/// may access this value; ordinary node commands, management DTOs and logs cannot.
pub struct WorkspaceCredential {
    pub workspace_id: Uuid,
    pub account_name: String,
    pub windows_sid: Option<String>,
    pub credential_revision: i64,
    pub password: Zeroizing<String>,
}
#[derive(Debug, Clone, serde::Serialize, sqlx::FromRow)]
pub struct WorkspaceProfile {
    pub id: Uuid,
    pub application_id: Uuid,
    pub deployment_id: Uuid,
    pub node_id: Uuid,
    pub account_name: String,
    pub windows_sid: Option<String>,
    pub state: String,
    pub revision: i64,
    pub credential_revision: i64,
    pub created_at: DateTime<Utc>,
    pub updated_at: DateTime<Utc>,
}
#[derive(sqlx::FromRow)]
pub(crate) struct WorkspaceRow {
    pub id: Uuid,
    pub application_id: Uuid,
    pub deployment_id: Uuid,
    pub node_id: Uuid,
    pub account_name: String,
    pub windows_sid: Option<String>,
    pub revision: i64,
    pub credential_revision: i64,
    pub schema_version: i32,
    pub key_id: Uuid,
    pub nonce: Vec<u8>,
    pub ciphertext: Vec<u8>,
}
impl WorkspaceRow {
    pub fn binding(&self, deployment: Uuid) -> SecretBinding {
        SecretBinding {
            deployment,
            workspace: self.id,
            application: self.application_id,
            placement: self.deployment_id,
            node: self.node_id,
            account: self.account_name.clone(),
            credential_revision: self.credential_revision,
        }
    }
    pub fn secret(&self) -> SealedCredential {
        SealedCredential {
            key_id: self.key_id,
            nonce: self.nonce.clone(),
            ciphertext: self.ciphertext.clone(),
        }
    }
}
pub(crate) fn validate_sid(value: &str) -> bool {
    let Some(rest) = value.strip_prefix("S-1-5-21-") else {
        return false;
    };
    let values: Vec<_> = rest.split('-').collect();
    values.len() == 4
        && values.iter().all(|part| {
            !part.is_empty()
                && part.len() <= 10
                && (part.len() == 1 || !part.starts_with('0'))
                && part.bytes().all(|v| v.is_ascii_digit())
                && part.parse::<u32>().is_ok()
        })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn windows_sid_is_canonical_local_account_identity() {
        assert!(validate_sid("S-1-5-21-1-2-4294967295-1001"));
        for sid in [
            "S-1-5-18",
            "S-1-5-21-1-2-3",
            "S-1-5-21-01-2-3-1001",
            "S-1-5-21-1-2-3-4294967296",
            "S-1-5-21-1-2-3-1001 ",
            "S-1-5-21-1-2-3-+1",
            "s-1-5-21-1-2-3-1001",
        ] {
            assert!(!validate_sid(sid));
        }
    }
}
