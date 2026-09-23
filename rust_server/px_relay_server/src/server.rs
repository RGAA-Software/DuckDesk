use crate::{
    config::RelayConfig,
    state::{relay_error, ConnectionHandle, Delivery, OutboundMessage, RelayRegistry},
};
use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        Query, State,
    },
    http::{HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    routing::get,
    Json, Router,
};
use futures_util::{SinkExt, StreamExt};
use prost::Message as ProstMessage;
use protocol::px_relay::{RelayCreateRoomRespMessage, RelayMessage, RelayMessageType};
use serde::{Deserialize, Serialize};
use std::{
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use subtle::ConstantTimeEq;
use tokio::sync::{mpsc, Mutex};
use uuid::Uuid;

#[derive(Clone)]
pub struct RelayServerState {
    config: RelayConfig,
    registry: Arc<Mutex<RelayRegistry>>,
    draining: Arc<AtomicBool>,
}

#[derive(Deserialize)]
struct RelayQuery {
    device_id: String,
    #[serde(default)]
    remote_device_id: String,
    #[serde(default, rename = "device_name")]
    _device_name: String,
    #[serde(default)]
    stream_id: String,
    appkey: String,
}

#[derive(Serialize)]
struct HealthResponse {
    status: &'static str,
    accepting_new_connections: bool,
    connections: usize,
    max_connections: usize,
    rooms: usize,
    max_rooms: usize,
    uploaded_payload_bytes: u64,
    forwarded_payload_bytes: u64,
    creator_to_remote_payload_bytes: u64,
    remote_to_creator_payload_bytes: u64,
    dropped_messages: u64,
}

#[derive(Deserialize)]
struct DrainingRequest {
    draining: bool,
}

#[derive(Serialize)]
struct DrainingResponse {
    status: &'static str,
    accepting_new_connections: bool,
}

pub fn router(config: RelayConfig) -> Router {
    Router::new()
        .route("/healthz", get(health))
        .route("/ping", get(ping))
        .route("/control/draining", axum::routing::post(set_draining))
        .route("/relay", get(websocket_upgrade))
        .with_state(RelayServerState {
            config,
            registry: Arc::new(Mutex::new(RelayRegistry::default())),
            draining: Arc::new(AtomicBool::new(false)),
        })
}

async fn ping() -> &'static str {
    "Pong"
}

async fn health(State(state): State<RelayServerState>) -> Json<HealthResponse> {
    let snapshot = state.registry.lock().await.snapshot();
    let accepting_new_connections = !state.draining.load(Ordering::Acquire);
    Json(HealthResponse {
        status: if accepting_new_connections {
            "ok"
        } else {
            "draining"
        },
        accepting_new_connections,
        connections: snapshot.connections,
        max_connections: state.config.max_connections,
        rooms: snapshot.rooms,
        max_rooms: state.config.max_rooms,
        uploaded_payload_bytes: snapshot.uploaded_payload_bytes,
        forwarded_payload_bytes: snapshot.forwarded_payload_bytes,
        creator_to_remote_payload_bytes: snapshot.creator_to_remote_payload_bytes,
        remote_to_creator_payload_bytes: snapshot.remote_to_creator_payload_bytes,
        dropped_messages: snapshot.dropped_messages,
    })
}

async fn set_draining(
    State(state): State<RelayServerState>,
    headers: HeaderMap,
    Json(request): Json<DrainingRequest>,
) -> Result<Json<DrainingResponse>, StatusCode> {
    if !authorized_control(&state.config, &headers) {
        return Err(StatusCode::UNAUTHORIZED);
    }
    state.draining.store(request.draining, Ordering::Release);
    Ok(Json(DrainingResponse {
        status: if request.draining { "draining" } else { "ok" },
        accepting_new_connections: !request.draining,
    }))
}

