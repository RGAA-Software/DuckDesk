use crate::{
    error::ApiError,
    request::{self, Input, Page, Params as Query, Route as Path},
    StateData,
};
use axum::{
    extract::{
        ws::{Message, WebSocket},
        ConnectInfo, OriginalUri, State, WebSocketUpgrade,
    },
    http::{header, HeaderMap, StatusCode},
    response::Response,
    routing::{get, patch},
    Json, Router,
};
use futures_util::{SinkExt, StreamExt};
use px_console_store::{
    RelayNodeConfiguration, RelayNodeConnection, RelayNodeReport, RelayNodeSpec,
};
use px_relay_control_protocol::{RelayRequest, RelayResponse, MAX_MESSAGE_BYTES};
use std::{net::SocketAddr, sync::Arc, time::Duration};
use tokio::time::timeout;
use zeroize::Zeroizing;

const AUTHENTICATION_DEADLINE: Duration = Duration::from_secs(5);
const DATABASE_DEADLINE: Duration = Duration::from_secs(5);
const IDLE_DEADLINE: Duration = Duration::from_secs(35);
const WRITE_DEADLINE: Duration = Duration::from_secs(5);

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/relay-control", get(upgrade))
        .route("/api/console/managed/relays", get(managed).post(create))
        .route("/api/console/managed/relays/{id}", patch(configure))
}

async fn create(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(spec): Input<RelayNodeSpec>,
) -> Result<(StatusCode, Json<serde_json::Value>), ApiError> {
    let administrator = request::administrator(&state, &headers)?;
    let (relay_token, credential) = request::mint();
    let relay = state
        .db
        .relay_nodes()
        .create(&administrator, &spec, &credential)
        .await?;
    Ok((
        StatusCode::CREATED,
        Json(serde_json::json!({"relay":relay,"relay_token":relay_token.as_str()})),
    ))
}

