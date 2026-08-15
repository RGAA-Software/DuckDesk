use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CmsVisit {
    #[serde(default)]
    pub conn_id: String,

    #[serde(default)]
    pub stream_id: String,

    #[serde(default)]
    pub conn_type: String,

    #[serde(default)]
    pub visitor_device: String,

    #[serde(default)]
    pub target_device: String,

    #[serde(default)]
    pub begin: i64,

    #[serde(default)]
    pub end: i64,

    #[serde(default)]
    pub duration: i64,

    #[serde(default)]
    pub created_timestamp: i64,

    #[serde(default)]
    pub total: i64,
}

impl CmsVisit {}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CmsUpdateVisit {
    #[serde(default)]
    pub conn_id: String,

    #[serde(default)]
    pub end: i64,

    #[serde(default)]
    pub duration: i64,
}

impl CmsUpdateVisit {}
