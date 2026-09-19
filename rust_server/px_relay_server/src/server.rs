use crate::{
    config::RelayConfig,
    state::{relay_error, ConnectionHandle, Delivery, RelayRegistry},
};
use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        Query, State,
    },
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::get,
    Json, Router,
};
use futures_util::{SinkExt, StreamExt};
use prost::Message as ProstMessage;
use protocol::px_relay::{RelayCreateRoomRespMessage, RelayMessage, RelayMessageType};
use serde::{Deserialize, Serialize};
use std::{sync::Arc, time::Duration};
use subtle::ConstantTimeEq;
use tokio::sync::{mpsc, Mutex};
use uuid::Uuid;

#[derive(Clone)]
pub struct RelayServerState {
    config: RelayConfig,
    registry: Arc<Mutex<RelayRegistry>>,
}

#[derive(Deserialize)]
struct RelayQuery {
    device_id: String,
    #[serde(default)]
    remote_device_id: String,
    #[serde(default, rename = "device_name")]
    _device_name: String,
    #[serde(default, rename = "stream_id")]
    _stream_id: String,
    appkey: String,
}

#[derive(Serialize)]
struct HealthResponse {
    status: &'static str,
    connections: usize,
    rooms: usize,
    uploaded_payload_bytes: u64,
    forwarded_payload_bytes: u64,
    creator_to_remote_payload_bytes: u64,
    remote_to_creator_payload_bytes: u64,
    dropped_messages: u64,
}

pub fn router(config: RelayConfig) -> Router {
    Router::new()
        .route("/healthz", get(health))
        .route("/ping", get(ping))
        .route("/relay", get(websocket_upgrade))
        .with_state(RelayServerState {
            config,
            registry: Arc::new(Mutex::new(RelayRegistry::default())),
        })
}

async fn ping() -> &'static str {
    "Pong"
}

async fn health(State(state): State<RelayServerState>) -> Json<HealthResponse> {
    let snapshot = state.registry.lock().await.snapshot();
    Json(HealthResponse {
        status: "ok",
        connections: snapshot.connections,
        rooms: snapshot.rooms,
        uploaded_payload_bytes: snapshot.uploaded_payload_bytes,
        forwarded_payload_bytes: snapshot.forwarded_payload_bytes,
        creator_to_remote_payload_bytes: snapshot.creator_to_remote_payload_bytes,
        remote_to_creator_payload_bytes: snapshot.remote_to_creator_payload_bytes,
        dropped_messages: snapshot.dropped_messages,
    })
}

async fn websocket_upgrade(
    State(state): State<RelayServerState>,
    Query(query): Query<RelayQuery>,
    websocket: WebSocketUpgrade,
) -> Response {
    if !valid_identity(&query.device_id)
        || (!query.remote_device_id.is_empty() && !valid_identity(&query.remote_device_id))
        || state
            .config
            .app_key
            .as_slice()
            .ct_eq(query.appkey.as_bytes())
            .unwrap_u8()
            != 1
    {
        return StatusCode::UNAUTHORIZED.into_response();
    }
    websocket
        .max_message_size(state.config.max_message_bytes)
        .on_upgrade(move |socket| serve_connection(state, query, socket))
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
    let (outbound_sender, mut outbound_receiver) = mpsc::channel(state.config.outbound_queue);
    let writer = tokio::spawn(async move {
        while let Some(message) = outbound_receiver.recv().await {
            if websocket_sender.send(message).await.is_err() {
                break;
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
        let _ = outbound_sender.send(Message::Close(None)).await;
        let _ = writer.await;
        return;
    };
    deliver(&state, replacement_deliveries).await;

    while let Some(incoming) = websocket_receiver.next().await {
        let Ok(message) = incoming else {
            break;
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
                if outbound_sender.send(Message::Pong(payload)).await.is_err() {
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
    own_sender: mpsc::Sender<Message>,
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
                .send(Message::Binary(
                    relay_error(code, message_type).encode_to_vec().into(),
                ))
                .await;
            Err(())
        }
    }
}

async fn deliver(state: &RelayServerState, deliveries: Vec<Delivery>) {
    for delivery in deliveries {
        if delivery.sender.try_send(delivery.message).is_err() {
            state.registry.lock().await.mark_drop();
        }
    }
}
