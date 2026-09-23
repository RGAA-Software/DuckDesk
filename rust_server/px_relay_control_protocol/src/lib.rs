//! Strict wire contract for the Relay-initiated Console control connection.

use serde::{Deserialize, Serialize};
use uuid::Uuid;

pub const MAX_CONNECTIONS: usize = 128;
pub const MAX_MESSAGE_BYTES: usize = 8 * 1024;

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RelayReport {
    pub sequence: u64,
    pub product_version_code: u32,
    pub draining: bool,
    pub max_connections: u32,
    pub current_connections: u32,
    pub max_rooms: u32,
    pub current_rooms: u32,
    pub uploaded_bytes: u64,
    pub forwarded_bytes: u64,
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
pub enum RelayRequest {
    Authenticate {
        request_id: u64,
        relay_token: String,
    },
    Report {
        request_id: u64,
        report: RelayReport,
    },
}

impl RelayRequest {
    pub fn request_id(&self) -> u64 {
        match self {
            Self::Authenticate { request_id, .. } | Self::Report { request_id, .. } => *request_id,
        }
    }
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
pub enum RelayResponse {
    Authenticated {
        request_id: u64,
        relay_node_id: Uuid,
        generation: i64,
        control_epoch: i64,
        desired_draining: bool,
    },
    Reported {
        request_id: u64,
        desired_draining: bool,
    },
    Error {
        request_id: Option<u64>,
        code: String,
    },
}

impl RelayResponse {
    pub fn request_id(&self) -> Option<u64> {
        match self {
            Self::Authenticated { request_id, .. } | Self::Reported { request_id, .. } => {
                Some(*request_id)
            }
            Self::Error { request_id, .. } => *request_id,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn contract_rejects_unknown_fields_and_preserves_request_identity() {
        let request: RelayRequest = serde_json::from_str(
            r#"{"type":"report","request_id":7,"report":{"sequence":1,"product_version_code":1,"draining":false,"max_connections":2,"current_connections":1,"max_rooms":1,"current_rooms":0,"uploaded_bytes":3,"forwarded_bytes":2}}"#,
        )
        .unwrap();
        assert_eq!(request.request_id(), 7);
        assert!(serde_json::from_str::<RelayRequest>(
            r#"{"type":"authenticate","request_id":1,"relay_token":"secret","extra":true}"#
        )
        .is_err());
    }
}
