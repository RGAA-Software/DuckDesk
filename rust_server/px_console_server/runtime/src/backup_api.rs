use crate::{error::ApiError, request, StateData};
use axum::{
    extract::{
        ws::{Message, WebSocket},
        ConnectInfo, OriginalUri, State, WebSocketUpgrade,
    },
    http::{header, HeaderMap},
    response::Response,
    routing::get,
    Json, Router,
};
use futures_util::StreamExt;
use px_backup::{BackupDaemonStatus, BackupToConsole, ConsoleToBackup, MAX_CONTROL_MESSAGE_BYTES};
use px_console_store::ClientType;
use sha2::{Digest, Sha256};
use std::{
    collections::HashMap,
    net::SocketAddr,
    sync::Arc,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
use subtle::ConstantTimeEq;
use tokio::sync::{mpsc, oneshot, Mutex};
use uuid::Uuid;
use zeroize::Zeroizing;

const AUTHENTICATION_TIMEOUT: Duration = Duration::from_secs(5);
const TRIGGER_TIMEOUT: Duration = Duration::from_secs(10);

pub(crate) struct BackupControl {
    token_digest: [u8; 32],
    connection: Mutex<Option<BackupConnection>>,
    status: Mutex<Option<(BackupDaemonStatus, u64)>>,
}

struct BackupConnection {
    id: Uuid,
    sender: mpsc::Sender<TriggerCommand>,
}

struct TriggerCommand {
    task_id: Uuid,
    result: oneshot::Sender<Result<(), ApiError>>,
}

impl BackupControl {
    pub(crate) fn new(token: Zeroizing<String>) -> Result<Self, ApiError> {
        if token.len() != 64
            || !token
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
        {
            return Err(ApiError::Invalid);
        }
        Ok(Self {
            token_digest: Sha256::digest(token.as_bytes()).into(),
            connection: Mutex::new(None),
            status: Mutex::new(None),
        })
    }
}

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/backup-control", get(upgrade))
        .route("/api/console/managed/backup", get(status))
        .route(
            "/api/console/managed/backup/trigger",
            axum::routing::post(trigger),
        )
}

async fn administrator(state: &StateData, headers: &HeaderMap) -> Result<(), ApiError> {
    let token = request::administrator(state, headers)?;
    let profile = state
        .db
        .identity()
        .profile(&token, ClientType::AdminWeb)
        .await?;
    if profile.role != "admin" {
        return Err(ApiError::Rejected);
    }
    Ok(())
}