async fn managed(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<serde_json::Value>, ApiError> {
    Ok(Json(serde_json::json!(
        state
            .db
            .relay_nodes()
            .list_managed(
                &request::administrator(&state, &headers)?,
                page.after,
                page.limit,
            )
            .await?
    )))
}

#[derive(serde::Deserialize)]
#[serde(deny_unknown_fields)]
struct RelayNodeChange {
    revision: i64,
    configuration: RelayNodeConfiguration,
}

async fn configure(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(relay_node_id): Path<uuid::Uuid>,
    Input(change): Input<RelayNodeChange>,
) -> Result<Json<serde_json::Value>, ApiError> {
    Ok(Json(serde_json::json!(
        state
            .db
            .relay_nodes()
            .configure(
                &request::administrator(&state, &headers)?,
                relay_node_id,
                change.revision,
                change.configuration,
            )
            .await?
    )))
}

async fn upgrade(
    State(state): State<Arc<StateData>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    OriginalUri(uri): OriginalUri,
    headers: HeaderMap,
    websocket: WebSocketUpgrade,
) -> Result<Response, ApiError> {
    reject_headers(&headers, uri.query().is_some())?;
    if !state
        .node_limits
        .allow(&format!("relay-control:{}", peer.ip()), peer.ip())
    {
        return Err(ApiError::RateLimited);
    }
    let permit = state
        .relay_slots
        .clone()
        .try_acquire_owned()
        .map_err(|_| ApiError::Unavailable)?;
    let session_state = state.clone();
    Ok(websocket
        .max_message_size(MAX_MESSAGE_BYTES)
        .max_frame_size(MAX_MESSAGE_BYTES)
        .on_upgrade(move |socket| async move {
            let _permit = permit;
            session(socket, session_state).await;
        }))
}

fn reject_headers(headers: &HeaderMap, has_query: bool) -> Result<(), ApiError> {
    if has_query
        || headers.contains_key(header::AUTHORIZATION)
        || headers.contains_key(header::ORIGIN)
        || headers.contains_key("x-pixels-client-type")
        || headers
            .keys()
            .any(|key| key.as_str() == "forwarded" || key.as_str().starts_with("x-forwarded-"))
    {
        return Err(ApiError::Rejected);
    }
    Ok(())
}

async fn session(mut socket: WebSocket, state: Arc<StateData>) {
    let Some(RelayRequest::Authenticate {
        request_id,
        relay_token,
    }) = receive(&mut socket, &state, AUTHENTICATION_DEADLINE).await
    else {
        let _ = send(
            &mut socket,
            &RelayResponse::Error {
                request_id: None,
                code: "authentication_required".into(),
            },
        )
        .await;
        let _ = socket.close().await;
        return;
    };
    let token = Zeroizing::new(relay_token);
    let Some(credential) = request::secret_digest(&token) else {
        authentication_failed(&mut socket, request_id).await;
        return;
    };
    if request_id == 0 || state.active().is_err() {
        let _ = socket.close().await;
        return;
    }
    let (_, connection_key) = request::mint();
    let connection = match timeout(
        DATABASE_DEADLINE,
        state
            .db
            .relay_nodes()
            .open_connection(state.epoch, &credential, &connection_key),
    )
    .await
    {
        Ok(Ok(connection)) => connection,
        _ => {
            authentication_failed(&mut socket, request_id).await;
            return;
        }
    };
    if send(
        &mut socket,
        &RelayResponse::Authenticated {
            request_id,
            relay_node_id: connection.id(),
            generation: connection.generation(),
            control_epoch: connection.epoch().value(),
            desired_draining: connection.desired_draining(),
        },
    )
    .await
    .is_ok()
    {
        run_authenticated(&mut socket, &state, &connection, request_id).await;
    }
    let _ = timeout(
        DATABASE_DEADLINE,
        state.db.relay_nodes().close_connection(&connection),
    )
    .await;
    let _ = socket.close().await;
}

async fn authentication_failed(socket: &mut WebSocket, request_id: u64) {
    let _ = send(
        socket,
        &RelayResponse::Error {
            request_id: Some(request_id),
            code: "authentication_failed".into(),
        },
    )
    .await;
    let _ = socket.close().await;
}

async fn run_authenticated(
    socket: &mut WebSocket,
    state: &StateData,
    connection: &RelayNodeConnection,
    mut last_request_id: u64,
) {
    loop {
        let Some(message) = receive(socket, state, IDLE_DEADLINE).await else {
            return;
        };
        let request_id = message.request_id();
        if request_id == 0
            || request_id <= last_request_id
            || matches!(message, RelayRequest::Authenticate { .. })
        {
            let _ = send(
                socket,
                &RelayResponse::Error {
                    request_id: Some(request_id),
                    code: "invalid_sequence".into(),
                },
            )
            .await;
            return;
        }
        last_request_id = request_id;
        if state.active().is_err() {
            return;
        }
        let RelayRequest::Report { report, .. } = message else {
            return;
        };
        let stored = state
            .db
            .relay_nodes()
            .report(
                connection,
                &RelayNodeReport {
                    sequence: report.sequence,
                    product_version_code: report.product_version_code,
                    draining: report.draining,
                    max_connections: report.max_connections,
                    current_connections: report.current_connections,
                    max_rooms: report.max_rooms,
                    current_rooms: report.current_rooms,
                    uploaded_bytes: report.uploaded_bytes,
                    forwarded_bytes: report.forwarded_bytes,
                },
            )
            .await;
        let response = match stored {
            Ok(profile) => RelayResponse::Reported {
                request_id,
                desired_draining: profile.desired_draining,
            },
            Err(error) => RelayResponse::Error {
                request_id: Some(request_id),
                code: match ApiError::from(error) {
                    ApiError::Invalid => "invalid_input",
                    ApiError::Rejected => "rejected",
                    ApiError::Conflict => "conflict",
                    ApiError::Unavailable => "unavailable",
                    _ => "internal",
                }
                .into(),
            },
        };
        if send(socket, &response).await.is_err() {
            return;
        }
    }
}

async fn receive(
    socket: &mut WebSocket,
    state: &StateData,
    deadline: Duration,
) -> Option<RelayRequest> {
    loop {
        let message = tokio::select! {
            biased;
            _ = state.cancellation.cancelled() => return None,
            result = timeout(deadline, socket.next()) => result.ok()??.ok()?,
        };
        match message {
            Message::Text(text) if text.len() <= MAX_MESSAGE_BYTES => {
                return serde_json::from_str(&text).ok();
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
            Message::Close(_) => return None,
            Message::Text(_) | Message::Binary(_) => return None,
        }
    }
}

async fn send(socket: &mut WebSocket, response: &RelayResponse) -> Result<(), ()> {
    let encoded = serde_json::to_string(response).map_err(|_| ())?;
    if encoded.len() > MAX_MESSAGE_BYTES {
        return Err(());
    }
    timeout(WRITE_DEADLINE, socket.send(Message::Text(encoded.into())))
        .await
        .map_err(|_| ())?
        .map_err(|_| ())
}
