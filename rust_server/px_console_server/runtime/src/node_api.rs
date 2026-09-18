use crate::{
    error::ApiError,
    node_wire::{NodeRequest, NodeResponse, MAX_MESSAGE_BYTES},
    request::{self, Input, Page, Params as Query, Revision, Route as Path},
    StateData,
};
use axum::{
    extract::{
        ws::{Message, WebSocket},
        ConnectInfo, OriginalUri, State, WebSocketUpgrade,
    },
    http::{header, HeaderMap, StatusCode},
    response::Response,
    routing::{get, patch, post},
    Json, Router,
};
use chrono::{DateTime, Utc};
use futures_util::{SinkExt, StreamExt};
use px_console_store::{NodeConfiguration, NodeConnection, NodeProduct, TelemetryHistoryCursor};
use serde::Deserialize;
use serde_json::{json, Value};
use std::{net::SocketAddr, sync::Arc, time::Duration};
use tokio::time::timeout;
use uuid::Uuid;
use zeroize::Zeroizing;

const AUTHENTICATION_DEADLINE: Duration = Duration::from_secs(5);
const DATABASE_DEADLINE: Duration = Duration::from_secs(5);
const IDLE_DEADLINE: Duration = Duration::from_secs(35);
const WRITE_DEADLINE: Duration = Duration::from_secs(5);

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/node-control", get(upgrade))
        .route("/api/console/managed/nodes", get(managed).post(create))
        .route(
            "/api/console/managed/nodes/{id}",
            patch(configure).delete(remove),
        )
        .route(
            "/api/console/managed/nodes/{id}/telemetry",
            get(telemetry_history),
        )
        .route("/api/console/managed/nodes/{id}/credential", post(rotate))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct TelemetryPage {
    before_received_at: Option<DateTime<Utc>>,
    before_generation: Option<i64>,
    before_sequence: Option<i64>,
    limit: u32,
}

impl TelemetryPage {
    fn cursor(&self) -> Result<Option<TelemetryHistoryCursor>, ApiError> {
        match (
            self.before_received_at,
            self.before_generation,
            self.before_sequence,
        ) {
            (None, None, None) => Ok(None),
            (Some(received_at), Some(node_generation), Some(report_sequence)) => {
                Ok(Some(TelemetryHistoryCursor {
                    received_at,
                    node_generation,
                    report_sequence,
                }))
            }
            _ => Err(ApiError::Invalid),
        }
    }
}

