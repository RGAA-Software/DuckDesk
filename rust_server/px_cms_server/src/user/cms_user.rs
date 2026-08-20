use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CmsUser {
    // uid, generated bson id -> md5 hex it
    pub uid: String,

    // username
    #[serde(default)]
    pub username: String,

    // Argon2id PHC string. Persistence only; never include in an HTTP DTO.
    #[serde(default)]
    pub password_hash: String,

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
pub struct CmsUserView {
    pub uid: String,
    pub username: String,
    pub assigned: bool,
    pub created_timestamp: i64,
    pub update_timestamp: i64,
    pub deleted: bool,
    pub avatar_path: String,

    #[serde(default)]
    pub auth_version: i64,

    #[serde(default)]
    pub must_change_password: bool,

    #[serde(default)]
    pub version: i64,
    pub total: u32,
}

impl From<&CmsUser> for CmsUserView {
    fn from(user: &CmsUser) -> Self {
        Self {
            uid: user.uid.clone(),
            username: user.username.clone(),
            assigned: user.assigned,
            created_timestamp: user.created_timestamp,
            update_timestamp: user.update_timestamp,
            deleted: user.deleted,
            avatar_path: user.avatar_path.clone(),
            auth_version: user.auth_version,
            must_change_password: user.must_change_password,
            version: user.version,
            total: user.total,
        }
    }
}

impl From<CmsUser> for CmsUserView {
    fn from(user: CmsUser) -> Self {
        Self::from(&user)
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CmsUserAdapter {
    #[serde(rename = "User ID")]
    pub uid: String,

    #[serde(rename = "User Name")]
    pub user_name: String,

    #[serde(rename = "Password")]
    pub password: String,

    #[serde(rename = "Created Time")]
    pub created_time: String,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn http_view_never_serializes_password() {
        let user = CmsUser {
            uid: "u1".to_string(),
            username: "alice".to_string(),
            password_hash: "secret-verifier".to_string(),
            ..Default::default()
        };

        let json = serde_json::to_string(&CmsUserView::from(user)).unwrap();
        assert!(!json.contains("password_hash"));
        assert!(!json.contains("secret-verifier"));
    }
}
