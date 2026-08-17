//! mongo model for `c_records`: one record per (device_id, filename) cached /
//! kept render-record file on the cms host
//! (docs/cms_render_records_view_design.md section 6.3 / 6.4).

use serde::{Deserialize, Serialize};

pub const RECORD_STATE_FETCHING: &str = "fetching";
pub const RECORD_STATE_READY: &str = "ready";
pub const RECORD_STATE_ERROR: &str = "error";

/// deterministic id: "{device_id}:{filename}"
pub fn make_record_id(device_id: &str, filename: &str) -> String {
    format!("{}:{}", device_id, filename)
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CmsRenderRecord {
    // "{device_id}:{filename}", unique
    #[serde(default)]
    pub id: String,

    #[serde(default)]
    pub device_id: String,

    #[serde(default)]
    pub filename: String,

    // expected total bytes (from the panel file stat)
    #[serde(default)]
    pub size: i64,

    // device file mtime, unix seconds (0 when unknown, e.g. direct-pull download)
    #[serde(default)]
    pub mtime: i64,

    // keep=true: user-pinned copy, exempt from TTL / disk-threshold cleanup (6.4)
    #[serde(default)]
    pub keep: bool,

    // "fetching" | "ready" | "error"
    #[serde(default)]
    pub state: String,

    // received bytes so far (for the "回传中 x%" display, 6.1)
    #[serde(default)]
    pub progress: i64,

    #[serde(default)]
    pub error: String,

    // milliseconds
    #[serde(default)]
    pub created_timestamp: i64,

    // milliseconds; TTL is counted from this (停止查看 24h)
    #[serde(default)]
    pub updated_timestamp: i64,
}

impl CmsRenderRecord {
    pub fn url(&self) -> String {
        format!("/uploads/records/{}/{}", self.device_id, self.filename)
    }
}
