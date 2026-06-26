use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct OffUpdateVersion {
    #[serde(default)]
    pub version: String,
    #[serde(default)]
    pub verify_code: String,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct OffVersion {
    #[serde(default)]
    pub version: String,
    #[serde(default)]
    pub created_at: i64,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct OffUpdateVersionResponse {
    #[serde(default)]
    pub message: String,
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct OffQueryVersionResponse {
    #[serde(default)]
    pub version: String,
}
