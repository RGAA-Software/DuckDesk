use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
#[serde(default)]
pub struct ConsoleRemoteSession {
    pub logical_session_id: String,
    pub takeover_previous_session_id: String,
    pub device_id: String,
    pub stream_id: String,
    pub subject_id: String,
    pub role: String,
    pub transports: Vec<String>,
    pub active: bool,
    pub opened_timestamp: i64,
    pub updated_timestamp: i64,
    pub closed_timestamp: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ConsoleRemoteSessionEvent {
    pub event_id: String,
    pub device_id: String,
    pub logical_session_id: String,
    /// For Takeover, identifies the other Controller session involved.
    pub related_session_id: String,
    pub event_type: String,
    pub previous_role: String,
    pub role: String,
    pub previous_transports: Vec<String>,
    pub transports: Vec<String>,
    pub timestamp: i64,
}

#[cfg(test)]
mod tests {
    use super::ConsoleRemoteSession;

    #[test]
    fn render_snapshot_uses_server_defaults_for_persistence_fields() {
        let sessions: Vec<ConsoleRemoteSession> = serde_json::from_str(
            r#"[{"logical_session_id":"session-1","stream_id":"stream-1","subject_id":"user-1","role":"controller","transports":["rtc_local"]}]"#,
        ).expect("Render logical session snapshot must deserialize");

        assert_eq!(sessions.len(), 1);
        assert_eq!(sessions[0].logical_session_id, "session-1");
        assert!(sessions[0].device_id.is_empty());
        assert!(!sessions[0].active);
        assert_eq!(sessions[0].opened_timestamp, 0);
    }
}