fn authorized_control(config: &RelayConfig, headers: &HeaderMap) -> bool {
    let Some(authorization) = headers
        .get(axum::http::header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
    else {
        return false;
    };
    config
        .control_key
        .as_slice()
        .ct_eq(authorization.as_bytes())
        .unwrap_u8()
        == 1
}

async fn websocket_upgrade(
    State(state): State<RelayServerState>,
    Query(query): Query<RelayQuery>,
    websocket: WebSocketUpgrade,
) -> Response {
    if state.draining.load(Ordering::Acquire) {
        return StatusCode::SERVICE_UNAVAILABLE.into_response();
    }
    if !valid_identity(&query.device_id)
        || (!query.remote_device_id.is_empty() && !valid_identity(&query.remote_device_id))
        || !authorized_admission(&state.config, &query)
    {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    websocket
        .max_message_size(state.config.max_message_bytes)
        .on_upgrade(move |socket| serve_connection(state, query, socket))
}

fn authorized_admission(config: &RelayConfig, query: &RelayQuery) -> bool {
    if config
        .app_key
        .as_slice()
        .ct_eq(query.appkey.as_bytes())
        .unwrap_u8()
        == 1
    {
        return true;
    }
    let Some(remote_resource_id) = frontend_remote_resource(query) else {
        return false;
    };
    let Ok(session_id) = Uuid::parse_str(&query.stream_id) else {
        return false;
    };
    let Ok(now_unix_seconds) = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|value| value.as_secs())
    else {
        return false;
    };
    px_relay_admission::verify(
        &config.app_key,
        &query.appkey,
        session_id,
        remote_resource_id,
        now_unix_seconds,
    )
}

fn frontend_remote_resource(query: &RelayQuery) -> Option<Uuid> {
    let remote_resource = if query.device_id.starts_with("client_") {
        query.remote_device_id.strip_prefix("server_")?
    } else if query.device_id.starts_with("ft_client_") {
        query.remote_device_id.strip_prefix("ft_server_")?
    } else {
        return None;
    };
    Uuid::parse_str(remote_resource)
        .ok()
        .filter(|value| !value.is_nil())
}

fn valid_identity(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 256
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'_' | b'-' | b'.' | b':'))
}

async fn serve_connection(state: RelayServerState, query: RelayQuery, socket: WebSocket) {
    let generation = Uuid::new_v4();
    let (mut websocket_sender, mut websocket_receiver) = socket.split();
    let (outbound_sender, mut outbound_receiver) =
        mpsc::channel::<OutboundMessage>(state.config.outbound_queue);
    let writer_state = state.clone();
    let writer = tokio::spawn(async move {
        while let Some(outbound) = outbound_receiver.recv().await {
            if websocket_sender.send(outbound.message).await.is_err() {
                break;
            }
            if let Some(accounting) = outbound.accounting {
                writer_state
                    .registry
                    .lock()
                    .await
                    .record_forwarded_payload(accounting);
            }
        }
    });
    let replacement_deliveries = {
        let mut registry = state.registry.lock().await;
        registry.register(
            query.device_id.clone(),
            ConnectionHandle {
                generation,
                sender: outbound_sender.clone(),
                authorized_remote_device_id: (!query.remote_device_id.is_empty())
                    .then_some(query.remote_device_id.clone()),
            },
            state.config.max_connections,
        )
    };
    let Ok(replacement_deliveries) = replacement_deliveries else {
        let _ = outbound_sender
            .send(OutboundMessage {
                message: Message::Close(None),
                accounting: None,
            })
            .await;
        let _ = writer.await;
        return;
    };
    deliver(&state, replacement_deliveries).await;

    loop {
        let message = match tokio::time::timeout(
            state.config.connection_idle_timeout,
            websocket_receiver.next(),
        )
        .await
        {
            Ok(Some(Ok(message))) => message,
            Ok(Some(Err(_))) | Ok(None) | Err(_) => break,
        };
        match message {
            Message::Binary(payload) => {
                if process_binary(
                    &state,
                    &query.device_id,
                    outbound_sender.clone(),
                    payload.to_vec(),
                )
                .await
                .is_err()
                {
                    break;
                }
            }
            Message::Ping(payload) => {
                if outbound_sender
                    .send(OutboundMessage {
                        message: Message::Pong(payload),
                        accounting: None,
                    })
                    .await
                    .is_err()
                {
                    break;
                }
            }
            Message::Close(_) => break,
            Message::Text(_) | Message::Pong(_) => {}
        }
    }

    let disconnect_deliveries = state
        .registry
        .lock()
        .await
        .disconnect(&query.device_id, generation);
    deliver(&state, disconnect_deliveries).await;
    drop(outbound_sender);
    let _ = tokio::time::timeout(Duration::from_secs(2), writer).await;
}

