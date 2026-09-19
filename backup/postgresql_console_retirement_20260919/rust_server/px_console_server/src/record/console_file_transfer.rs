use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConsoleFileTransfer {
    #[serde(default)]
    pub the_file_id: String,

    #[serde(default)]
    pub visitor_device: String,

    #[serde(default)]
    pub target_device: String,

    #[serde(default)]
    pub begin: i64,

    #[serde(default)]
    pub end: i64,

    #[serde(default)]
    pub direction: String,

    #[serde(default)]
    pub file_detail: String,

    #[serde(default)]
    pub success: bool,

    #[serde(default)]
    pub duration: i64,

    #[serde(default)]
    pub status: String,

    #[serde(default)]
    pub end_reason: String,

    #[serde(default)]
    pub recovered: bool,

    #[serde(default)]
    pub created_timestamp: i64,

    #[serde(default)]
    pub total: i64,
}

impl ConsoleFileTransfer {}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConsoleUpdateFileTransfer {
    #[serde(default)]
    pub the_file_id: String,

    #[serde(default)]
    pub end: i64,

    #[serde(default)]
    pub success: bool,

    #[serde(default)]
    pub duration: i64,

    #[serde(default)]
    pub status: String,

    #[serde(default)]
    pub end_reason: String,

    #[serde(default)]
    pub recovered: bool,
}

impl ConsoleUpdateFileTransfer {}
