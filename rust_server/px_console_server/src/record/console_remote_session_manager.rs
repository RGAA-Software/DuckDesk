use crate::gConsoleDatabase;
use crate::record::console_remote_session::{ConsoleRemoteSession, ConsoleRemoteSessionEvent};
use mongodb::bson::doc;
use std::collections::{BTreeMap, HashSet};
use std::sync::Arc;

pub struct ConsoleRemoteSessionManager;

/// Convert Render's transient heartbeat payload into the Console persistence
/// contract.  A logical session is the unit of reporting, so a reconnecting
/// transport must never create a second record (or a second online user).
fn normalize_snapshot(
    device_id: &str,
    snapshot_json: &str,
    timestamp: i64,
) -> Result<Vec<ConsoleRemoteSession>, String> {
    let parsed: Vec<ConsoleRemoteSession> =
        serde_json::from_str(snapshot_json).map_err(|error| error.to_string())?;
    let mut normalized = BTreeMap::<String, ConsoleRemoteSession>::new();

    for mut current in parsed {
        if current.logical_session_id.is_empty() {
            continue;
        }
        current.device_id = device_id.to_string();
        current.active = true;
        current.updated_timestamp = timestamp;
        current.transports.sort();
        current.transports.dedup();

        if let Some(previous) = normalized.get_mut(&current.logical_session_id) {
            if previous.takeover_previous_session_id != current.takeover_previous_session_id
                || previous.stream_id != current.stream_id
                || previous.subject_id != current.subject_id
                || previous.role != current.role
            {
                return Err(format!(
                    "conflicting duplicate logical session {}",
                    current.logical_session_id
                ));
            }
            previous.transports.extend(current.transports);
            previous.transports.sort();
            previous.transports.dedup();
            continue;
        }

        normalized.insert(current.logical_session_id.clone(), current);
    }

    Ok(normalized.into_values().collect())
}

impl ConsoleRemoteSessionManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self)
    }

    pub async fn reconcile_snapshot(
        &self,
        device_id: String,
        snapshot_json: String,
        timestamp: i64,
    ) {
        let parsed = match normalize_snapshot(&device_id, &snapshot_json, timestamp) {
            Ok(value) => value,
            Err(error) => {
                tracing::warn!(%device_id, %error, "ignore invalid remote session snapshot");
                return;
            }
        };
        let sessions = gConsoleDatabase.lock().await.remote_session();
        let events = gConsoleDatabase.lock().await.remote_session_event();
        let sessions = sessions.lock().await;
        let events = events.lock().await;
        let mut seen = HashSet::new();
        for mut current in parsed {
            seen.insert(current.logical_session_id.clone());
            let prior = sessions
                .find_one(doc! { "logical_session_id": &current.logical_session_id })
                .await
                .ok()
                .flatten();
            if let Some(previous) = &prior {
                current.opened_timestamp = previous.opened_timestamp;
                if previous.role != current.role {
                    Self::append(&events, &current, "RoleChanged", previous, timestamp).await;
                }
                if previous.transports != current.transports {
                    Self::append(&events, &current, "TransportChanged", previous, timestamp).await;
                }
            } else {
                current.opened_timestamp = timestamp;
                Self::append(
                    &events,
                    &current,
                    "SessionOpened",
                    &ConsoleRemoteSession::default(),
                    timestamp,
                )
                .await;
                if !current.takeover_previous_session_id.is_empty() {
                    Self::append_with_related(
                        &events,
                        &current,
                        "Takeover",
                        &ConsoleRemoteSession::default(),
                        &current.takeover_previous_session_id,
                        timestamp,
                    )
                    .await;
                }
            }
            let _ = sessions
                .replace_one(
                    doc! { "logical_session_id": &current.logical_session_id },
                    current,
                )
                .upsert(true)
                .await;
        }
        let mut cursor = match sessions
            .find(doc! { "device_id": &device_id, "active": true })
            .await
        {
            Ok(v) => v,
            Err(_) => return,
        };
        use futures_util::StreamExt;
        while let Some(Ok(previous)) = cursor.next().await {
            if seen.contains(&previous.logical_session_id) {
                continue;
            }
            let mut closed = previous.clone();
            closed.active = false;
            closed.closed_timestamp = timestamp;
            closed.updated_timestamp = timestamp;
            Self::append(&events, &closed, "SessionClosed", &previous, timestamp).await;
            let _ = sessions
                .replace_one(
                    doc! { "logical_session_id": &closed.logical_session_id },
                    closed,
                )
                .await;
        }
    }

    async fn append(
        events: &mongodb::Collection<ConsoleRemoteSessionEvent>,
        current: &ConsoleRemoteSession,
        event_type: &str,
        previous: &ConsoleRemoteSession,
        timestamp: i64,
    ) {
        Self::append_with_related(events, current, event_type, previous, "", timestamp).await;
    }

    async fn append_with_related(
        events: &mongodb::Collection<ConsoleRemoteSessionEvent>,
        current: &ConsoleRemoteSession,
        event_type: &str,
        previous: &ConsoleRemoteSession,
        related_session_id: &str,
        timestamp: i64,
    ) {
        let event = ConsoleRemoteSessionEvent {
            event_id: format!(
                "{}:{}:{}:{}",
                current.device_id, current.logical_session_id, timestamp, event_type
            ),
            device_id: current.device_id.clone(),
            logical_session_id: current.logical_session_id.clone(),
            related_session_id: related_session_id.to_string(),
            event_type: event_type.to_string(),
            previous_role: previous.role.clone(),
            role: current.role.clone(),
            previous_transports: previous.transports.clone(),
            transports: current.transports.clone(),
            timestamp,
        };
        let _ = events.insert_one(event).await;
    }
}

