use serde::{Deserialize, Serialize};

use crate::identity::model::GroupRef;

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ConsoleUser {
    // uid, generated bson id -> md5 hex it
    pub uid: String,

    // username
    #[serde(default)]
    pub username: String,

    // Argon2id PHC string. Persistence only; never include in an HTTP DTO.
    #[serde(default)]
    pub password_hash: String,

    // AES-GCM encrypted copy used only by the authenticated administrator
    // password-view endpoint. It is never included in a user DTO.
    #[serde(default)]
    pub password_ciphertext: String,

    #[serde(default)]
    pub username_normalized: String,

    // already used by some or not
    #[serde(default, deserialize_with = "px_base::serde_as_bool")]
    pub assigned: bool,

    // created timestamp
    #[serde(default)]
    pub created_timestamp: i64,

    // update timestamp
    #[serde(default)]
    pub update_timestamp: i64,

    // deleted?
    #[serde(default, deserialize_with = "px_base::serde_as_bool")]
    pub deleted: bool,

    // Administratively disabled accounts remain visible and can be enabled
    // again; this is intentionally distinct from soft deletion.
    #[serde(default, deserialize_with = "px_base::serde_as_bool")]
    pub disabled: bool,

    // avatar path
    #[serde(default)]
    pub avatar_path: String,

    #[serde(default)]
    pub auth_version: i64,

    #[serde(default)]
    pub must_change_password: bool,

    #[serde(default)]
    pub version: i64,

    #[serde(skip_deserializing, skip_serializing)]
    pub total: u32,
}

/// Public/admin HTTP representation of a user.  The persistence model above
/// must never be returned directly because it contains the password verifier.
#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct ConsoleUserView {
    pub uid: String,
    pub username: String,
    pub assigned: bool,
    pub created_timestamp: i64,
    pub update_timestamp: i64,
    pub deleted: bool,
    pub disabled: bool,
    pub avatar_path: String,

    #[serde(default)]
    pub auth_version: i64,

    #[serde(default)]
    pub must_change_password: bool,

    #[serde(default)]
    pub version: i64,
    #[serde(default)]
    pub groups: Vec<GroupRef>,
    pub total: u32,
}

impl From<&ConsoleUser> for ConsoleUserView {
    fn from(user: &ConsoleUser) -> Self {
        Self {
            uid: user.uid.clone(),
            username: user.username.clone(),
            assigned: user.assigned,
            created_timestamp: user.created_timestamp,
            update_timestamp: user.update_timestamp,
            deleted: user.deleted,
            disabled: user.disabled,
            avatar_path: user.avatar_path.clone(),
            auth_version: user.auth_version,
            // Retained in the wire model for compatibility with older clients.
            // Console no longer requires a first-login password change.
            must_change_password: false,
            version: user.version,
            groups: Vec::new(),
            total: user.total,
        }
    }
}

impl From<ConsoleUser> for ConsoleUserView {
    fn from(user: ConsoleUser) -> Self {
        Self::from(&user)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn http_view_never_serializes_password() {
        let user = ConsoleUser {
            uid: "u1".to_string(),
            username: "alice".to_string(),
            password_hash: "secret-verifier".to_string(),
            password_ciphertext: "encrypted-secret".to_string(),
            ..Default::default()
        };

        let json = serde_json::to_string(&ConsoleUserView::from(user)).unwrap();
        assert!(!json.contains("password_hash"));
        assert!(!json.contains("secret-verifier"));
        assert!(!json.contains("password_ciphertext"));
        assert!(!json.contains("encrypted-secret"));
    }
}
