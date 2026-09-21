use crate::{error::ApiError, request, StateData};
use axum::{
    extract::{
        ws::{Message, WebSocket},
        OriginalUri, State, WebSocketUpgrade,
    },
    http::{header, HeaderMap, Method},
    response::Response,
    routing::get,
    Router,
};
use chrono::{DateTime, Utc};
use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use std::{collections::VecDeque, sync::Arc, time::Duration};
use tokio::{sync::broadcast, time::timeout};
use uuid::Uuid;
use zeroize::Zeroizing;

const AUTHENTICATION_DEADLINE: Duration = Duration::from_secs(5);
const DATABASE_DEADLINE: Duration = Duration::from_secs(5);
const HEARTBEAT_INTERVAL: Duration = Duration::from_secs(15);
const WRITE_DEADLINE: Duration = Duration::from_secs(5);
const MAX_MESSAGE_BYTES: usize = 4 * 1024;
const EVENT_CAPACITY: usize = 1024;

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub(crate) struct ManagementEvent {
    stream_id: Uuid,
    sequence: u64,
    occurred_at: DateTime<Utc>,
    category: &'static str,
    resource_id: Option<Uuid>,
}

struct EventWindow {
    next_sequence: u64,
    events: VecDeque<ManagementEvent>,
}

pub(crate) struct ManagementEvents {
    stream_id: Uuid,
    window: std::sync::Mutex<EventWindow>,
    sender: broadcast::Sender<ManagementEvent>,
}

enum Replay {
    Events(Vec<ManagementEvent>),
    SnapshotRequired { latest_sequence: u64 },
}

impl ManagementEvents {
    pub(crate) fn new() -> Arc<Self> {
        let (sender, _) = broadcast::channel(EVENT_CAPACITY);
        Arc::new(Self {
            stream_id: Uuid::new_v4(),
            window: std::sync::Mutex::new(EventWindow {
                next_sequence: 1,
                events: VecDeque::with_capacity(EVENT_CAPACITY),
            }),
            sender,
        })
    }

    pub(crate) fn publish(&self, category: &'static str, resource_id: Option<Uuid>) {
        let event = {
            let mut window = self
                .window
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            let event = ManagementEvent {
                stream_id: self.stream_id,
                sequence: window.next_sequence,
                occurred_at: Utc::now(),
                category,
                resource_id,
            };
            window.next_sequence = window.next_sequence.saturating_add(1);
            if window.events.len() == EVENT_CAPACITY {
                window.events.pop_front();
            }
            window.events.push_back(event.clone());
            event
        };
        let _ = self.sender.send(event);
    }

    fn subscribe(&self) -> broadcast::Receiver<ManagementEvent> {
        self.sender.subscribe()
    }

    fn replay(&self, stream_id: Option<Uuid>, after: Option<u64>) -> Replay {
        let window = self
            .window
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let latest_sequence = window.next_sequence.saturating_sub(1);
        let (Some(stream_id), Some(after)) = (stream_id, after) else {
            return Replay::SnapshotRequired { latest_sequence };
        };
        let oldest_sequence = window
            .events
            .front()
            .map_or(window.next_sequence, |event| event.sequence);
        if stream_id != self.stream_id
            || after > latest_sequence
            || after.saturating_add(1) < oldest_sequence
        {
            return Replay::SnapshotRequired { latest_sequence };
        }
        Replay::Events(
            window
                .events
                .iter()
                .filter(|event| event.sequence > after)
                .cloned()
                .collect(),
        )
    }

    fn latest_sequence(&self) -> u64 {
        self.window
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .next_sequence
            .saturating_sub(1)
    }
}

#[derive(Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
enum ClientMessage {
    Authenticate {
        token: String,
        stream_id: Option<Uuid>,
        after: Option<u64>,
    },
}

#[derive(Serialize)]
#[serde(tag = "type", rename_all = "snake_case")]
enum ServerMessage<'a> {
    Ready {
        stream_id: Uuid,
        latest_sequence: u64,
        snapshot_required: bool,
    },
    Event {
        #[serde(flatten)]
        event: &'a ManagementEvent,
    },
    Heartbeat {
        stream_id: Uuid,
        latest_sequence: u64,
    },
    SnapshotRequired {
        stream_id: Uuid,
        latest_sequence: u64,
    },
}

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new().route("/api/console/managed/events", get(upgrade))
}

