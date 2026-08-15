use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CmsUser {
    // uid, generated bson id -> md5 hex it
    pub uid: String,

    // username
    #[serde(default)]
    pub username: String,

    // password // md5 hex string
    #[serde(default)]
    pub password: String,

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

    // is manager
    #[serde(default, deserialize_with = "px_base::serde_as_bool")]
    pub administrator: bool,

    #[serde(skip_deserializing, skip_serializing)]
    pub total: u32,
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