async fn process_binary(
    state: &RelayServerState,
    connection_device_id: &str,
    own_sender: mpsc::Sender<OutboundMessage>,
    encoded_message: Vec<u8>,
) -> Result<(), ()> {
    let decoded = RelayMessage::decode(encoded_message.as_slice()).map_err(|_| ())?;
    let message_type = decoded.r#type;
    let relay_type = RelayMessageType::try_from(message_type).map_err(|_| ())?;
    let result = match relay_type {
        RelayMessageType::KRelayHello | RelayMessageType::KRelayHeartBeat => {
            Ok(vec![Delivery::raw(own_sender.clone(), encoded_message)])
        }
        RelayMessageType::KRelayCreateRoom => {
            let Some(request) = decoded.create_room else {
                return Err(());
            };
            let created = state.registry.lock().await.create_room(
                connection_device_id,
                &request.device_id,
                &request.remote_device_id,
                &request.device_name,
                &request.stream_id,
                state.config.max_rooms,
            );
            created.map(|(room, sender)| {
                vec![Delivery::binary(
                    sender,
                    RelayMessage {
                        r#type: RelayMessageType::KRelayCreateRoomResp as i32,
                        create_room_resp: Some(RelayCreateRoomRespMessage {
                            device_id: room.creator_device_id,
                            remote_device_id: room.remote_device_id,
                            room_id: room.id,
                        }),
                        ..Default::default()
                    },
                )]
            })
        }
        RelayMessageType::KRelayRequestControl => {
            let Some(request) = decoded.request_control.as_ref() else {
                return Err(());
            };
            state
                .registry
                .lock()
                .await
                .forward_control(
                    connection_device_id,
                    &request.room_id,
                    &request.device_id,
                    &request.remote_device_id,
                    encoded_message,
                )
                .map(|delivery| vec![delivery])
        }
        RelayMessageType::KRelayRequestControlResp => {
            let Some(response) = decoded.request_control_resp.as_ref() else {
                return Err(());
            };
            state.registry.lock().await.accept_control_response(
                connection_device_id,
                &response.room_id,
                &response.device_id,
                &response.remote_device_id,
                encoded_message,
                response.under_control,
            )
        }
        RelayMessageType::KRelayTargetMessage => {
            let Some(target) = decoded.relay.as_ref() else {
                return Err(());
            };
            state.registry.lock().await.forward_payload(
                connection_device_id,
                &decoded.from_device_id,
                &target.room_ids,
                target.relay_msg_index,
                target.payload.len(),
                encoded_message,
            )
        }
        RelayMessageType::KRelayRequestPausedStream
        | RelayMessageType::KRelayRequestResumeStream => {
            let (room_id, device_id, remote_device_id) =
                if relay_type == RelayMessageType::KRelayRequestPausedStream {
                    let Some(request) = decoded.request_pause.as_ref() else {
                        return Err(());
                    };
                    (
                        &request.room_id,
                        &request.device_id,
                        &request.remote_device_id,
                    )
                } else {
                    let Some(request) = decoded.request_resume.as_ref() else {
                        return Err(());
                    };
                    (
                        &request.room_id,
                        &request.device_id,
                        &request.remote_device_id,
                    )
                };
            state
                .registry
                .lock()
                .await
                .forward_control(
                    connection_device_id,
                    room_id,
                    device_id,
                    remote_device_id,
                    encoded_message,
                )
                .map(|delivery| vec![delivery])
        }
        RelayMessageType::KRelayRequestStop => {
            let Some(request) = decoded.request_stop.as_ref() else {
                return Err(());
            };
            state.registry.lock().await.stop_room(
                connection_device_id,
                &request.room_id,
                &request.device_id,
                &request.remote_device_id,
            )
        }
        RelayMessageType::KRelayNotification
        | RelayMessageType::KRelayError
        | RelayMessageType::KRelayCreateRoomResp
        | RelayMessageType::KRelayRoomPrepared
        | RelayMessageType::KRelayRoomInfoChanged
        | RelayMessageType::KRelayRoomDestroyed
        | RelayMessageType::KRelayRequestStopResp
        | RelayMessageType::KRelayRemoteDeviceOffline => {
            Err(protocol::px_relay::RelayErrorCode::KRelayCodeRejectControl)
        }
    };

    match result {
        Ok(deliveries) => {
            deliver(state, deliveries).await;
            Ok(())
        }
        Err(code) => {
            state.registry.lock().await.mark_drop();
            let _ = own_sender
                .send(OutboundMessage {
                    message: Message::Binary(
                        relay_error(code, message_type).encode_to_vec().into(),
                    ),
                    accounting: None,
                })
                .await;
            Err(())
        }
    }
}

async fn deliver(state: &RelayServerState, deliveries: Vec<Delivery>) {
    for delivery in deliveries {
        if delivery.sender.try_send(delivery.outbound).is_err() {
            state.registry.lock().await.mark_drop();
        }
    }
}