#[cfg(test)]
mod tests {
    use super::normalize_snapshot;

    #[test]
    fn report_contract_counts_multiple_transports_as_one_logical_session() {
        let sessions = normalize_snapshot(
            "device-1",
            r#"[
                {"logical_session_id":"controller-1","stream_id":"stream-1","subject_id":"user-1","role":"controller","transports":["ws","udp"]},
                {"logical_session_id":"controller-1","stream_id":"stream-1","subject_id":"user-1","role":"controller","transports":["rtc","ws"]},
                {"logical_session_id":"observer-1","stream_id":"stream-2","subject_id":"user-2","role":"observer","transports":["rtc_local"]}
            ]"#,
            123,
        ).expect("matching duplicate session rows must normalize");

        assert_eq!(sessions.len(), 2);
        let controller = sessions
            .iter()
            .find(|session| session.logical_session_id == "controller-1")
            .expect("controller session must remain present");
        assert_eq!(controller.device_id, "device-1");
        assert!(controller.active);
        assert_eq!(controller.updated_timestamp, 123);
        assert_eq!(controller.transports, vec!["rtc", "udp", "ws"]);
        assert_eq!(
            sessions
                .iter()
                .filter(|session| session.role == "controller")
                .count(),
            1
        );
        assert_eq!(
            sessions
                .iter()
                .filter(|session| session.role == "observer")
                .count(),
            1
        );
    }

    #[test]
    fn report_contract_rejects_conflicting_duplicate_session_identity() {
        let error = normalize_snapshot(
            "device-1",
            r#"[
                {"logical_session_id":"session-1","stream_id":"stream-1","subject_id":"user-1","role":"controller","transports":["ws"]},
                {"logical_session_id":"session-1","stream_id":"stream-2","subject_id":"user-1","role":"controller","transports":["rtc"]}
            ]"#,
            123,
        ).expect_err("a conflicting logical-session identity must not be persisted");

        assert!(error.contains("conflicting duplicate logical session session-1"));
    }
}