async fn telemetry_history(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(node_id): Path<Uuid>,
    Query(page): Query<TelemetryPage>,
) -> Result<Json<Value>, ApiError> {
    let cursor = page.cursor()?;
    Ok(Json(json!(
        state
            .db
            .nodes()
            .list_telemetry_history(
                &request::administrator(&state, &headers)?,
                node_id,
                cursor,
                page.limit,
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
    reject_node_headers(&headers, uri.query().is_some())?;
    if !state
        .node_limits
        .allow(&format!("node-control:{}", peer.ip()), peer.ip())
    {
        return Err(ApiError::RateLimited);
    }
    let permit = state
        .node_slots
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

fn reject_node_headers(headers: &HeaderMap, has_query: bool) -> Result<(), ApiError> {
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
    let Some(NodeRequest::Authenticate {
        request_id,
        node_token,
    }) = receive(&mut socket, &state, AUTHENTICATION_DEADLINE).await
    else {
        let _ = send(
            &mut socket,
            &NodeResponse::Error {
                request_id: None,
                code: "authentication_required".into(),
            },
        )
        .await;
        let _ = socket.close().await;
        return;
    };
    let token = Zeroizing::new(node_token);
    let Some(credential) = request::secret_digest(&token) else {
        let _ = send(
            &mut socket,
            &NodeResponse::Error {
                request_id: Some(request_id),
                code: "authentication_failed".into(),
            },
        )
        .await;
        let _ = socket.close().await;
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
            .nodes()
            .open_connection(state.epoch, &credential, &connection_key),
    )
    .await
    {
        Ok(Ok(connection)) => connection,
        _ => {
            let _ = send(
                &mut socket,
                &NodeResponse::Error {
                    request_id: Some(request_id),
                    code: "authentication_failed".into(),
                },
            )
            .await;
            let _ = socket.close().await;
            return;
        }
    };
    if send(
        &mut socket,
        &NodeResponse::Authenticated {
            request_id,
            node_id: connection.id(),
            generation: connection.generation(),
            control_epoch: connection.epoch().value(),
        },
    )
    .await
    .is_ok()
    {
        run_authenticated(&mut socket, &state, &connection, request_id).await;
    }
    let _ = timeout(
        DATABASE_DEADLINE,
        state.db.nodes().close_connection(&connection),
    )
    .await;
    let _ = socket.close().await;
}

async fn run_authenticated(
    socket: &mut WebSocket,
    state: &StateData,
    connection: &NodeConnection,
    mut last_request_id: u64,
) {
    loop {
        let Some(message) = receive(socket, state, IDLE_DEADLINE).await else {
            return;
        };
        let request_id = message.request_id();
        if request_id == 0
            || request_id <= last_request_id
            || matches!(message, NodeRequest::Authenticate { .. })
        {
            let _ = send(
                socket,
                &NodeResponse::Error {
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
        let response = match operation(state, connection, message).await {
            Ok(response) => response,
            Err(error) => NodeResponse::Error {
                request_id: Some(request_id),
                code: error_code(error).into(),
            },
        };
        let failed = matches!(response, NodeResponse::Error { .. });
        if send(socket, &response).await.is_err() || failed {
            return;
        }
    }
}

async fn operation(
    state: &StateData,
    connection: &NodeConnection,
    message: NodeRequest,
) -> Result<NodeResponse, ApiError> {
    let request_id = message.request_id();
    let future = async {
        match message {
            NodeRequest::Report { report, .. } => {
                let node = state
                    .db
                    .nodes()
                    .report(connection, &crate::node_wire::report(report))
                    .await?;
                Ok(NodeResponse::Reported {
                    request_id,
                    state: node.state,
                    endpoint_revision: node.endpoint_revision,
                })
            }
            NodeRequest::BeginReconciliation { .. } => Ok(NodeResponse::ReconciliationStarted {
                request_id,
                challenge: crate::node_wire::challenge(
                    state
                        .db
                        .instances()
                        .begin_reconciliation(connection)
                        .await?,
                ),
            }),
            NodeRequest::Reconcile { inventory, .. } => {
                state
                    .db
                    .instances()
                    .reconcile(connection, &crate::node_wire::inventory(inventory))
                    .await?;
                Ok(NodeResponse::Reconciled { request_id })
            }
            NodeRequest::PollCommand { .. } => Ok(NodeResponse::Command {
                request_id,
                command: state
                    .db
                    .instances()
                    .next_command(connection)
                    .await?
                    .map(crate::node_wire::command)
                    .map(Box::new),
            }),
            NodeRequest::AcknowledgeCommand { receipt, .. } => {
                let instance = state
                    .db
                    .instances()
                    .acknowledge_command(connection, &crate::node_wire::receipt(receipt))
                    .await?;
                Ok(NodeResponse::CommandAcknowledged {
                    request_id,
                    state: instance.state,
                    revision: instance.revision,
                })
            }
            NodeRequest::ReportDeployment {
                deployment_id,
                observation,
                ..
            } => {
                state
                    .db
                    .deployments()
                    .report(
                        connection,
                        deployment_id,
                        &crate::node_wire::observation(observation),
                    )
                    .await?;
                Ok(NodeResponse::DeploymentReported { request_id })
            }
            NodeRequest::ListDeployments { after, limit, .. } => {
                let deployments = state
                    .db
                    .deployments()
                    .list_node(connection, after, limit)
                    .await?
                    .into_iter()
                    .map(crate::node_wire::assignment)
                    .collect();
                Ok(NodeResponse::Deployments {
                    request_id,
                    deployments,
                })
            }
            NodeRequest::ListFrontends { .. } => Ok(NodeResponse::Frontends {
                request_id,
                frontends: state
                    .db
                    .resource_sessions()
                    .list_node(connection)
                    .await?
                    .into_iter()
                    .map(crate::node_wire::expected_frontend)
                    .collect(),
            }),
            NodeRequest::AdmitFrontend {
                session_id,
                revision,
                frontend_token,
                ..
            } => {
                let token = Zeroizing::new(frontend_token);
                let digest = request::secret_digest(&token).ok_or(ApiError::Invalid)?;
                let grant = state
                    .db
                    .resource_sessions()
                    .admit_frontend(connection, session_id, revision, &digest)
                    .await?;
                Ok(NodeResponse::FrontendAdmitted {
                    request_id,
                    grant: crate::node_wire::frontend_grant(grant),
                })
            }
            NodeRequest::BeginFrontendRetirement { session_id, .. } => {
                let retirement = state
                    .db
                    .resource_sessions()
                    .begin_retirement(connection, session_id)
                    .await?;
                Ok(NodeResponse::FrontendRetirementStarted {
                    request_id,
                    retirement: crate::node_wire::frontend_retirement(retirement),
                })
            }
            NodeRequest::FinishFrontendRetirement {
                session_id,
                challenge_id,
                ..
            } => {
                let session = state
                    .db
                    .resource_sessions()
                    .finish_retirement(connection, session_id, challenge_id)
                    .await?;
                Ok(NodeResponse::FrontendRetired {
                    request_id,
                    session_id: session.id,
                    revision: session.revision,
                })
            }
            NodeRequest::OpenChannel { channel, .. } => {
                let channel = state
                    .db
                    .activity()
                    .open_channel(connection, &crate::node_wire::open_channel(channel))
                    .await?;
                Ok(NodeResponse::ChannelOpened {
                    request_id,
                    channel_id: channel.id,
                    state: channel.state,
                    sequence: channel.sequence,
                    revision: channel.revision,
                })
            }
            NodeRequest::ReportChannel {
                channel_id,
                progress,
                ..
            } => {
                let channel = state
                    .db
                    .activity()
                    .report_channel(
                        connection,
                        channel_id,
                        &crate::node_wire::channel_progress(progress),
                    )
                    .await?;
                Ok(NodeResponse::ChannelReported {
                    request_id,
                    channel_id: channel.id,
                    state: channel.state,
                    sequence: channel.sequence,
                    revision: channel.revision,
                })
            }
            NodeRequest::BeginFileTransfer { transfer, .. } => {
                let transfer = state
                    .db
                    .file_transfers()
                    .begin(connection, &crate::node_wire::begin_file_transfer(transfer))
                    .await?;
                Ok(NodeResponse::FileTransferStarted {
                    request_id,
                    transfer_id: transfer.id,
                    state: transfer.state,
                    sequence: transfer.sequence,
                    revision: transfer.revision,
                })
            }
            NodeRequest::ReportFileTransfer {
                transfer_id,
                progress,
                ..
            } => {
                let transfer = state
                    .db
                    .file_transfers()
                    .report(
                        connection,
                        transfer_id,
                        &crate::node_wire::transfer_progress(progress),
                    )
                    .await?;
                Ok(NodeResponse::FileTransferReported {
                    request_id,
                    transfer_id: transfer.id,
                    state: transfer.state,
                    sequence: transfer.sequence,
                    revision: transfer.revision,
                })
            }
            NodeRequest::ReportRecording { recording, .. } => {
                let recording = state
                    .db
                    .recordings()
                    .report(connection, &crate::node_wire::recording_report(recording))
                    .await?;
                Ok(NodeResponse::RecordingReported {
                    request_id,
                    recording_id: recording.id,
                    reported_present: recording.reported_present,
                    source_sequence: recording.source_sequence,
                    revision: recording.revision,
                })
            }
            NodeRequest::PollRecordingCache { after, limit, .. } => {
                let cache = state
                    .recording_cache
                    .as_ref()
                    .ok_or(ApiError::Unavailable)?;
                let attempts = state
                    .db
                    .recording_cache()
                    .pending(cache, connection, after, u32::from(limit))
                    .await?;
                let mut uploads = Vec::with_capacity(attempts.len());
                for attempt in attempts {
                    if let Some(upload) = state.uploads.issue(connection, attempt)? {
                        uploads.push(upload);
                    }
                }
                Ok(NodeResponse::RecordingCacheUploads {
                    request_id,
                    uploads,
                })
            }
            NodeRequest::Authenticate { .. } => Err(ApiError::Invalid),
        }
    };
    timeout(DATABASE_DEADLINE, future)
        .await
        .map_err(|_| ApiError::Unavailable)?
}

async fn receive(
    socket: &mut WebSocket,
    state: &StateData,
    deadline: Duration,
) -> Option<NodeRequest> {
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

async fn send(socket: &mut WebSocket, response: &NodeResponse) -> Result<(), ()> {
    let encoded = serde_json::to_string(response).map_err(|_| ())?;
    if encoded.len() > MAX_MESSAGE_BYTES {
        return Err(());
    }
    timeout(WRITE_DEADLINE, socket.send(Message::Text(encoded.into())))
        .await
        .map_err(|_| ())?
        .map_err(|_| ())
}

fn error_code(error: ApiError) -> &'static str {
    match error {
        ApiError::Invalid => "invalid_input",
        ApiError::Unauthorized => "unauthorized",
        ApiError::Rejected => "rejected",
        ApiError::NotFound => "not_found",
        ApiError::Conflict => "conflict",
        ApiError::RateLimited => "rate_limited",
        ApiError::Unavailable => "unavailable",
        ApiError::Internal => "internal",
    }
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NewNode {
    device_id: Uuid,
    product: NodeProduct,
    max_instances: u32,
}
async fn create(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(input): Input<NewNode>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    let token = request::administrator(&state, &headers)?;
    let (secret, digest) = request::mint();
    let node = state
        .db
        .nodes()
        .create(
            &token,
            input.device_id,
            input.product,
            &digest,
            input.max_instances,
        )
        .await?;
    Ok((
        StatusCode::CREATED,
        Json(json!({"node":node,"node_token":secret.as_str()})),
    ))
}
async fn managed(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .nodes()
            .list_managed_views(
                &request::administrator(&state, &headers)?,
                page.after,
                page.limit
            )
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeChange {
    revision: i64,
    configuration: NodeConfiguration,
}
async fn configure(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<NodeChange>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .nodes()
            .configure(
                &request::administrator(&state, &headers)?,
                id,
                input.revision,
                input.configuration
            )
            .await?
    )))
}
async fn remove(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Query(input): Query<Revision>,
) -> Result<StatusCode, ApiError> {
    state
        .db
        .nodes()
        .delete(
            &request::administrator(&state, &headers)?,
            id,
            input.revision,
        )
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
async fn rotate(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<Revision>,
) -> Result<Json<Value>, ApiError> {
    let token = request::administrator(&state, &headers)?;
    let (secret, digest) = request::mint();
    let node = state
        .db
        .nodes()
        .rotate_key(&token, id, input.revision, &digest)
        .await?;
    Ok(Json(json!({"node":node,"node_token":secret.as_str()})))
}