pub(crate) fn http_mutation(method: &Method, path: &str) -> Option<(&'static str, Option<Uuid>)> {
    if matches!(*method, Method::GET | Method::HEAD | Method::OPTIONS) {
        return None;
    }
    let suffix = path.strip_prefix("/api/console/")?;
    let category =
        if suffix.starts_with("managed/nodes") || suffix.starts_with("managed/telemetry-alerts") {
            "nodes"
        } else if suffix.starts_with("managed/devices") {
            "devices"
        } else if suffix.starts_with("managed/applications") {
            "applications"
        } else if suffix.starts_with("managed/deployments") {
            "deployments"
        } else if suffix.starts_with("managed/resource-sessions") {
            "sessions"
        } else if suffix.starts_with("managed/recordings")
            || suffix.starts_with("managed/recording-cache")
        {
            "recordings"
        } else if suffix.starts_with("managed/guests") {
            "guests"
        } else if suffix.starts_with("users") || suffix.starts_with("groups") {
            "identities"
        } else {
            return None;
        };
    let resource_id = suffix
        .split('/')
        .find_map(|segment| Uuid::parse_str(segment).ok());
    Some((category, resource_id))
}

async fn upgrade(
    State(state): State<Arc<StateData>>,
    OriginalUri(uri): OriginalUri,
    headers: HeaderMap,
    websocket: WebSocketUpgrade,
) -> Result<Response, ApiError> {
    if uri.query().is_some()
        || headers.contains_key(header::AUTHORIZATION)
        || headers.contains_key("x-pixels-client-type")
    {
        return Err(ApiError::Rejected);
    }
    state.policy.check_websocket(&headers)?;
    Ok(websocket
        .max_message_size(MAX_MESSAGE_BYTES)
        .max_frame_size(MAX_MESSAGE_BYTES)
        .on_upgrade(move |socket| session(socket, state)))
}

async fn session(mut socket: WebSocket, state: Arc<StateData>) {
    let Some(ClientMessage::Authenticate {
        token,
        stream_id,
        after,
    }) = receive_authentication(&mut socket, &state).await
    else {
        let _ = socket.close().await;
        return;
    };
    let token = Zeroizing::new(token);
    let Some(token_digest) = request::secret_digest(&token) else {
        let _ = socket.close().await;
        return;
    };
    if !authorized(&state, &token_digest).await {
        let _ = socket.close().await;
        return;
    }

    let mut receiver = state.management_events.subscribe();
    let mut latest_sequence = match state.management_events.replay(stream_id, after) {
        Replay::Events(events) => {
            let latest = state.management_events.latest_sequence();
            if send(
                &mut socket,
                &ServerMessage::Ready {
                    stream_id: state.management_events.stream_id,
                    latest_sequence: latest,
                    snapshot_required: false,
                },
            )
            .await
            .is_err()
            {
                return;
            }
            for event in events {
                if send(&mut socket, &ServerMessage::Event { event: &event })
                    .await
                    .is_err()
                {
                    return;
                }
            }
            latest
        }
        Replay::SnapshotRequired { latest_sequence } => {
            if send(
                &mut socket,
                &ServerMessage::Ready {
                    stream_id: state.management_events.stream_id,
                    latest_sequence,
                    snapshot_required: true,
                },
            )
            .await
            .is_err()
            {
                return;
            }
            latest_sequence
        }
    };

    let mut heartbeat = tokio::time::interval(HEARTBEAT_INTERVAL);
    heartbeat.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    heartbeat.tick().await;
    loop {
        tokio::select! {
            biased;
            _ = state.cancellation.cancelled() => break,
            incoming = socket.next() => match incoming {
                Some(Ok(Message::Ping(payload))) => {
                    if timeout(WRITE_DEADLINE, socket.send(Message::Pong(payload))).await.is_err() {
                        break;
                    }
                }
                Some(Ok(Message::Pong(_))) => {}
                Some(Ok(Message::Close(_))) | None | Some(Err(_)) => break,
                Some(Ok(Message::Text(_))) | Some(Ok(Message::Binary(_))) => break,
            },
            event = receiver.recv() => match event {
                Ok(event) => {
                    if event.sequence <= latest_sequence {
                        continue;
                    }
                    if !authorized(&state, &token_digest).await
                        || send(&mut socket, &ServerMessage::Event { event: &event }).await.is_err()
                    {
                        break;
                    }
                    latest_sequence = event.sequence;
                }
                Err(broadcast::error::RecvError::Lagged(_)) => {
                    latest_sequence = state.management_events.latest_sequence();
                    if send(
                        &mut socket,
                        &ServerMessage::SnapshotRequired {
                            stream_id: state.management_events.stream_id,
                            latest_sequence,
                        },
                    )
                    .await
                    .is_err()
                    {
                        break;
                    }
                    receiver = state.management_events.subscribe();
                }
                Err(broadcast::error::RecvError::Closed) => break,
            },
            _ = heartbeat.tick() => {
                if !authorized(&state, &token_digest).await
                    || send(
                        &mut socket,
                        &ServerMessage::Heartbeat {
                            stream_id: state.management_events.stream_id,
                            latest_sequence,
                        },
                    )
                    .await
                    .is_err()
                {
                    break;
                }
            }
        }
    }
    let _ = socket.close().await;
}

