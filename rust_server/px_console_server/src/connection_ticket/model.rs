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
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct TicketGrant {
    pub kind: String,
    pub device_id: String,
    pub app_id: Option<String>,
    pub instance_id: Option<String>,
    pub subject_type: String,
    pub subject_id: String,
    pub permissions: Vec<String>,
    pub expires_at: i64,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct TicketResponse {
    pub ticket: String,
    pub renewal_token: String,
    pub launch_url: String,
    pub expires_at: i64,
    pub permissions: Vec<String>,
}

#[derive(Debug, Clone, Default, Serialize)]
pub struct TicketRenewResponse {
    pub ticket: String,
    pub renewal_token: String,
    pub expires_at: i64,
    pub permissions: Vec<String>,
}
