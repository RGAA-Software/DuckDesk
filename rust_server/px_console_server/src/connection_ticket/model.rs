use crate::rtc::model::RtcSessionIceConfig;
use mongodb::bson::DateTime;
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ConnectionTicket {
    pub ticket_hash: String,
    /// Rotating bearer capability used only to obtain a fresh one-time ticket
    /// after a browser RTC connection has already consumed `ticket_hash`.
    /// The raw value is never persisted.
    #[serde(default)]
    pub renewal_hash: String,
    pub kind: String,
    pub subject_type: String,
    pub subject_id: String,
    pub session_id: String,
    /// Server-issued remote-control session identity. This is intentionally
    /// distinct from `session_id`, which identifies the authenticated Console
    /// login session.
    #[serde(default)]
    pub logical_session_id: String,
    /// Opaque Render routing id, issued by Console and bound to the logical
    /// session. Peers must never choose this value themselves.
    #[serde(default)]
    pub stream_id: String,
    /// Requested admission mode, not a fine-grained capability grant.
    #[serde(default = "default_join_mode")]
    pub join_mode: String,
    /// Policy is snapshotted into the ticket so the target Render can enforce
    /// the exact admission contract that Console authorized.
    #[serde(default = "default_true")]
    pub allow_observer: bool,
    #[serde(default = "default_true")]
    pub allow_takeover: bool,
    pub device_id: String,
    pub app_id: Option<String>,
    pub instance_id: Option<String>,
    pub permissions: Vec<String>,
    pub client_nonce: String,
    pub created_at: i64,
    pub expires_at: i64,
    #[serde(default)]
    pub renewal_expires_at: i64,
    pub cleanup_at: DateTime,
    pub consumed_at: Option<i64>,
    /// Stable id of the logical redemption operation. An exact, short-lived
    /// replay is permitted so Direct RTC can retry an occupied allocation with
    /// takeover=1 without turning a successfully consumed ticket into a 403.
    #[serde(default)]
    pub consumed_request_id: Option<String>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct TicketGrant {
    pub kind: String,
    pub device_id: String,
    pub app_id: Option<String>,
    pub instance_id: Option<String>,
    pub subject_type: String,
    pub subject_id: String,
    pub logical_session_id: String,
    pub stream_id: String,
    pub join_mode: String,
    pub allow_observer: bool,
    pub allow_takeover: bool,
    pub permissions: Vec<String>,
    pub expires_at: i64,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct TicketResponse {
    pub ticket: String,
    pub renewal_token: String,
    pub launch_url: String,
    pub expires_at: i64,
    pub logical_session_id: String,
    pub stream_id: String,
    pub join_mode: String,
    pub permissions: Vec<String>,
    pub rtc_ice_config: RtcSessionIceConfig,
    pub relay_host: String,
    pub relay_port: u16,
    /// Relay routing target. Application child renders have an identity which
    /// is independent from the physical device bound to the ticket.
    pub signal_device_id: String,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct TicketRenewResponse {
    pub ticket: String,
    pub renewal_token: String,
    pub expires_at: i64,
    pub logical_session_id: String,
    pub stream_id: String,
    pub join_mode: String,
    pub permissions: Vec<String>,
    pub rtc_ice_config: RtcSessionIceConfig,
}

fn default_join_mode() -> String {
    "control".to_string()
}

fn default_true() -> bool {
    true
}
