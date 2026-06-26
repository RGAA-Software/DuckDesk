use serde::{Deserialize, Serialize};

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct UpdateInfo {
    #[serde(default)]
    pub desc: String,

    #[serde(default)]
    pub version: String,

    #[serde(default)]
    pub down_addr: String,

    #[serde(default)]
    pub file_md5: String,

    #[serde(default)]
    pub forced: bool,

    #[serde(default)]
    pub created_timestamp: i64,

    #[serde(default)]
    pub file_size: i64,

    #[serde(default)]
    pub file_name: String,
}