async fn authorized(state: &StateData, token: &px_console_store::TokenDigest) -> bool {
    state.active().is_ok()
        && matches!(
            timeout(DATABASE_DEADLINE, state.db.control().authorize_read(token)).await,
            Ok(Ok(()))
        )
}

async fn receive_authentication(
    socket: &mut WebSocket,
    state: &StateData,
) -> Option<ClientMessage> {
    loop {
        let message = tokio::select! {
            biased;
            _ = state.cancellation.cancelled() => return None,
            result = timeout(AUTHENTICATION_DEADLINE, socket.next()) => result.ok()??.ok()?,
        };
        match message {
            Message::Text(text) if text.len() <= MAX_MESSAGE_BYTES => {
                return serde_json::from_str(&text).ok()
            }
            Message::Ping(payload) => {
                if timeout(WRITE_DEADLINE, socket.send(Message::Pong(payload)))
                    .await
                    .is_err()
                {
                    return None;
                }
            }
            Message::Pong(_) => {}
            Message::Close(_) | Message::Text(_) | Message::Binary(_) => return None,
        }
    }
}

async fn send(socket: &mut WebSocket, message: &ServerMessage<'_>) -> Result<(), ()> {
    let encoded = serde_json::to_string(message).map_err(|_| ())?;
    timeout(WRITE_DEADLINE, socket.send(Message::Text(encoded.into())))
        .await
        .map_err(|_| ())?
        .map_err(|_| ())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn replay_is_ordered_and_requires_a_snapshot_outside_the_window() {
        let events = ManagementEvents::new();
        events.publish("nodes", None);
        events.publish("recordings", None);
        match events.replay(Some(events.stream_id), Some(0)) {
            Replay::Events(replayed) => {
                assert_eq!(replayed.len(), 2);
                assert_eq!(replayed[0].sequence, 1);
                assert_eq!(replayed[1].sequence, 2);
            }
            Replay::SnapshotRequired { .. } => panic!("same stream cursor must replay"),
        }
        assert!(matches!(
            events.replay(Some(Uuid::new_v4()), Some(0)),
            Replay::SnapshotRequired { latest_sequence: 2 }
        ));
        assert!(matches!(
            events.replay(Some(events.stream_id), Some(3)),
            Replay::SnapshotRequired { latest_sequence: 2 }
        ));
        assert!(matches!(
            events.replay(None, None),
            Replay::SnapshotRequired { latest_sequence: 2 }
        ));
    }

    #[test]
    fn successful_http_mutations_are_classified_without_treating_reads_as_events() {
        let resource_id = Uuid::new_v4();
        assert_eq!(
            http_mutation(
                &Method::PATCH,
                &format!("/api/console/managed/nodes/{resource_id}")
            ),
            Some(("nodes", Some(resource_id)))
        );
        assert_eq!(
            http_mutation(&Method::POST, "/api/console/groups"),
            Some(("identities", None))
        );
        assert_eq!(
            http_mutation(&Method::GET, "/api/console/managed/nodes"),
            None
        );
        assert_eq!(http_mutation(&Method::POST, "/api/console/sessions"), None);
    }

    #[test]
    fn high_frequency_overflow_requires_snapshot_and_retains_the_exact_window() {
        let events = ManagementEvents::new();
        for _ in 0..EVENT_CAPACITY + 2 {
            events.publish("nodes", None);
        }
        assert!(matches!(
            events.replay(Some(events.stream_id), Some(0)),
            Replay::SnapshotRequired {
                latest_sequence: 1026
            }
        ));
        match events.replay(Some(events.stream_id), Some(2)) {
            Replay::Events(replayed) => {
                assert_eq!(replayed.len(), EVENT_CAPACITY);
                assert_eq!(replayed.first().unwrap().sequence, 3);
                assert_eq!(replayed.last().unwrap().sequence, 1026);
            }
            Replay::SnapshotRequired { .. } => {
                panic!("the oldest retained cursor must remain replayable")
            }
        }
    }
}