async fn status(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<Json<serde_json::Value>, ApiError> {
    state.operational()?;
    administrator(&state, &headers).await?;
    let control = state.backup_control.as_ref().ok_or(ApiError::Unavailable)?;
    let connected = control.connection.lock().await.is_some();
    let snapshot = control.status.lock().await.clone();
    Ok(Json(serde_json::json!({
        "connected":connected,
        "status":snapshot.as_ref().map(|(status, _)| status),
        "reported_at_unix":snapshot.map(|(_, reported_at)| reported_at),
    })))
}

async fn trigger(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<Json<serde_json::Value>, ApiError> {
    state.operational()?;
    administrator(&state, &headers).await?;
    let control = state.backup_control.as_ref().ok_or(ApiError::Unavailable)?;
    let sender = control
        .connection
        .lock()
        .await
        .as_ref()
        .map(|connection| connection.sender.clone())
        .ok_or(ApiError::Unavailable)?;
    let task_id = Uuid::new_v4();
    let (result_sender, result_receiver) = oneshot::channel();
    sender
        .send(TriggerCommand {
            task_id,
            result: result_sender,
        })
        .await
        .map_err(|_| ApiError::Unavailable)?;
    tokio::time::timeout(TRIGGER_TIMEOUT, result_receiver)
        .await
        .map_err(|_| ApiError::Unavailable)?
        .map_err(|_| ApiError::Unavailable)??;
    Ok(Json(serde_json::json!({"task_id":task_id})))
}

async fn upgrade(
    State(state): State<Arc<StateData>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    OriginalUri(uri): OriginalUri,
    headers: HeaderMap,
    websocket: WebSocketUpgrade,
) -> Result<Response, ApiError> {
    state.operational()?;
    if uri.query().is_some()
        || headers.contains_key(header::AUTHORIZATION)
        || headers.contains_key(header::ORIGIN)
        || headers.contains_key("x-pixels-client-type")
        || headers
            .keys()
            .any(|key| key.as_str() == "forwarded" || key.as_str().starts_with("x-forwarded-"))
    {
        return Err(ApiError::Rejected);
    }
    if state.backup_control.is_none() {
        return Err(ApiError::Unavailable);
    }
    if !state
        .node_limits
        .allow(&format!("backup-control:{}", peer.ip()), peer.ip())
    {
        return Err(ApiError::RateLimited);
    }
    let permit = state
        .backup_slots
        .clone()
        .try_acquire_owned()
        .map_err(|_| ApiError::Unavailable)?;
    Ok(websocket
        .max_message_size(MAX_CONTROL_MESSAGE_BYTES)
        .max_frame_size(MAX_CONTROL_MESSAGE_BYTES)
        .on_upgrade(move |socket| async move {
            let _permit = permit;
            session(socket, state).await;
        }))
}

async fn session(mut socket: WebSocket, state: Arc<StateData>) {
    let Some(control) = state.backup_control.as_ref() else {
        return;
    };
    let Ok(Some(Ok(Message::Text(authentication)))) =
        tokio::time::timeout(AUTHENTICATION_TIMEOUT, socket.next()).await
    else {
        return;
    };
    if authentication.len() > MAX_CONTROL_MESSAGE_BYTES {
        return;
    }
    let Ok(BackupToConsole::Authenticate {
        deployment_id,
        token,
    }) = serde_json::from_str::<BackupToConsole>(&authentication)
    else {
        return;
    };
    let digest: [u8; 32] = Sha256::digest(token.as_bytes()).into();
    if deployment_id != state.deployment
        || token.len() != 64
        || !bool::from(digest.ct_eq(&control.token_digest))
    {
        return;
    }
    if send(&mut socket, &ConsoleToBackup::Authenticated)
        .await
        .is_err()
    {
        return;
    }
    let connection_id = Uuid::new_v4();
    let (sender, mut receiver) = mpsc::channel::<TriggerCommand>(8);
    *control.connection.lock().await = Some(BackupConnection {
        id: connection_id,
        sender,
    });
    let mut pending = HashMap::<Uuid, oneshot::Sender<Result<(), ApiError>>>::new();
    loop {
        tokio::select! {
            Some(command) = receiver.recv() => {
                if state.operational().is_err() { break; }
                if send(&mut socket, &ConsoleToBackup::Trigger { task_id: command.task_id }).await.is_err() { break; }
                pending.insert(command.task_id, command.result);
            }
            incoming = socket.next() => {
                if state.operational().is_err() { break; }
                if control.connection.lock().await.as_ref().is_none_or(|current| current.id != connection_id) { break; }
                let Some(Ok(Message::Text(encoded))) = incoming else { break; };
                if encoded.len() > MAX_CONTROL_MESSAGE_BYTES { break; }
                match serde_json::from_str::<BackupToConsole>(&encoded) {
                    Ok(BackupToConsole::Status { status }) if status.deployment_id == state.deployment => {
                        let reported_at = SystemTime::now().duration_since(UNIX_EPOCH).map(|duration| duration.as_secs()).unwrap_or(0);
                        *control.status.lock().await = Some((status, reported_at));
                    }
                    Ok(BackupToConsole::TriggerResult { task_id, accepted, code }) => {
                        let Some(response) = pending.remove(&task_id) else { break; };
                        let result = if accepted { Ok(()) } else if code.as_deref() == Some("busy") { Err(ApiError::Conflict) } else { Err(ApiError::Unavailable) };
                        let _ = response.send(result);
                    }
                    _ => break,
                }
            }
        }
    }
    let mut connection = control.connection.lock().await;
    if connection
        .as_ref()
        .is_some_and(|current| current.id == connection_id)
    {
        *connection = None;
    }
}

async fn send(socket: &mut WebSocket, response: &ConsoleToBackup) -> Result<(), ()> {
    let encoded = serde_json::to_string(response).map_err(|_| ())?;
    tokio::time::timeout(
        Duration::from_secs(5),
        socket.send(Message::Text(encoded.into())),
    )
    .await
    .map_err(|_| ())?
    .map_err(|_| ())
}

#[cfg(test)]
mod tests {
    use super::BackupControl;
    use zeroize::Zeroizing;

    #[test]
    fn control_requires_a_full_private_token() {
        assert!(BackupControl::new(Zeroizing::new("a".repeat(64))).is_ok());
        assert!(BackupControl::new(Zeroizing::new("short".to_string())).is_err());
        assert!(BackupControl::new(Zeroizing::new("A".repeat(64))).is_err());
    }
}
