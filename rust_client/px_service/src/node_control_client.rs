use std::collections::{HashMap, HashSet};
use std::sync::Arc;
use std::time::{Duration, Instant};

use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use chrono::Utc;
use futures_util::{SinkExt, StreamExt};
use px_node_protocol::{
    ApplicationLaunch, BeginFileTransfer, ChannelKind, ChannelProgress, CommandOutcome,
    CommandReceipt, DeploymentAssignment, DeploymentObservation, DeploymentPreparation,
    GpuReservation, NodeCommand, NodeCommandAction, NodeReport, NodeRequest, NodeResponse,
    ObservedRuntime, ObservedRuntimePhase, OpenChannel, PreparationFailure, PreparationState,
    RdpWorkspaceCredential, RecordingCacheUpload, RelayEndpoint, RuntimeInventory,
    TelemetryBackfillSample, TransferDirection, VideoCodec, MAX_MESSAGE_BYTES,
};
use service_core::{AppInstanceState, StartAppRequest};
use tokio::net::TcpStream;
use tokio::sync::{oneshot, Mutex};
use tokio::task::JoinSet;
use tokio::time::{sleep, timeout};
use tokio_tungstenite::tungstenite::protocol::WebSocketConfig;
use tokio_tungstenite::tungstenite::Message;
use tokio_tungstenite::{MaybeTlsStream, WebSocketStream};
use tokio_util::io::ReaderStream;
use tracing::{info, warn};
use uuid::Uuid;
use zeroize::Zeroize;

use crate::node_control_store::{
    FileTransferOutboxStore, NodeControlConfiguration, NodeControlStore, TelemetryBacklogStore,
};
use crate::product_descriptor::ProductDescriptor;
use crate::recording_inventory::RecordingInventory;
use crate::service_host::ServiceRuntime;

type NodeSocket = WebSocketStream<MaybeTlsStream<TcpStream>>;

const CONNECT_TIMEOUT: Duration = Duration::from_secs(8);
const EXCHANGE_TIMEOUT: Duration = Duration::from_secs(10);
const RECONNECT_DELAY: Duration = Duration::from_secs(2);
const CONFIGURATION_POLL: Duration = Duration::from_secs(5);
const COMMAND_POLL: Duration = Duration::from_secs(1);
const REPORT_INTERVAL: Duration = Duration::from_secs(15);
const FRONTEND_FAIL_CLOSED_LEASE: Duration = Duration::from_secs(30);
const FRONTEND_CHALLENGE_RENEWAL_MARGIN: chrono::TimeDelta = chrono::TimeDelta::seconds(5);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct NodeControlIdentity {
    pub node_id: Uuid,
    pub device_id: Uuid,
    pub generation: i64,
    pub control_epoch: i64,
}

struct NodeControlAuthentication {
    identity: NodeControlIdentity,
    relay: Option<RelayEndpoint>,
}

struct PendingFrontendRetirement {
    challenge_id: Uuid,
    deadline: chrono::DateTime<Utc>,
    render_name: Option<String>,
    fail_closed_at: Instant,
}

struct FrontendObservation {
    connected_renders: HashSet<String>,
    sessions_by_render: HashMap<String, HashSet<Uuid>>,
    render_by_session: HashMap<Uuid, String>,
    duplicate_sessions: HashSet<Uuid>,
}

struct ProtocolSession {
    next_request_id: u64,
    identity: Option<NodeControlIdentity>,
}

pub(crate) enum NodeControlOperation {
    AdmitFrontend {
        session_id: Uuid,
        revision: i64,
        frontend_token: zeroize::Zeroizing<String>,
        completion: oneshot::Sender<Result<px_node_protocol::FrontendGrant, String>>,
    },
    OpenChannel {
        source_id: Uuid,
        session_id: Uuid,
        kind: ChannelKind,
        completion: oneshot::Sender<Result<NodeChannelReceipt, String>>,
    },
    ReportChannel {
        channel_id: Uuid,
        progress: ChannelProgress,
        completion: oneshot::Sender<Result<NodeChannelReceipt, String>>,
    },
    BeginFileTransfer {
        transfer_request_id: Uuid,
        session_id: Uuid,
        direction: TransferDirection,
        file_name: String,
        total_bytes: u64,
        expected_sha256: Option<[u8; 32]>,
        completion: oneshot::Sender<Result<NodeFileTransferReceipt, String>>,
    },
    FileTransferReportsQueued,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct NodeChannelReceipt {
    pub channel_id: Uuid,
    pub state: String,
    pub sequence: i64,
    pub revision: i64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct NodeFileTransferReceipt {
    pub transfer_id: Uuid,
    pub state: String,
    pub sequence: i64,
    pub revision: i64,
}

impl ProtocolSession {
    fn new() -> Self {
        Self {
            next_request_id: 1,
            identity: None,
        }
    }

    fn request_id(&mut self) -> Result<u64, String> {
        let id = self.next_request_id;
        self.next_request_id = self
            .next_request_id
            .checked_add(1)
            .ok_or_else(|| "node-control request sequence exhausted".to_string())?;
        Ok(id)
    }

    fn authenticate(&mut self, token: &str) -> Result<NodeRequest, String> {
        Ok(NodeRequest::Authenticate {
            request_id: self.request_id()?,
            node_token: token.to_string(),
        })
    }

    fn accept_authentication(
        &mut self,
        expected_request_id: u64,
        response: NodeResponse,
    ) -> Result<NodeControlAuthentication, String> {
        match response {
            NodeResponse::Authenticated {
                request_id,
                node_id,
                device_id,
                generation,
                control_epoch,
                relay,
            } if request_id == expected_request_id
                && !node_id.is_nil()
                && !device_id.is_nil()
                && generation > 0
                && control_epoch > 0
                && relay.as_ref().is_none_or(valid_relay_endpoint) =>
            {
                let identity = NodeControlIdentity {
                    node_id,
                    device_id,
                    generation,
                    control_epoch,
                };
                self.identity = Some(identity);
                Ok(NodeControlAuthentication { identity, relay })
            }
            NodeResponse::Error { code, .. } => {
                Err(format!("node-control authentication rejected: {code}"))
            }
            _ => Err("unexpected node-control authentication response".into()),
        }
    }

    fn validate_command(&self, command: &NodeCommand) -> Result<(), String> {
        let identity = self
            .identity
            .ok_or_else(|| "node-control session is not authenticated".to_string())?;
        if command.node_generation != identity.generation
            || command.control_epoch != identity.control_epoch
            || command.instance_revision <= 0
            || command.application_revision <= 0
            || command.deployment_revision <= 0
            || command.endpoint_revision <= 0
            || command.lease_until <= Utc::now()
            || command.deadline <= Utc::now()
        {
            return Err("node command identity, revision or deadline is stale".into());
        }
        Ok(())
    }
}

fn valid_relay_endpoint(endpoint: &RelayEndpoint) -> bool {
    !endpoint.host.trim().is_empty()
        && endpoint.host.trim() == endpoint.host
        && !endpoint.host.contains(['/', '?', '#', '@'])
        && endpoint.port != 0
        && (16..=512).contains(&endpoint.app_key.len())
}

pub async fn node_control_loop(
    runtime: Arc<Mutex<ServiceRuntime>>,
    recording_inventory: Arc<std::sync::Mutex<RecordingInventory>>,
) -> Result<(), String> {
    let product = ProductDescriptor::load_for_current_executable()?;
    let (store, file_transfer_outbox, mut stop_rx, mut operations) = {
        let mut guard = runtime.lock().await;
        (
            NodeControlStore::new(guard.config.data_root.clone()),
            guard.file_transfer_outbox.clone(),
            guard.subscribe_stop(),
            guard
                .node_control_receiver
                .take()
                .ok_or_else(|| "node-control operation receiver was already taken".to_string())?,
        )
    };
    let telemetry_backlog =
        TelemetryBacklogStore::new(runtime.lock().await.config.data_root.clone());
    let mut last_offline_sample = std::time::Instant::now()
        .checked_sub(REPORT_INTERVAL)
        .unwrap_or_else(std::time::Instant::now);
    loop {
        let configuration = loop {
            match store.load()? {
                Some(configuration) => break configuration,
                None => {
                    tokio::select! {
                        _ = stop_rx.recv() => return Ok(()),
                        _ = sleep(CONFIGURATION_POLL) => {}
                    }
                }
            }
        };
        {
            let mut guard = runtime.lock().await;
            guard
                .config
                .node
                .set_access_host(configuration.public_host.clone())?;
        }
        let connection_context = NodeConnectionContext {
            configuration: &configuration,
            product: &product,
            recording_inventory: &recording_inventory,
            telemetry_backlog: &telemetry_backlog,
            file_transfer_outbox: &file_transfer_outbox,
        };
        let connection_result =
            run_connection(&runtime, &connection_context, &mut stop_rx, &mut operations).await;
        let mut runtime = runtime.lock().await;
        runtime.rdp_console_trusted = false;
        runtime.node_control_identity = None;
        runtime.node_control_relay = None;
        drop(runtime);
        match connection_result {
            Ok(ConnectionEnd::Stopped) => return Ok(()),
            Err(error) => {
                warn!(%error, "node-control connection ended");
                if last_offline_sample.elapsed() >= REPORT_INTERVAL {
                    if let Err(backlog_error) = telemetry_backlog.append(
                        tokio::task::spawn_blocking(crate::node_telemetry::sample)
                            .await
                            .unwrap_or_else(|_| crate::node_telemetry::unavailable()),
                    ) {
                        warn!(error = %backlog_error, "offline node telemetry could not be queued");
                    }
                    last_offline_sample = std::time::Instant::now();
                }
            }
        }
        tokio::select! {
            _ = stop_rx.recv() => return Ok(()),
            _ = sleep(RECONNECT_DELAY) => {}
        }
    }
}

enum ConnectionEnd {
    Stopped,
}

struct NodeConnectionContext<'a> {
    configuration: &'a NodeControlConfiguration,
    product: &'a ProductDescriptor,
    recording_inventory: &'a Arc<std::sync::Mutex<RecordingInventory>>,
    telemetry_backlog: &'a TelemetryBacklogStore,
    file_transfer_outbox: &'a Arc<std::sync::Mutex<FileTransferOutboxStore>>,
}

async fn run_connection(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    context: &NodeConnectionContext<'_>,
    stop_rx: &mut tokio::sync::broadcast::Receiver<()>,
    operations: &mut tokio::sync::mpsc::Receiver<NodeControlOperation>,
) -> Result<ConnectionEnd, String> {
    let configuration = context.configuration;
    let product = context.product;
    let recording_inventory = context.recording_inventory;
    let telemetry_backlog = context.telemetry_backlog;
    let file_transfer_outbox = context.file_transfer_outbox;
    let http = reqwest::Client::builder()
        .redirect(reqwest::redirect::Policy::none())
        .timeout(EXCHANGE_TIMEOUT)
        .build()
        .map_err(|_| "Console identity HTTP client cannot be created".to_string())?;
    let websocket = WebSocketConfig::default()
        .max_message_size(Some(MAX_MESSAGE_BYTES))
        .max_frame_size(Some(MAX_MESSAGE_BYTES));
    let connection = tokio::select! {
        _ = stop_rx.recv() => return Ok(ConnectionEnd::Stopped),
        result = timeout(
            CONNECT_TIMEOUT,
            tokio_tungstenite::connect_async_with_config(
                configuration.endpoint.as_str(),
                Some(websocket),
                false,
            ),
        ) => result,
    };
    let (mut socket, _) = connection
        .map_err(|_| "node-control connection timed out".to_string())?
        .map_err(|error| format!("node-control connection failed: {error}"))?;
    let mut session = ProtocolSession::new();
    let authentication = session.authenticate(&configuration.node_token)?;
    let authentication_id = authentication.request_id();
    let response = exchange(&mut socket, authentication).await?;
    let authentication = session.accept_authentication(authentication_id, response)?;
    {
        let mut runtime = runtime.lock().await;
        runtime.rdp_console_trusted = true;
        let relay_changed = runtime.node_control_relay != authentication.relay;
        runtime.node_control_identity = Some(authentication.identity);
        runtime.node_control_relay = authentication.relay.clone();
        if relay_changed && runtime.state.desktop_alive {
            if let Some(launch) = runtime.state.last_desktop_launch.clone() {
                runtime.restart_desktop(launch)?;
            }
        }
    }
    info!(
        node_id = %authentication.identity.node_id,
        device_id = %authentication.identity.device_id,
        generation = authentication.identity.generation,
        control_epoch = authentication.identity.control_epoch,
        relay_configured = authentication.relay.is_some(),
        "node-control authenticated"
    );

    let report_outcome = report(
        &mut socket,
        &mut session,
        runtime,
        configuration,
        product,
        1,
    )
    .await?;
    sync_telemetry_backlog(&mut socket, &mut session, telemetry_backlog, 4).await?;
    sync_file_transfer_reports(&mut socket, &mut session, file_transfer_outbox).await?;
    sync_deployments(
        &mut socket,
        &mut session,
        product,
        report_outcome.endpoint_revision,
        1,
    )
    .await?;
    reconcile(&mut socket, &mut session, runtime).await?;
    let mut pending_frontend_retirements = HashMap::new();
    synchronize_frontend_retirements(
        &mut socket,
        &mut session,
        runtime,
        &mut pending_frontend_retirements,
    )
    .await?;
    let inventory_for_connection = recording_inventory.clone();
    tokio::task::spawn_blocking(move || {
        inventory_for_connection
            .lock()
            .map_err(|_| "recording inventory lock is unavailable".to_string())?
            .begin_connection()
    })
    .await
    .map_err(|_| "recording inventory connection reset failed".to_string())??;
    sync_recordings(&mut socket, &mut session, recording_inventory).await?;

    let mut report_sequence = 1_u64;
    let mut reports = tokio::time::interval(REPORT_INTERVAL);
    reports.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    reports.tick().await;
    let mut command_polls = tokio::time::interval(COMMAND_POLL);
    command_polls.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    command_polls.tick().await;
    let console_endpoint = url::Url::parse(&configuration.endpoint)
        .map_err(|_| "node-control endpoint cannot be reused for recording upload".to_string())?;
    let mut upload_tasks = JoinSet::new();
    loop {
        tokio::select! {
            _ = stop_rx.recv() => {
                let _ = socket.close(None).await;
                return Ok(ConnectionEnd::Stopped);
            }
            _ = reports.tick() => {
                report_sequence = report_sequence.checked_add(1)
                    .ok_or_else(|| "node report sequence exhausted".to_string())?;
                ServiceRuntime::refresh_app_processes(runtime).await;
                let report_outcome = report(
                    &mut socket,
                    &mut session,
                    runtime,
                    configuration,
                    product,
                    report_sequence,
                ).await?;
                if report_outcome.state == "reconciling" {
                    reconcile(&mut socket, &mut session, runtime).await?;
                }
                sync_telemetry_backlog(&mut socket, &mut session, telemetry_backlog, 4).await?;
                sync_file_transfer_reports(&mut socket, &mut session, file_transfer_outbox).await?;
                sync_deployments(
                    &mut socket,
                    &mut session,
                    product,
                    report_outcome.endpoint_revision,
                    report_sequence,
                ).await?;
                sync_recordings(&mut socket, &mut session, recording_inventory).await?;
            }
            _ = command_polls.tick() => {
                let request = NodeRequest::PollCommand {
                    request_id: session.request_id()?,
                };
                let request_id = request.request_id();
                let response = exchange(&mut socket, request).await?;
                let command = match response {
                    NodeResponse::Command { request_id: actual, command }
                        if actual == request_id => command,
                    NodeResponse::Error { code, .. } => {
                        return Err(format!("node-control poll rejected: {code}"));
                    }
                    _ => return Err("unexpected node-control poll response".into()),
                };
                if let Some(command) = command {
                    let outcome = match session.validate_command(&command) {
                        Ok(()) => {
                            let rdp_workspace = if matches!(
                                &command.action,
                                NodeCommandAction::Start {
                                    launch: ApplicationLaunch::Rdp,
                                    ..
                                }
                            ) {
                                Some(fetch_rdp_workspace(&mut socket, &mut session, &command).await?)
                            } else {
                                None
                            };
                            let workspace_id = rdp_workspace.as_ref().map(|workspace| workspace.workspace_id);
                            let command_execution_io = CommandExecutionIo {
                                socket: &mut socket,
                                session: &mut session,
                                operations,
                                file_transfer_outbox,
                            };
                            let mut outcome = execute_command_while_servicing_operations(
                                command_execution_io,
                                runtime,
                                &command,
                                rdp_workspace,
                                authentication.identity,
                            )
                            .await?;
                            if matches!(outcome, CommandOutcome::Running { .. }) {
                                if let Some(workspace_id) = workspace_id {
                                    let windows_sid = {
                                        let guard = runtime.lock().await;
                                        guard.app_registry
                                            .get(&command.instance_id.to_string())
                                            .and_then(|record| record.rdp_windows_sid.clone())
                                    };
                                    let confirmed = match windows_sid {
                                        Some(windows_sid) => confirm_rdp_workspace(
                                            &mut socket,
                                            &mut session,
                                            &command,
                                            workspace_id,
                                            windows_sid,
                                        ).await.is_ok(),
                                        None => false,
                                    };
                                    if !confirmed {
                                        warn!(command_id = %command.id, "RDP workspace identity confirmation failed");
                                        if exact_launch_match(runtime, &command).await == Some(true) {
                                            let _ = ServiceRuntime::stop_app_instance(
                                                runtime,
                                                &command.instance_id.to_string(),
                                            ).await;
                                        }
                                        outcome = CommandOutcome::Unknown;
                                    }
                                }
                            }
                            outcome
                        }
                        Err(error) => {
                            warn!(command_id = %command.id, %error, "node command rejected locally");
                            CommandOutcome::Unknown
                        }
                    };
                    acknowledge(&mut socket, &mut session, &command, outcome).await?;
                }
                let uploads = poll_recording_uploads(&mut socket, &mut session).await?;
                for upload in uploads {
                    let upload_http = http.clone();
                    let upload_endpoint = console_endpoint.clone();
                    let upload_inventory = recording_inventory.clone();
                    upload_tasks.spawn(async move {
                        upload_recording(upload_http, upload_endpoint, upload_inventory, upload).await
                    });
                }
                synchronize_frontend_retirements(
                    &mut socket,
                    &mut session,
                    runtime,
                    &mut pending_frontend_retirements,
                ).await?;
            }
            operation = operations.recv() => {
                let Some(operation) = operation else {
                    return Err("node-control operation channel closed".into());
                };
                if matches!(operation, NodeControlOperation::FileTransferReportsQueued) {
                    sync_file_transfer_reports(&mut socket, &mut session, file_transfer_outbox).await?;
                } else {
                    execute_operation(&mut socket, &mut session, operation).await?;
                }
            }
            completed = upload_tasks.join_next(), if !upload_tasks.is_empty() => {
                match completed {
                    Some(Ok(Ok(()))) => {}
                    Some(Ok(Err(error))) => warn!(%error, "recording cache upload failed"),
                    Some(Err(error)) => warn!(%error, "recording cache upload task failed"),
                    None => {}
                }
            }
        }
    }
}

async fn sync_recordings(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    inventory: &Arc<std::sync::Mutex<RecordingInventory>>,
) -> Result<(), String> {
    let inventory_for_scan = inventory.clone();
    let reports = tokio::task::spawn_blocking(move || {
        inventory_for_scan
            .lock()
            .map_err(|_| "recording inventory lock is unavailable".to_string())?
            .scan()
    })
    .await
    .map_err(|_| "recording inventory scan task failed".to_string())??;
    for recording in reports.into_iter().take(32) {
        let source_id = recording.source_id;
        let sequence = recording.sequence;
        let request = NodeRequest::ReportRecording {
            request_id: session.request_id()?,
            recording,
        };
        let expected = request.request_id();
        match exchange(socket, request).await? {
            NodeResponse::RecordingReported {
                request_id,
                source_sequence,
                ..
            } if request_id == expected
                && source_sequence == i64::try_from(sequence).unwrap_or(-1) =>
            {
                let inventory_for_acknowledgement = inventory.clone();
                tokio::task::spawn_blocking(move || {
                    inventory_for_acknowledgement
                        .lock()
                        .map_err(|_| "recording inventory lock is unavailable".to_string())?
                        .acknowledge(source_id, sequence)
                })
                .await
                .map_err(|_| "recording inventory acknowledgement task failed".to_string())??;
            }
            NodeResponse::Error { code, .. } => {
                return Err(format!("recording report rejected: {code}"));
            }
            _ => return Err("unexpected recording report response".into()),
        }
    }
    Ok(())
}

async fn poll_recording_uploads(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
) -> Result<Vec<RecordingCacheUpload>, String> {
    let request = NodeRequest::PollRecordingCache {
        request_id: session.request_id()?,
        after: None,
        limit: 4,
    };
    let expected = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::RecordingCacheUploads {
            request_id,
            uploads,
        } if request_id == expected => Ok(uploads),
        NodeResponse::Error { code, .. } => Err(format!("recording cache poll rejected: {code}")),
        _ => Err("unexpected recording cache poll response".into()),
    }
}

async fn upload_recording(
    http: reqwest::Client,
    mut console_endpoint: url::Url,
    inventory: Arc<std::sync::Mutex<RecordingInventory>>,
    upload: RecordingCacheUpload,
) -> Result<(), String> {
    let expected_path = format!("/api/console/node-recording-cache/{}", upload.attempt_id);
    if upload.valid_for_ms == 0 || upload.upload_path != expected_path {
        return Err("recording upload capability is invalid".into());
    }
    console_endpoint
        .set_scheme(match console_endpoint.scheme() {
            "ws" => "http",
            "wss" => "https",
            _ => return Err("recording upload endpoint scheme is invalid".into()),
        })
        .map_err(|_| "recording upload endpoint scheme cannot be changed".to_string())?;
    console_endpoint.set_path(&upload.upload_path);
    console_endpoint.set_query(None);
    console_endpoint.set_fragment(None);
    let inventory_for_open = inventory.clone();
    let upload_source_id = upload.source_id;
    let upload_size = upload.size_bytes;
    let upload_sha256 = upload.source_sha256;
    let file = tokio::task::spawn_blocking(move || {
        inventory_for_open
            .lock()
            .map_err(|_| "recording inventory lock is unavailable".to_string())?
            .open_upload(upload_source_id, upload_size, upload_sha256)
    })
    .await
    .map_err(|_| "recording upload source open task failed".to_string())??;
    let file = tokio::fs::File::from_std(file);
    let stream = ReaderStream::with_capacity(file, 1024 * 1024);
    let upload_token = zeroize::Zeroizing::new(upload.upload_token);
    let response = http
        .put(console_endpoint)
        .header(
            reqwest::header::AUTHORIZATION,
            format!("Bearer {}", upload_token.as_str()),
        )
        .header(reqwest::header::CONTENT_TYPE, "application/octet-stream")
        .header(reqwest::header::CONTENT_LENGTH, upload_size)
        .body(reqwest::Body::wrap_stream(stream))
        .send()
        .await
        .map_err(|_| "recording upload request failed".to_string())?;
    if response.status() != reqwest::StatusCode::CREATED {
        return Err(format!(
            "recording upload was rejected with HTTP {}",
            response.status().as_u16()
        ));
    }
    Ok(())
}

async fn execute_operation(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    operation: NodeControlOperation,
) -> Result<(), String> {
    match operation {
        NodeControlOperation::AdmitFrontend {
            session_id,
            revision,
            frontend_token,
            completion,
        } => {
            let started_at = std::time::Instant::now();
            let request = NodeRequest::AdmitFrontend {
                request_id: session.request_id()?,
                session_id,
                revision,
                frontend_token: frontend_token.to_string(),
            };
            let expected = request.request_id();
            let (result, reset_connection) = match exchange(socket, request).await {
                Ok(NodeResponse::FrontendAdmitted {
                    request_id,
                    mut grant,
                }) if request_id == expected => {
                    let elapsed_ms =
                        u32::try_from(started_at.elapsed().as_millis()).unwrap_or(u32::MAX);
                    grant.valid_for_ms = grant.valid_for_ms.saturating_sub(elapsed_ms);
                    if grant.valid_for_ms == 0 {
                        (
                            Err("frontend admission lease expired in transit".into()),
                            false,
                        )
                    } else {
                        (Ok(grant), false)
                    }
                }
                Ok(NodeResponse::Error { code, .. }) => {
                    (Err(format!("frontend admission rejected: {code}")), false)
                }
                Ok(_) => (Err("unexpected frontend admission response".into()), true),
                Err(error) => (Err(error), true),
            };
            let _ = completion.send(result);
            if reset_connection {
                return Err("node-control protocol failed during frontend admission".into());
            }
            Ok(())
        }
        NodeControlOperation::OpenChannel {
            source_id,
            session_id,
            kind,
            completion,
        } => {
            let request = NodeRequest::OpenChannel {
                request_id: session.request_id()?,
                channel: OpenChannel {
                    source_id,
                    session_id,
                    kind,
                },
            };
            let expected = request.request_id();
            let (result, reset_connection) = match exchange(socket, request).await {
                Ok(NodeResponse::ChannelOpened {
                    request_id,
                    channel_id,
                    state,
                    sequence,
                    revision,
                }) if request_id == expected => (
                    Ok(NodeChannelReceipt {
                        channel_id,
                        state,
                        sequence,
                        revision,
                    }),
                    false,
                ),
                Ok(NodeResponse::Error { code, .. }) => (
                    Err(format!("resource channel open rejected: {code}")),
                    false,
                ),
                Ok(_) => (
                    Err("unexpected resource channel open response".into()),
                    true,
                ),
                Err(error) => (Err(error), true),
            };
            let _ = completion.send(result);
            if reset_connection {
                return Err("node-control protocol failed while opening resource channel".into());
            }
            Ok(())
        }
        NodeControlOperation::ReportChannel {
            channel_id,
            progress,
            completion,
        } => {
            let request = NodeRequest::ReportChannel {
                request_id: session.request_id()?,
                channel_id,
                progress,
            };
            let expected = request.request_id();
            let (result, reset_connection) = match exchange(socket, request).await {
                Ok(NodeResponse::ChannelReported {
                    request_id,
                    channel_id,
                    state,
                    sequence,
                    revision,
                }) if request_id == expected => (
                    Ok(NodeChannelReceipt {
                        channel_id,
                        state,
                        sequence,
                        revision,
                    }),
                    false,
                ),
                Ok(NodeResponse::Error { code, .. }) => (
                    Err(format!("resource channel report rejected: {code}")),
                    false,
                ),
                Ok(_) => (
                    Err("unexpected resource channel report response".into()),
                    true,
                ),
                Err(error) => (Err(error), true),
            };
            let _ = completion.send(result);
            if reset_connection {
                return Err("node-control protocol failed while reporting resource channel".into());
            }
            Ok(())
        }
        NodeControlOperation::BeginFileTransfer {
            transfer_request_id,
            session_id,
            direction,
            file_name,
            total_bytes,
            expected_sha256,
            completion,
        } => {
            let request = NodeRequest::BeginFileTransfer {
                request_id: session.request_id()?,
                transfer: BeginFileTransfer {
                    transfer_request_id,
                    session_id,
                    direction,
                    file_name,
                    total_bytes,
                    expected_sha256,
                },
            };
            let expected = request.request_id();
            let (result, reset_connection) = match exchange(socket, request).await {
                Ok(NodeResponse::FileTransferStarted {
                    request_id,
                    transfer_id,
                    state,
                    sequence,
                    revision,
                }) if request_id == expected => (
                    Ok(NodeFileTransferReceipt {
                        transfer_id,
                        state,
                        sequence,
                        revision,
                    }),
                    false,
                ),
                Ok(NodeResponse::Error { code, .. }) => {
                    (Err(format!("file transfer begin rejected: {code}")), false)
                }
                Ok(_) => (Err("unexpected file transfer begin response".into()), true),
                Err(error) => (Err(error), true),
            };
            let _ = completion.send(result);
            if reset_connection {
                return Err("node-control protocol failed while beginning file transfer".into());
            }
            Ok(())
        }
        NodeControlOperation::FileTransferReportsQueued => {
            Err("file transfer outbox notification was not intercepted".into())
        }
    }
}

async fn sync_file_transfer_reports(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    outbox: &Arc<std::sync::Mutex<FileTransferOutboxStore>>,
) -> Result<(), String> {
    let outbox_for_read = outbox.clone();
    let reports = tokio::task::spawn_blocking(move || {
        outbox_for_read
            .lock()
            .map_err(|_| "file transfer outbox lock is unavailable".to_string())?
            .pending(32)
    })
    .await
    .map_err(|_| "file transfer outbox read task failed".to_string())??;

    let mut rejected_transfers = std::collections::HashSet::new();
    for report in reports {
        let transfer_id = report.transfer_id;
        if rejected_transfers.contains(&transfer_id) {
            continue;
        }
        let sequence = report.sequence;
        let expected_sequence = i64::try_from(sequence)
            .map_err(|_| "file transfer outbox sequence is invalid".to_string())?;
        let request = NodeRequest::ReportFileTransfer {
            request_id: session.request_id()?,
            transfer_id,
            progress: report.progress(),
        };
        let expected_request_id = request.request_id();
        match exchange(socket, request).await? {
            NodeResponse::FileTransferReported {
                request_id,
                transfer_id: confirmed_transfer_id,
                sequence: confirmed_sequence,
                ..
            } if request_id == expected_request_id
                && confirmed_transfer_id == transfer_id
                && confirmed_sequence == expected_sequence =>
            {
                let outbox_for_acknowledgement = outbox.clone();
                tokio::task::spawn_blocking(move || {
                    outbox_for_acknowledgement
                        .lock()
                        .map_err(|_| "file transfer outbox lock is unavailable".to_string())?
                        .acknowledge(transfer_id, sequence)
                })
                .await
                .map_err(|_| "file transfer outbox acknowledgement task failed".to_string())??;
            }
            NodeResponse::Error { code, .. } => {
                let outbox_for_rejection = outbox.clone();
                let removed = tokio::task::spawn_blocking(move || {
                    outbox_for_rejection
                        .lock()
                        .map_err(|_| "file transfer outbox lock is unavailable".to_string())?
                        .reject_transfer(transfer_id)
                })
                .await
                .map_err(|_| "file transfer outbox rejection task failed".to_string())??;
                rejected_transfers.insert(transfer_id);
                warn!(%transfer_id, %code, removed, "file transfer outbox reports rejected by Console");
            }
            _ => return Err("unexpected file transfer report response".into()),
        }
    }
    Ok(())
}

struct NodeReportOutcome {
    endpoint_revision: i64,
    state: String,
}

async fn report(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    runtime: &Arc<Mutex<ServiceRuntime>>,
    configuration: &NodeControlConfiguration,
    product: &ProductDescriptor,
    sequence: u64,
) -> Result<NodeReportOutcome, String> {
    let node = runtime.lock().await.config.node.clone();
    let telemetry = match tokio::task::spawn_blocking(crate::node_telemetry::sample).await {
        Ok(telemetry) => telemetry,
        Err(error) => {
            tracing::warn!(error = %error, "node telemetry worker failed");
            crate::node_telemetry::unavailable()
        }
    };
    let capability = |name: &str| product.capabilities.iter().any(|value| value == name);
    let rdp_configuration = if capability("rdp_host") {
        std::env::current_exe()
            .ok()
            .and_then(|executable| executable.parent().map(|directory| directory.join("rdp")))
            .and_then(|directory| {
                service_core::rdp_deployment::RdpDeployment::load(&directory).ok()
            })
            .and_then(|deployment| deployment.frontend_identity().ok())
    } else {
        None
    };
    let request = NodeRequest::Report {
        request_id: session.request_id()?,
        report: NodeReport {
            sequence,
            product_version_code: product.product_version_code,
            public_host: configuration.public_host.clone(),
            desktop_port: node.network.desktop_port,
            application_port_start: node.applications.port_start,
            application_port_end: node.applications.port_end,
            game_hook: capability("game_hook"),
            webview: capability("webview_host"),
            rdp: rdp_configuration.is_some(),
            rdp_domain: rdp_configuration.as_ref().map(|(domain, _)| domain.clone()),
            rdp_proxy_certificate_sha256: rdp_configuration.map(|(_, pin)| pin),
            telemetry,
        },
    };
    let expected = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::Reported {
            request_id,
            state,
            endpoint_revision,
        } if request_id == expected && endpoint_revision > 0 => Ok(NodeReportOutcome {
            endpoint_revision,
            state,
        }),
        NodeResponse::Error { code, .. } => Err(format!("node report rejected: {code}")),
        _ => Err("unexpected node report response".into()),
    }
}

async fn sync_telemetry_backlog(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    backlog: &TelemetryBacklogStore,
    maximum_batches: usize,
) -> Result<(), String> {
    for _ in 0..maximum_batches {
        let pending = match backlog.pending(4) {
            Ok(pending) => pending,
            Err(error) => {
                warn!(%error, "protected node telemetry backlog is unavailable");
                return Ok(());
            }
        };
        if pending.is_empty() {
            return Ok(());
        }
        let expected_sample_ids = pending
            .iter()
            .map(|sample| sample.sample_id)
            .collect::<Vec<_>>();
        let request = NodeRequest::ReportTelemetryBackfill {
            request_id: session.request_id()?,
            samples: pending
                .into_iter()
                .map(|sample| TelemetryBackfillSample {
                    sample_id: sample.sample_id,
                    telemetry: sample.telemetry,
                })
                .collect(),
        };
        let expected_request_id = request.request_id();
        match exchange(socket, request).await? {
            NodeResponse::TelemetryBackfilled {
                request_id,
                sample_ids,
            } if request_id == expected_request_id && sample_ids == expected_sample_ids => {
                if let Err(error) = backlog.acknowledge(&sample_ids) {
                    warn!(%error, "node telemetry acknowledgement could not be persisted");
                    return Ok(());
                }
            }
            NodeResponse::Error { code, .. } => {
                return Err(format!("node telemetry backfill rejected: {code}"));
            }
            _ => return Err("unexpected node telemetry backfill response".into()),
        }
    }
    Ok(())
}

async fn sync_deployments(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    product: &ProductDescriptor,
    endpoint_revision: i64,
    sequence: u64,
) -> Result<(), String> {
    let telemetry = crate::node_telemetry::sample();
    let mut after = None;
    loop {
        let request = NodeRequest::ListDeployments {
            request_id: session.request_id()?,
            after,
            limit: 50,
        };
        let expected = request.request_id();
        let deployments = match exchange(socket, request).await? {
            NodeResponse::Deployments {
                request_id,
                deployments,
            } if request_id == expected => deployments,
            NodeResponse::Error { code, .. } => {
                return Err(format!("node deployment listing rejected: {code}"));
            }
            _ => return Err("unexpected node deployment listing response".into()),
        };
        let count = deployments.len();
        for deployment in deployments {
            after = Some(deployment.id);
            let observation = DeploymentObservation {
                deployment_revision: deployment.deployment_revision,
                application_revision: deployment.application_revision,
                endpoint_revision,
                sequence,
                status: preparation_state(product, &deployment, &telemetry),
            };
            let request = NodeRequest::ReportDeployment {
                request_id: session.request_id()?,
                deployment_id: deployment.id,
                observation,
            };
            let expected = request.request_id();
            match exchange(socket, request).await? {
                NodeResponse::DeploymentReported { request_id } if request_id == expected => {}
                NodeResponse::Error { code, .. } => {
                    return Err(format!("node deployment report rejected: {code}"));
                }
                _ => return Err("unexpected node deployment report response".into()),
            }
        }
        if count < 50 {
            return Ok(());
        }
    }
}

fn preparation_state(
    product: &ProductDescriptor,
    deployment: &DeploymentAssignment,
    telemetry: &px_node_protocol::NodeTelemetry,
) -> PreparationState {
    if deployment.disabled {
        return PreparationState::Pending;
    }
    let capability = |name: &str| product.capabilities.iter().any(|value| value == name);
    match &deployment.preparation {
        DeploymentPreparation::GameHook {
            install_root,
            executable_relative,
            gpu_key,
        } => {
            if !capability("game_hook") {
                return PreparationState::Failed {
                    reason: PreparationFailure::UnsupportedMode,
                };
            }
            if !gpu_binding_is_available(telemetry, gpu_key.as_deref()) {
                return PreparationState::Failed {
                    reason: PreparationFailure::BindingUnverified,
                };
            }
            match service_core::resolve_game_path(install_root, executable_relative) {
                Ok(path) if path.is_file() => PreparationState::Ready,
                Ok(_) => PreparationState::Failed {
                    reason: PreparationFailure::MissingFiles,
                },
                Err(_) => PreparationState::Failed {
                    reason: PreparationFailure::InvalidConfiguration,
                },
            }
        }
        DeploymentPreparation::Webview { gpu_key } => {
            if !capability("webview_host") {
                PreparationState::Failed {
                    reason: PreparationFailure::UnsupportedMode,
                }
            } else if !gpu_binding_is_available(telemetry, gpu_key.as_deref()) {
                PreparationState::Failed {
                    reason: PreparationFailure::BindingUnverified,
                }
            } else {
                PreparationState::Ready
            }
        }
        DeploymentPreparation::Rdp { .. } => {
            if !capability("rdp_host") {
                return PreparationState::Failed {
                    reason: PreparationFailure::UnsupportedMode,
                };
            }
            let deployment = std::env::current_exe()
                .ok()
                .and_then(|executable| executable.parent().map(|directory| directory.join("rdp")))
                .and_then(|directory| {
                    service_core::rdp_deployment::RdpDeployment::load(&directory).ok()
                });
            if deployment.is_some() {
                PreparationState::Ready
            } else {
                PreparationState::Failed {
                    reason: PreparationFailure::MissingFiles,
                }
            }
        }
    }
}

fn gpu_binding_is_available(
    telemetry: &px_node_protocol::NodeTelemetry,
    requested_stable_key: Option<&str>,
) -> bool {
    if telemetry.gpu_inventory_revision.is_none() {
        return false;
    }
    telemetry
        .gpus
        .iter()
        .filter(|gpu| {
            gpu.runtime_binding_ready
                && requested_stable_key.is_none_or(|stable_key| gpu.stable_key == stable_key)
        })
        .count()
        == 1
}

async fn observe_frontends(
    runtime: &Arc<Mutex<ServiceRuntime>>,
) -> Result<FrontendObservation, String> {
    let guard = runtime.lock().await;
    let connected_renders = guard.render_senders.keys().cloned().collect::<HashSet<_>>();
    let mut sessions_by_render = HashMap::new();
    let mut render_by_session = HashMap::new();
    let mut duplicate_sessions = HashSet::new();
    for render_name in &connected_renders {
        let Some(snapshot) = guard.render_logical_sessions.get(render_name) else {
            continue;
        };
        let rows = serde_json::from_str::<Vec<serde_json::Value>>(snapshot).map_err(|error| {
            format!("invalid logical-session snapshot from {render_name}: {error}")
        })?;
        let mut session_ids = HashSet::new();
        for row in rows {
            let Some(session_id) = row
                .get("logical_session_id")
                .and_then(serde_json::Value::as_str)
                .and_then(|value| Uuid::parse_str(value).ok())
                .filter(|value| !value.is_nil())
            else {
                continue;
            };
            session_ids.insert(session_id);
            if render_by_session
                .insert(session_id, render_name.clone())
                .is_some()
            {
                duplicate_sessions.insert(session_id);
            }
        }
        sessions_by_render.insert(render_name.clone(), session_ids);
    }
    for duplicate_session in &duplicate_sessions {
        render_by_session.remove(duplicate_session);
    }
    Ok(FrontendObservation {
        connected_renders,
        sessions_by_render,
        render_by_session,
        duplicate_sessions,
    })
}

fn frontend_observation_drained(
    session_id: Uuid,
    retirement: &PendingFrontendRetirement,
    observation: &FrontendObservation,
) -> bool {
    if Instant::now() >= retirement.fail_closed_at {
        return true;
    }
    if observation.duplicate_sessions.contains(&session_id) {
        return false;
    }
    match retirement.render_name.as_ref() {
        Some(render_name) => {
            observation.connected_renders.contains(render_name)
                && observation
                    .sessions_by_render
                    .get(render_name)
                    .is_some_and(|session_ids| !session_ids.contains(&session_id))
        }
        None => Instant::now() >= retirement.fail_closed_at,
    }
}

async fn begin_frontend_retirement(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    session_id: Uuid,
    render_name: Option<String>,
    fail_closed_at: Instant,
) -> Result<Option<PendingFrontendRetirement>, String> {
    let request = NodeRequest::BeginFrontendRetirement {
        request_id: session.request_id()?,
        session_id,
    };
    let expected_request_id = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::FrontendRetirementStarted {
            request_id,
            retirement,
        } if request_id == expected_request_id
            && retirement.session_id == session_id
            && !retirement.challenge_id.is_nil()
            && retirement.reject_through_revision > 0
            && retirement.deadline > Utc::now() =>
        {
            Ok(Some(PendingFrontendRetirement {
                challenge_id: retirement.challenge_id,
                deadline: retirement.deadline,
                render_name,
                fail_closed_at,
            }))
        }
        NodeResponse::Error { code, .. } if code == "rejected" => Ok(None),
        NodeResponse::Error { code, .. } => {
            Err(format!("frontend retirement start failed: {code}"))
        }
        _ => Err("unexpected frontend retirement start response".into()),
    }
}

async fn finish_frontend_retirement(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    session_id: Uuid,
    challenge_id: Uuid,
) -> Result<bool, String> {
    let request = NodeRequest::FinishFrontendRetirement {
        request_id: session.request_id()?,
        session_id,
        challenge_id,
    };
    let expected_request_id = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::FrontendRetired {
            request_id,
            session_id: retired_session_id,
            revision,
        } if request_id == expected_request_id
            && retired_session_id == session_id
            && revision > 0 =>
        {
            Ok(true)
        }
        NodeResponse::Error { code, .. } if code == "rejected" => Ok(false),
        NodeResponse::Error { code, .. } => {
            Err(format!("frontend retirement completion failed: {code}"))
        }
        _ => Err("unexpected frontend retirement completion response".into()),
    }
}

async fn synchronize_frontend_retirements(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    runtime: &Arc<Mutex<ServiceRuntime>>,
    pending_retirements: &mut HashMap<Uuid, PendingFrontendRetirement>,
) -> Result<(), String> {
    let request = NodeRequest::ListFrontends {
        request_id: session.request_id()?,
    };
    let expected_request_id = request.request_id();
    let frontends = match exchange(socket, request).await? {
        NodeResponse::Frontends {
            request_id,
            frontends,
        } if request_id == expected_request_id => frontends,
        NodeResponse::Error { code, .. } => {
            return Err(format!("frontend inventory failed: {code}"));
        }
        _ => return Err("unexpected frontend inventory response".into()),
    };
    let retiring_ids = frontends
        .iter()
        .filter(|frontend| matches!(frontend.state.as_str(), "closing" | "reconcile_required"))
        .map(|frontend| frontend.id)
        .collect::<HashSet<_>>();
    pending_retirements.retain(|session_id, _| retiring_ids.contains(session_id));
    if retiring_ids.is_empty() {
        return Ok(());
    }

    let observation = observe_frontends(runtime).await?;
    for session_id in retiring_ids {
        let challenge_needs_refresh =
            pending_retirements
                .get(&session_id)
                .is_none_or(|retirement| {
                    retirement.deadline - Utc::now() <= FRONTEND_CHALLENGE_RENEWAL_MARGIN
                });
        if challenge_needs_refresh {
            let previous = pending_retirements.remove(&session_id);
            let render_name = previous
                .as_ref()
                .and_then(|retirement| retirement.render_name.clone())
                .or_else(|| observation.render_by_session.get(&session_id).cloned());
            let fail_closed_at = previous
                .map(|retirement| retirement.fail_closed_at)
                .unwrap_or_else(|| Instant::now() + FRONTEND_FAIL_CLOSED_LEASE);
            if let Some(retirement) =
                begin_frontend_retirement(socket, session, session_id, render_name, fail_closed_at)
                    .await?
            {
                pending_retirements.insert(session_id, retirement);
            }
        }

        let Some(retirement) = pending_retirements.get(&session_id) else {
            continue;
        };
        if !frontend_observation_drained(session_id, retirement, &observation) {
            continue;
        }
        let challenge_id = retirement.challenge_id;
        if finish_frontend_retirement(socket, session, session_id, challenge_id).await? {
            pending_retirements.remove(&session_id);
            info!(%session_id, "frontend retirement reached a durable closed state");
        }
    }
    Ok(())
}

async fn reconcile(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    runtime: &Arc<Mutex<ServiceRuntime>>,
) -> Result<(), String> {
    ServiceRuntime::refresh_app_processes(runtime).await;
    let begin = NodeRequest::BeginReconciliation {
        request_id: session.request_id()?,
    };
    let expected = begin.request_id();
    let challenge = match exchange(socket, begin).await? {
        NodeResponse::ReconciliationStarted {
            request_id,
            challenge,
        } if request_id == expected => challenge,
        NodeResponse::Error { code, .. } => {
            return Err(format!("node reconciliation start rejected: {code}"));
        }
        _ => return Err("unexpected reconciliation start response".into()),
    };
    let identity = session
        .identity
        .ok_or_else(|| "node-control session is not authenticated".to_string())?;
    if challenge.node_generation != identity.generation
        || challenge.control_epoch != identity.control_epoch
        || challenge.deadline <= Utc::now()
    {
        return Err("stale reconciliation challenge".into());
    }
    let runtimes = {
        let guard = runtime.lock().await;
        guard
            .app_registry
            .list()
            .into_iter()
            .filter(|record| record.is_active())
            .filter_map(|record| {
                Some(ObservedRuntime {
                    instance_id: Uuid::parse_str(&record.instance_id).ok()?,
                    launch_id: Uuid::parse_str(&record.request_id).ok()?,
                    port: record.listen_port,
                    phase: if record.state == AppInstanceState::Starting {
                        ObservedRuntimePhase::Starting
                    } else {
                        ObservedRuntimePhase::Running
                    },
                })
            })
            .collect()
    };
    let request = NodeRequest::Reconcile {
        request_id: session.request_id()?,
        inventory: RuntimeInventory {
            challenge_id: challenge.id,
            runtimes,
        },
    };
    let expected = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::Reconciled { request_id } if request_id == expected => Ok(()),
        NodeResponse::Error { code, .. } => Err(format!("node reconciliation rejected: {code}")),
        _ => Err("unexpected reconciliation response".into()),
    }
}

async fn fetch_rdp_workspace(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    command: &NodeCommand,
) -> Result<RdpWorkspaceCredential, String> {
    let request = NodeRequest::FetchRdpWorkspace {
        request_id: session.request_id()?,
        command_id: command.id,
        lease_id: command.lease_id,
    };
    let expected = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::RdpWorkspace {
            request_id,
            workspace,
        } if request_id == expected => Ok(workspace),
        NodeResponse::Error { code, .. } => {
            Err(format!("RDP workspace credential rejected: {code}"))
        }
        _ => Err("unexpected RDP workspace credential response".into()),
    }
}

async fn confirm_rdp_workspace(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    command: &NodeCommand,
    workspace_id: Uuid,
    windows_sid: String,
) -> Result<(), String> {
    let request = NodeRequest::ConfirmRdpWorkspace {
        request_id: session.request_id()?,
        command_id: command.id,
        lease_id: command.lease_id,
        workspace_id,
        windows_sid,
    };
    let expected = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::RdpWorkspaceConfirmed {
            request_id,
            workspace_id: confirmed_workspace,
        } if request_id == expected && confirmed_workspace == workspace_id => Ok(()),
        NodeResponse::Error { code, .. } => {
            Err(format!("RDP workspace confirmation rejected: {code}"))
        }
        _ => Err("unexpected RDP workspace confirmation response".into()),
    }
}

async fn execute_command(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    command: &NodeCommand,
    rdp_workspace: Option<RdpWorkspaceCredential>,
    identity: NodeControlIdentity,
) -> CommandOutcome {
    let deadline = std::cmp::min(command.lease_until, command.deadline);
    if deadline <= Utc::now() {
        return CommandOutcome::Unknown;
    }
    let outcome = execute_command_before_deadline(runtime, command, rdp_workspace, identity).await;
    if deadline > Utc::now() {
        return outcome;
    }
    warn!(command_id = %command.id, "node command execution exceeded its lease/deadline");
    if matches!(outcome, CommandOutcome::Running { .. }) {
        let instance_id = command.instance_id.to_string();
        if exact_launch_match(runtime, command).await == Some(true)
            && ServiceRuntime::stop_app_instance(runtime, &instance_id)
                .await
                .is_ok()
        {
            return CommandOutcome::Absent;
        }
    }
    CommandOutcome::Unknown
}

async fn execute_command_while_servicing_operations(
    command_execution_io: CommandExecutionIo<'_>,
    runtime: &Arc<Mutex<ServiceRuntime>>,
    command: &NodeCommand,
    rdp_workspace: Option<RdpWorkspaceCredential>,
    identity: NodeControlIdentity,
) -> Result<CommandOutcome, String> {
    let CommandExecutionIo {
        socket,
        session,
        operations,
        file_transfer_outbox,
    } = command_execution_io;
    let command_execution = execute_command(runtime, command, rdp_workspace, identity);
    tokio::pin!(command_execution);
    loop {
        tokio::select! {
            outcome = &mut command_execution => return Ok(outcome),
            operation = operations.recv() => {
                let Some(operation) = operation else {
                    return Err("node-control operation channel closed".into());
                };
                if matches!(operation, NodeControlOperation::FileTransferReportsQueued) {
                    sync_file_transfer_reports(socket, session, file_transfer_outbox).await?;
                } else {
                    execute_operation(socket, session, operation).await?;
                }
            }
        }
    }
}

struct CommandExecutionIo<'a> {
    socket: &'a mut NodeSocket,
    session: &'a mut ProtocolSession,
    operations: &'a mut tokio::sync::mpsc::Receiver<NodeControlOperation>,
    file_transfer_outbox: &'a Arc<std::sync::Mutex<FileTransferOutboxStore>>,
}

async fn execute_command_before_deadline(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    command: &NodeCommand,
    rdp_workspace: Option<RdpWorkspaceCredential>,
    identity: NodeControlIdentity,
) -> CommandOutcome {
    match &command.action {
        NodeCommandAction::Start {
            port,
            launch,
            install_root,
            gpu_reservation,
            relay,
        } => {
            let rdp = matches!(launch, ApplicationLaunch::Rdp);
            if rdp != rdp_workspace.is_some() {
                warn!(command_id = %command.id, "node RDP workspace envelope mismatch");
                return CommandOutcome::Absent;
            }
            if !rdp {
                let Some(gpu_reservation) = gpu_reservation else {
                    warn!(command_id = %command.id, "node start command has no GPU reservation");
                    return CommandOutcome::Absent;
                };
                if let Err(error) = validate_gpu_reservation(gpu_reservation) {
                    warn!(command_id = %command.id, %error, "node GPU admission rejected the start command");
                    return CommandOutcome::Absent;
                }
            }
            let existing = {
                let guard = runtime.lock().await;
                guard
                    .app_registry
                    .get(&command.instance_id.to_string())
                    .map(|record| {
                        (
                            record.request_id == command.launch_id.to_string(),
                            record.is_active(),
                            record.listen_port,
                        )
                    })
            };
            match existing {
                Some((true, true, existing_port)) if existing_port == *port => {
                    return CommandOutcome::Running {
                        port: existing_port,
                    };
                }
                Some((true, false, _)) => return CommandOutcome::Absent,
                Some(_) => return CommandOutcome::Unknown,
                None => {}
            }
            let request = match start_request(
                command,
                *port,
                launch,
                install_root.as_deref(),
                gpu_reservation.as_ref(),
                relay.as_ref(),
                StartSecurityContext {
                    rdp_workspace,
                    node_identity: identity,
                },
            ) {
                Ok(request) => request,
                Err(error) => {
                    warn!(command_id = %command.id, %error, "node start command is not executable");
                    return CommandOutcome::Absent;
                }
            };
            match ServiceRuntime::start_app_instance(runtime, request).await {
                Ok((actual_port, _)) if actual_port == *port => {
                    CommandOutcome::Running { port: actual_port }
                }
                Ok((actual_port, _)) => {
                    warn!(command_id = %command.id, actual_port, expected_port = *port, "Render started on an unexpected port");
                    if exact_launch_match(runtime, command).await == Some(true) {
                        let _ = ServiceRuntime::stop_app_instance(
                            runtime,
                            &command.instance_id.to_string(),
                        )
                        .await;
                    }
                    CommandOutcome::Unknown
                }
                Err(error) => {
                    warn!(command_id = %command.id, %error, "node start command failed");
                    CommandOutcome::Unknown
                }
            }
        }
        NodeCommandAction::Stop => {
            let instance_id = command.instance_id.to_string();
            let exact = exact_launch_match(runtime, command).await;
            match exact {
                None => CommandOutcome::Absent,
                Some(false) => CommandOutcome::Unknown,
                Some(true) => {
                    match ServiceRuntime::stop_app_instance(runtime, &instance_id).await {
                        Ok(()) => CommandOutcome::Absent,
                        Err(error) => {
                            warn!(command_id = %command.id, %error, "node stop command failed");
                            CommandOutcome::Unknown
                        }
                    }
                }
            }
        }
    }
}

fn validate_gpu_reservation(reservation: &GpuReservation) -> Result<(), String> {
    validate_gpu_reservation_against(&crate::node_telemetry::sample(), reservation)
}

fn validate_gpu_reservation_against(
    telemetry: &px_node_protocol::NodeTelemetry,
    reservation: &GpuReservation,
) -> Result<(), String> {
    if reservation.inventory_revision < 1
        || reservation.memory_bytes < 1
        || reservation.compute_per_mille < 1
        || reservation.encoder_per_mille < 1
        || reservation.memory_reserve_bytes < 0
        || reservation.compute_limit_per_mille < reservation.compute_per_mille
        || reservation.compute_limit_per_mille > 1000
        || reservation.encoder_limit_per_mille < reservation.encoder_per_mille
        || reservation.encoder_limit_per_mille > 1000
    {
        return Err("GPU reservation values are invalid".into());
    }
    if telemetry.gpu_inventory_revision != u64::try_from(reservation.inventory_revision).ok() {
        return Err("GPU inventory revision changed".into());
    }
    let mut matching_gpus = telemetry
        .gpus
        .iter()
        .filter(|gpu| gpu.stable_key == reservation.stable_key);
    let gpu = matching_gpus
        .next()
        .ok_or_else(|| "reserved GPU is no longer present".to_string())?;
    if matching_gpus.next().is_some() || !gpu.runtime_binding_ready {
        return Err("GPU binding cannot be proven for this inventory".into());
    }
    let total_memory = gpu
        .dedicated_memory_bytes
        .and_then(|value| i64::try_from(value).ok())
        .ok_or_else(|| "GPU memory capacity is unknown".to_string())?;
    let used_memory = gpu
        .used_memory_bytes
        .and_then(|value| i64::try_from(value).ok())
        .ok_or_else(|| "GPU memory use is unknown".to_string())?;
    let required_memory = used_memory
        .checked_add(reservation.memory_bytes)
        .and_then(|value| value.checked_add(reservation.memory_reserve_bytes))
        .ok_or_else(|| "GPU memory reservation overflowed".to_string())?;
    let projected_compute = i64::from(
        gpu.utilization_per_mille
            .ok_or_else(|| "GPU pressure is unknown".to_string())?,
    ) + i64::from(reservation.compute_per_mille);
    let projected_encoder = i64::from(
        gpu.encoder_utilization_per_mille
            .ok_or_else(|| "GPU encoder pressure is unknown".to_string())?,
    ) + i64::from(reservation.encoder_per_mille);
    if required_memory > total_memory
        || projected_compute > i64::from(reservation.compute_limit_per_mille)
        || projected_encoder > i64::from(reservation.encoder_limit_per_mille)
    {
        return Err("GPU reservation no longer fits current capacity".into());
    }
    Ok(())
}

async fn exact_launch_match(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    command: &NodeCommand,
) -> Option<bool> {
    let instance_id = command.instance_id.to_string();
    let launch_id = command.launch_id.to_string();
    let guard = runtime.lock().await;
    guard
        .app_registry
        .get(&instance_id)
        .map(|record| record.request_id == launch_id)
}

struct StartSecurityContext {
    rdp_workspace: Option<RdpWorkspaceCredential>,
    node_identity: NodeControlIdentity,
}

fn start_request(
    command: &NodeCommand,
    port: u16,
    launch: &ApplicationLaunch,
    install_root: Option<&str>,
    gpu_reservation: Option<&GpuReservation>,
    relay: Option<&px_node_protocol::RelayEndpoint>,
    security: StartSecurityContext,
) -> Result<StartAppRequest, String> {
    let (mode, executable, arguments, webview, bitrate, codec) = match launch {
        ApplicationLaunch::GameHook {
            executable_relative,
            arguments,
            video,
        } => (
            service_core::app_instance::APP_MODE_GAME_HOOK,
            executable_relative.clone(),
            arguments.clone(),
            String::new(),
            video.bitrate_kbps,
            video.codec,
        ),
        ApplicationLaunch::Webview { entry_url, video } => (
            service_core::app_instance::APP_MODE_WEBVIEW,
            String::new(),
            String::new(),
            URL_SAFE_NO_PAD.encode(entry_url),
            video.bitrate_kbps,
            video.codec,
        ),
        ApplicationLaunch::Rdp => (
            service_core::app_instance::APP_MODE_RDP,
            String::new(),
            String::new(),
            String::new(),
            1,
            VideoCodec::H264,
        ),
    };
    let install_root = match launch {
        ApplicationLaunch::GameHook { .. } => install_root
            .filter(|value| !value.is_empty())
            .ok_or_else(|| "game-hook install root is missing".to_string())?
            .to_string(),
        _ if install_root.is_some() => {
            return Err("non-game launch supplied an install root".into())
        }
        _ => String::new(),
    };
    let rdp_account =
        security
            .rdp_workspace
            .map(|workspace| service_core::rdp_account::RdpAccountSpec {
                workspace_id: workspace.workspace_id.to_string(),
                account_name: workspace.account_name,
                password: workspace.password,
                credential_version: workspace.credential_revision,
                expected_sid: workspace.expected_sid,
            });
    let rdp = matches!(launch, ApplicationLaunch::Rdp);
    if rdp != rdp_account.is_some() || rdp != gpu_reservation.is_none() {
        return Err("RDP workspace and GPU reservation boundary mismatch".into());
    }
    Ok(StartAppRequest {
        request_id: command.launch_id.to_string(),
        instance_id: command.instance_id.to_string(),
        app_id: command.application_id.to_string(),
        install_root,
        game_exe_rel: executable,
        game_arguments: arguments,
        listen_port: i32::from(port),
        encoder_fps: 60,
        encoder_bitrate_kbps: i32::try_from(bitrate)
            .map_err(|_| "video bitrate is outside the Render range".to_string())?,
        encoder_format: match codec {
            VideoCodec::H264 => "h264",
            VideoCodec::H265 => "h265",
        }
        .into(),
        webrtc_enabled: true,
        websocket_enabled: true,
        app_mode: mode.into(),
        webview_url_b64: webview,
        gpu_stable_key: gpu_reservation.map(|reservation| reservation.stable_key.clone()),
        rdp_node_id: if rdp {
            security.node_identity.node_id.to_string()
        } else {
            String::new()
        },
        rdp_account,
        device_id: if rdp {
            security.node_identity.device_id.to_string()
        } else {
            command.instance_id.to_string()
        },
        // Render owns the wire-level `server_` prefix. Keep the Service-to-Render
        // identity canonical so the prefix is applied exactly once.
        relay_device_id: relay
            .filter(|_| !rdp)
            .map(|_| command.instance_id.to_string())
            .unwrap_or_default(),
        relay_server_host: relay
            .filter(|_| !rdp)
            .map(|endpoint| endpoint.host.clone())
            .unwrap_or_default(),
        relay_server_port: relay
            .filter(|_| !rdp)
            .map(|endpoint| i32::from(endpoint.port))
            .unwrap_or_default(),
        relay_appkey: relay
            .filter(|_| !rdp)
            .map(|endpoint| endpoint.app_key.clone())
            .unwrap_or_default(),
    })
}

async fn acknowledge(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    command: &NodeCommand,
    outcome: CommandOutcome,
) -> Result<(), String> {
    let request = NodeRequest::AcknowledgeCommand {
        request_id: session.request_id()?,
        receipt: CommandReceipt {
            command_id: command.id,
            lease_id: command.lease_id,
            instance_id: command.instance_id,
            launch_id: command.launch_id,
            instance_revision: command.instance_revision,
            outcome,
        },
    };
    let expected = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::CommandAcknowledged { request_id, .. } if request_id == expected => Ok(()),
        NodeResponse::Error { code, .. } => Err(format!("node command receipt rejected: {code}")),
        _ => Err("unexpected node command receipt response".into()),
    }
}

async fn exchange(
    socket: &mut NodeSocket,
    mut request: NodeRequest,
) -> Result<NodeResponse, String> {
    let expected = request.request_id();
    let encoded = serde_json::to_string(&request)
        .map_err(|_| "cannot serialize node-control request".to_string())?;
    if encoded.len() > MAX_MESSAGE_BYTES {
        return Err("node-control request exceeds the wire limit".into());
    }
    let outbound = Message::Text(encoded.into());
    timeout(EXCHANGE_TIMEOUT, socket.send(outbound))
        .await
        .map_err(|_| "node-control write timed out".to_string())?
        .map_err(|error| format!("node-control write failed: {error}"))?;
    match &mut request {
        NodeRequest::Authenticate { node_token, .. } => node_token.zeroize(),
        NodeRequest::AdmitFrontend { frontend_token, .. } => frontend_token.zeroize(),
        _ => {}
    }
    let response = timeout(EXCHANGE_TIMEOUT, async {
        loop {
            match socket.next().await {
                Some(Ok(Message::Text(text))) if text.len() <= MAX_MESSAGE_BYTES => {
                    return serde_json::from_str::<NodeResponse>(&text)
                        .map_err(|_| "invalid node-control response".to_string());
                }
                Some(Ok(Message::Ping(payload))) => socket
                    .send(Message::Pong(payload))
                    .await
                    .map_err(|error| format!("node-control pong failed: {error}"))?,
                Some(Ok(Message::Pong(_))) => {}
                Some(Ok(Message::Close(_))) | None => {
                    return Err("node-control connection closed".to_string());
                }
                Some(Ok(_)) => return Err("unsupported node-control frame".to_string()),
                Some(Err(error)) => return Err(format!("node-control read failed: {error}")),
            }
        }
    })
    .await
    .map_err(|_| "node-control read timed out".to_string())??;
    if response.request_id() != Some(expected) {
        return Err("node-control response request_id mismatch".into());
    }
    Ok(response)
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::TimeDelta;
    use px_node_protocol::{TransferProgress, VideoSpec};

    #[test]
    fn frontend_retirement_requires_a_fresh_render_snapshot_or_full_lease_expiry() {
        let session_id = Uuid::new_v4();
        let render_name = "render_4613".to_string();
        let retirement = PendingFrontendRetirement {
            challenge_id: Uuid::new_v4(),
            deadline: Utc::now() + TimeDelta::seconds(30),
            render_name: Some(render_name.clone()),
            fail_closed_at: Instant::now() + Duration::from_secs(30),
        };
        let active = FrontendObservation {
            connected_renders: HashSet::from([render_name.clone()]),
            sessions_by_render: HashMap::from([(render_name.clone(), HashSet::from([session_id]))]),
            render_by_session: HashMap::from([(session_id, render_name.clone())]),
            duplicate_sessions: HashSet::new(),
        };
        assert!(!frontend_observation_drained(
            session_id,
            &retirement,
            &active
        ));

        let drained = FrontendObservation {
            connected_renders: HashSet::from([render_name.clone()]),
            sessions_by_render: HashMap::from([(render_name.clone(), HashSet::new())]),
            render_by_session: HashMap::new(),
            duplicate_sessions: HashSet::new(),
        };
        assert!(frontend_observation_drained(
            session_id,
            &retirement,
            &drained
        ));

        let disconnected = FrontendObservation {
            connected_renders: HashSet::new(),
            sessions_by_render: HashMap::new(),
            render_by_session: HashMap::new(),
            duplicate_sessions: HashSet::new(),
        };
        assert!(!frontend_observation_drained(
            session_id,
            &retirement,
            &disconnected
        ));

        let expired_retirement = PendingFrontendRetirement {
            challenge_id: Uuid::new_v4(),
            deadline: Utc::now() + TimeDelta::seconds(30),
            render_name: None,
            fail_closed_at: Instant::now()
                .checked_sub(Duration::from_secs(1))
                .expect("test clock must support one second of history"),
        };
        assert!(frontend_observation_drained(
            session_id,
            &expired_retirement,
            &disconnected
        ));
    }

    fn command(action: NodeCommandAction) -> NodeCommand {
        NodeCommand {
            id: Uuid::new_v4(),
            instance_id: Uuid::new_v4(),
            launch_id: Uuid::new_v4(),
            application_id: Uuid::new_v4(),
            deployment_id: Uuid::new_v4(),
            application_revision: 1,
            deployment_revision: 1,
            instance_revision: 1,
            node_generation: 2,
            control_epoch: 3,
            endpoint_revision: 1,
            lease_id: Uuid::new_v4(),
            lease_until: Utc::now() + TimeDelta::seconds(10),
            deadline: Utc::now() + TimeDelta::seconds(20),
            action,
        }
    }

    fn authenticated_session() -> ProtocolSession {
        let mut session = ProtocolSession::new();
        session.identity = Some(node_identity());
        session
    }

    fn node_identity() -> NodeControlIdentity {
        NodeControlIdentity {
            node_id: Uuid::new_v4(),
            device_id: Uuid::new_v4(),
            generation: 2,
            control_epoch: 3,
        }
    }

    fn cloud_product() -> ProductDescriptor {
        ProductDescriptor {
            schema_version: 3,
            product: "cloud_node".into(),
            distribution: "official".into(),
            release_namespace: Some("pixels.official".into()),
            oem_id: None,
            oem_profile_sha256: None,
            edition: "CLOUD_NODE".into(),
            company: "Pixels".into(),
            product_version: "3.3.67".into(),
            product_version_code: 30367,
            signer_certificate_sha256: Some("c".repeat(64)),
            capabilities: vec!["game_hook".into(), "webview_host".into()],
        }
    }

    fn gpu_reservation() -> GpuReservation {
        GpuReservation {
            stable_key: "gpu-1".into(),
            inventory_revision: 7,
            memory_bytes: 1024,
            compute_per_mille: 200,
            encoder_per_mille: 200,
            memory_reserve_bytes: 1024,
            compute_limit_per_mille: 800,
            encoder_limit_per_mille: 800,
        }
    }

    fn gpu_telemetry() -> px_node_protocol::NodeTelemetry {
        px_node_protocol::NodeTelemetry {
            sampled_at: Utc::now(),
            probe_state: px_node_protocol::TelemetryProbeState::Ready,
            logical_processors: Some(8),
            cpu_utilization_per_mille: Some(100),
            memory_total_bytes: Some(16_384),
            memory_available_bytes: Some(8_192),
            disk_total_bytes: Some(16_384),
            disk_free_bytes: Some(8_192),
            gpu_inventory_revision: Some(7),
            gpus: vec![px_node_protocol::NodeGpuTelemetry {
                stable_key: "gpu-1".into(),
                name: "Test GPU".into(),
                runtime_binding_ready: true,
                dedicated_memory_bytes: Some(8_192),
                used_memory_bytes: Some(2_048),
                utilization_per_mille: Some(200),
                encoder_utilization_per_mille: Some(100),
            }],
        }
    }

    #[test]
    fn gpu_reservation_rechecks_identity_revision_metrics_and_headroom() {
        let reservation = gpu_reservation();
        let telemetry = gpu_telemetry();
        assert!(validate_gpu_reservation_against(&telemetry, &reservation).is_ok());
        let mut changed_revision = telemetry.clone();
        changed_revision.gpu_inventory_revision = Some(8);
        assert!(validate_gpu_reservation_against(&changed_revision, &reservation).is_err());
        let mut unknown_encoder = telemetry.clone();
        unknown_encoder.gpus[0].encoder_utilization_per_mille = None;
        assert!(validate_gpu_reservation_against(&unknown_encoder, &reservation).is_err());
        let mut second_gpu = telemetry.clone();
        let mut additional_gpu = second_gpu.gpus[0].clone();
        additional_gpu.stable_key = "gpu-2".into();
        second_gpu.gpus.push(additional_gpu);
        assert!(validate_gpu_reservation_against(&second_gpu, &reservation).is_ok());
        second_gpu.gpus[1].stable_key = "gpu-1".into();
        assert!(validate_gpu_reservation_against(&second_gpu, &reservation).is_err());
    }

    #[test]
    fn request_ids_are_strictly_increasing_and_authentication_is_bound() {
        let mut session = ProtocolSession::new();
        let request = session.authenticate(&"a".repeat(64)).unwrap();
        assert_eq!(request.request_id(), 1);
        let node_id = Uuid::new_v4();
        let device_id = Uuid::new_v4();
        let identity = session
            .accept_authentication(
                1,
                NodeResponse::Authenticated {
                    request_id: 1,
                    node_id,
                    device_id,
                    generation: 4,
                    control_epoch: 5,
                    relay: None,
                },
            )
            .unwrap();
        assert_eq!(identity.identity.node_id, node_id);
        assert_eq!(identity.identity.device_id, device_id);
        assert!(identity.relay.is_none());
        assert_eq!(session.request_id().unwrap(), 2);
    }

    #[test]
    fn command_validation_rejects_generation_epoch_and_expired_leases() {
        let session = authenticated_session();
        session
            .validate_command(&command(NodeCommandAction::Stop))
            .unwrap();
        let mut wrong_generation = command(NodeCommandAction::Stop);
        wrong_generation.node_generation = 4;
        assert!(session.validate_command(&wrong_generation).is_err());
        let mut expired = command(NodeCommandAction::Stop);
        expired.lease_until = Utc::now() - TimeDelta::seconds(1);
        assert!(session.validate_command(&expired).is_err());
    }

    #[test]
    fn start_conversion_preserves_paths_arguments_and_video() {
        let command = command(NodeCommandAction::Start {
            port: 4613,
            launch: ApplicationLaunch::GameHook {
                executable_relative: "游戏 目录\\game.exe".into(),
                arguments: "--name \"two words\"".into(),
                video: VideoSpec {
                    codec: VideoCodec::H265,
                    bitrate_kbps: 24_000,
                },
            },
            install_root: Some("D:\\Cloud Games".into()),
            gpu_reservation: None,
            relay: None,
        });
        let NodeCommandAction::Start {
            port,
            launch,
            install_root,
            ..
        } = &command.action
        else {
            unreachable!();
        };
        let request = start_request(
            &command,
            *port,
            launch,
            install_root.as_deref(),
            Some(&gpu_reservation()),
            None,
            StartSecurityContext {
                rdp_workspace: None,
                node_identity: node_identity(),
            },
        )
        .unwrap();
        assert_eq!(request.request_id, command.launch_id.to_string());
        assert_eq!(request.game_exe_rel, "游戏 目录\\game.exe");
        assert_eq!(request.game_arguments, "--name \"two words\"");
        assert_eq!(request.encoder_format, "h265");
        assert_eq!(request.encoder_bitrate_kbps, 24_000);
        assert_eq!(request.listen_port, 4613);
        assert!(request.relay_server_host.is_empty());
    }

    #[test]
    fn webview_conversion_uses_url_safe_payload_and_rejects_install_root() {
        let command = command(NodeCommandAction::Start {
            port: 4614,
            launch: ApplicationLaunch::Webview {
                entry_url: "https://example.com/云应用".into(),
                video: VideoSpec {
                    codec: VideoCodec::H264,
                    bitrate_kbps: 8_000,
                },
            },
            install_root: None,
            gpu_reservation: None,
            relay: Some(px_node_protocol::RelayEndpoint {
                host: "relay.example.test".into(),
                port: 4605,
                app_key: "deployment-relay-key".into(),
            }),
        });
        let NodeCommandAction::Start {
            port,
            launch,
            relay,
            ..
        } = &command.action
        else {
            unreachable!();
        };
        let reservation = gpu_reservation();
        let request = start_request(
            &command,
            *port,
            launch,
            None,
            Some(&reservation),
            relay.as_ref(),
            StartSecurityContext {
                rdp_workspace: None,
                node_identity: node_identity(),
            },
        )
        .unwrap();
        assert_eq!(
            URL_SAFE_NO_PAD.decode(request.webview_url_b64).unwrap(),
            "https://example.com/云应用".as_bytes()
        );
        assert!(start_request(
            &command,
            *port,
            launch,
            Some("D:\\wrong"),
            Some(&reservation),
            relay.as_ref(),
            StartSecurityContext {
                rdp_workspace: None,
                node_identity: node_identity(),
            },
        )
        .is_err());
        assert_eq!(request.relay_device_id, command.instance_id.to_string());
        assert_eq!(request.relay_server_host, "relay.example.test");
        assert_eq!(request.relay_server_port, 4605);
        assert_eq!(request.relay_appkey, "deployment-relay-key");
    }

    #[test]
    fn rdp_conversion_requires_workspace_omits_gpu_and_uses_authenticated_node_identity() {
        let command = command(NodeCommandAction::Start {
            port: 4615,
            launch: ApplicationLaunch::Rdp,
            install_root: None,
            gpu_reservation: None,
            relay: Some(px_node_protocol::RelayEndpoint {
                host: "relay.example.test".into(),
                port: 4605,
                app_key: "must-not-enter-rdp".into(),
            }),
        });
        let NodeCommandAction::Start {
            port,
            launch,
            relay,
            ..
        } = &command.action
        else {
            unreachable!();
        };
        let identity = node_identity();
        let workspace_id = Uuid::new_v4();
        let request = start_request(
            &command,
            *port,
            launch,
            None,
            None,
            relay.as_ref(),
            StartSecurityContext {
                rdp_workspace: Some(RdpWorkspaceCredential {
                    workspace_id,
                    account_name: "pxrdp_0123456789abcd".into(),
                    credential_revision: 1,
                    expected_sid: Some("S-1-5-21-1-2-3-1001".into()),
                    password: zeroize::Zeroizing::new(
                        "aA1!01234567890123456789012345678901".into(),
                    ),
                }),
                node_identity: identity,
            },
        )
        .unwrap();
        assert_eq!(request.app_mode, service_core::app_instance::APP_MODE_RDP);
        assert_eq!(request.rdp_node_id, identity.node_id.to_string());
        assert_eq!(request.device_id, identity.device_id.to_string());
        assert_eq!(
            request.rdp_account.as_ref().unwrap().workspace_id,
            workspace_id.to_string()
        );
        assert!(request.gpu_stable_key.is_none());
        assert!(request.relay_server_host.is_empty());
        assert!(start_request(
            &command,
            *port,
            launch,
            None,
            None,
            relay.as_ref(),
            StartSecurityContext {
                rdp_workspace: None,
                node_identity: identity,
            },
        )
        .is_err());
    }

    #[test]
    fn deployment_preparation_is_fail_closed_and_checks_real_files() {
        let product = cloud_product();
        let telemetry = gpu_telemetry();
        let executable = std::env::current_exe().unwrap();
        let game = DeploymentAssignment {
            id: Uuid::new_v4(),
            application_id: Uuid::new_v4(),
            deployment_revision: 1,
            application_revision: 1,
            disabled: false,
            preparation: DeploymentPreparation::GameHook {
                install_root: executable.parent().unwrap().to_string_lossy().into_owned(),
                executable_relative: executable
                    .file_name()
                    .unwrap()
                    .to_string_lossy()
                    .into_owned(),
                gpu_key: None,
            },
        };
        assert!(matches!(
            preparation_state(&product, &game, &telemetry),
            PreparationState::Ready
        ));
        let missing = DeploymentAssignment {
            preparation: DeploymentPreparation::GameHook {
                install_root: executable.parent().unwrap().to_string_lossy().into_owned(),
                executable_relative: "missing.exe".into(),
                gpu_key: None,
            },
            ..game
        };
        assert!(matches!(
            preparation_state(&product, &missing, &telemetry),
            PreparationState::Failed {
                reason: PreparationFailure::MissingFiles
            }
        ));
        let rdp = DeploymentAssignment {
            id: Uuid::new_v4(),
            application_id: Uuid::new_v4(),
            deployment_revision: 1,
            application_revision: 1,
            disabled: false,
            preparation: DeploymentPreparation::Rdp { gpu_key: None },
        };
        assert!(matches!(
            preparation_state(&product, &rdp, &telemetry),
            PreparationState::Failed {
                reason: PreparationFailure::UnsupportedMode
            }
        ));
    }

    #[tokio::test]
    async fn real_websocket_exchange_uses_strict_json_and_matching_request_id() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let node_id = Uuid::new_v4();
        let device_id = Uuid::new_v4();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let mut socket = tokio_tungstenite::accept_async(stream).await.unwrap();
            let message = socket.next().await.unwrap().unwrap();
            let Message::Text(text) = message else {
                panic!("expected text request");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            assert_eq!(request.request_id(), 1);
            assert!(matches!(request, NodeRequest::Authenticate { .. }));
            socket
                .send(Message::Text(
                    serde_json::to_string(&NodeResponse::Authenticated {
                        request_id: 1,
                        node_id,
                        device_id,
                        generation: 2,
                        control_epoch: 3,
                        relay: None,
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();
        });
        let endpoint = format!("ws://{address}/api/console/node-control");
        let (mut socket, _) = tokio_tungstenite::connect_async(endpoint).await.unwrap();
        let response = exchange(
            &mut socket,
            NodeRequest::Authenticate {
                request_id: 1,
                node_token: "a".repeat(64),
            },
        )
        .await
        .unwrap();
        assert!(matches!(
            response,
            NodeResponse::Authenticated {
                request_id: 1,
                generation: 2,
                control_epoch: 3,
                ..
            }
        ));
        server.await.unwrap();
    }

    #[tokio::test]
    async fn rdp_workspace_operations_bind_the_secret_and_confirmation_to_one_lease() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let command = command(NodeCommandAction::Stop);
        let expected_command_id = command.id;
        let expected_lease_id = command.lease_id;
        let expected_workspace_id = Uuid::new_v4();
        let expected_windows_sid = "S-1-5-21-1-2-3-1001".to_string();
        let server_windows_sid = expected_windows_sid.clone();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let mut socket = tokio_tungstenite::accept_async(stream).await.unwrap();

            let Message::Text(text) = socket.next().await.unwrap().unwrap() else {
                panic!("expected RDP workspace request");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::FetchRdpWorkspace {
                request_id,
                command_id,
                lease_id,
            } = request
            else {
                panic!("expected RDP workspace request");
            };
            assert_eq!(command_id, expected_command_id);
            assert_eq!(lease_id, expected_lease_id);
            socket
                .send(Message::Text(
                    serde_json::to_string(&NodeResponse::RdpWorkspace {
                        request_id,
                        workspace: RdpWorkspaceCredential {
                            workspace_id: expected_workspace_id,
                            account_name: "pxrdp_0123456789abcd".into(),
                            credential_revision: 3,
                            expected_sid: Some(server_windows_sid.clone()),
                            password: zeroize::Zeroizing::new("a-secure-workspace-password".into()),
                        },
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();

            let Message::Text(text) = socket.next().await.unwrap().unwrap() else {
                panic!("expected RDP workspace confirmation");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::ConfirmRdpWorkspace {
                request_id,
                command_id,
                lease_id,
                workspace_id,
                windows_sid,
            } = request
            else {
                panic!("expected RDP workspace confirmation");
            };
            assert_eq!(command_id, expected_command_id);
            assert_eq!(lease_id, expected_lease_id);
            assert_eq!(workspace_id, expected_workspace_id);
            assert_eq!(windows_sid, server_windows_sid);
            socket
                .send(Message::Text(
                    serde_json::to_string(&NodeResponse::RdpWorkspaceConfirmed {
                        request_id,
                        workspace_id,
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();
        });

        let endpoint = format!("ws://{address}/api/console/node-control");
        let (mut socket, _) = tokio_tungstenite::connect_async(endpoint).await.unwrap();
        let mut session = authenticated_session();
        let workspace = fetch_rdp_workspace(&mut socket, &mut session, &command)
            .await
            .unwrap();
        assert_eq!(workspace.workspace_id, expected_workspace_id);
        assert_eq!(workspace.account_name, "pxrdp_0123456789abcd");
        assert_eq!(workspace.password.as_str(), "a-secure-workspace-password");
        confirm_rdp_workspace(
            &mut socket,
            &mut session,
            &command,
            workspace.workspace_id,
            expected_windows_sid,
        )
        .await
        .unwrap();
        server.await.unwrap();
    }

    #[tokio::test]
    async fn frontend_admission_operation_uses_real_websocket_and_bounded_lease() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let expected_session_id = Uuid::new_v4();
        let expected_application_id = Uuid::new_v4();
        let expected_instance_id = Uuid::new_v4();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let mut socket = tokio_tungstenite::accept_async(stream).await.unwrap();
            let message = socket.next().await.unwrap().unwrap();
            let Message::Text(text) = message else {
                panic!("expected text request");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::AdmitFrontend {
                request_id,
                session_id,
                revision,
                frontend_token,
            } = request
            else {
                panic!("expected frontend admission request");
            };
            assert_eq!(session_id, expected_session_id);
            assert_eq!(revision, 7);
            assert_eq!(frontend_token, "single-use-secret");
            let response = NodeResponse::FrontendAdmitted {
                request_id,
                grant: px_node_protocol::FrontendGrant {
                    session_id,
                    revision: 8,
                    target: px_node_protocol::FrontendTarget::CloudApplication {
                        application_id: expected_application_id,
                        instance_id: expected_instance_id,
                    },
                    client_type: "android".into(),
                    access_role: "controller".into(),
                    valid_for_ms: 30_000,
                },
            };
            socket
                .send(Message::Text(
                    serde_json::to_string(&response).unwrap().into(),
                ))
                .await
                .unwrap();
        });

        let endpoint = format!("ws://{address}/api/console/node-control");
        let (mut socket, _) = tokio_tungstenite::connect_async(endpoint).await.unwrap();
        let mut session = ProtocolSession::new();
        let (completion, result) = oneshot::channel();
        execute_operation(
            &mut socket,
            &mut session,
            NodeControlOperation::AdmitFrontend {
                session_id: expected_session_id,
                revision: 7,
                frontend_token: zeroize::Zeroizing::new("single-use-secret".to_string()),
                completion,
            },
        )
        .await
        .unwrap();
        let grant = result.await.unwrap().unwrap();
        assert_eq!(grant.session_id, expected_session_id);
        assert_eq!(grant.revision, 8);
        assert!(grant.valid_for_ms > 0);
        assert!(grant.valid_for_ms <= 30_000);
        assert!(matches!(
            grant.target,
            px_node_protocol::FrontendTarget::CloudApplication {
                application_id,
                instance_id,
            } if application_id == expected_application_id && instance_id == expected_instance_id
        ));
        server.await.unwrap();
    }

    #[tokio::test]
    async fn resource_channel_operations_use_real_websocket_and_preserve_sequence() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let source_id = Uuid::new_v4();
        let session_id = Uuid::new_v4();
        let channel_id = Uuid::new_v4();
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let mut socket = tokio_tungstenite::accept_async(stream).await.unwrap();
            let Message::Text(text) = socket.next().await.unwrap().unwrap() else {
                panic!("expected channel open request");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::OpenChannel {
                request_id,
                channel,
            } = request
            else {
                panic!("expected channel open request");
            };
            assert_eq!(channel.source_id, source_id);
            assert_eq!(channel.session_id, session_id);
            assert!(matches!(channel.kind, ChannelKind::Media));
            socket
                .send(Message::Text(
                    serde_json::to_string(&NodeResponse::ChannelOpened {
                        request_id,
                        channel_id,
                        state: "active".into(),
                        sequence: 0,
                        revision: 1,
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();

            let Message::Text(text) = socket.next().await.unwrap().unwrap() else {
                panic!("expected channel report request");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::ReportChannel {
                request_id,
                channel_id: actual_channel_id,
                progress,
            } = request
            else {
                panic!("expected channel report request");
            };
            assert_eq!(actual_channel_id, channel_id);
            assert_eq!(progress.sequence, 1);
            assert_eq!(progress.sent_bytes, 1_000);
            assert!(matches!(
                progress.outcome,
                px_node_protocol::ChannelOutcome::Closed {
                    reason: px_node_protocol::ChannelClose::PeerClosed
                }
            ));
            socket
                .send(Message::Text(
                    serde_json::to_string(&NodeResponse::ChannelReported {
                        request_id,
                        channel_id,
                        state: "closed".into(),
                        sequence: 1,
                        revision: 2,
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();
        });

        let endpoint = format!("ws://{address}/api/console/node-control");
        let (mut socket, _) = tokio_tungstenite::connect_async(endpoint).await.unwrap();
        let mut session = ProtocolSession::new();
        let (open_completion, open_result) = oneshot::channel();
        execute_operation(
            &mut socket,
            &mut session,
            NodeControlOperation::OpenChannel {
                source_id,
                session_id,
                kind: ChannelKind::Media,
                completion: open_completion,
            },
        )
        .await
        .unwrap();
        let opened = open_result.await.unwrap().unwrap();
        assert_eq!(opened.channel_id, channel_id);
        assert_eq!(opened.state, "active");
        assert_eq!(opened.sequence, 0);
        assert_eq!(opened.revision, 1);

        let (report_completion, report_result) = oneshot::channel();
        execute_operation(
            &mut socket,
            &mut session,
            NodeControlOperation::ReportChannel {
                channel_id,
                progress: ChannelProgress {
                    sequence: 1,
                    sent_bytes: 1_000,
                    received_bytes: 250,
                    elapsed_ms: 500,
                    outcome: px_node_protocol::ChannelOutcome::Closed {
                        reason: px_node_protocol::ChannelClose::PeerClosed,
                    },
                },
                completion: report_completion,
            },
        )
        .await
        .unwrap();
        let reported = report_result.await.unwrap().unwrap();
        assert_eq!(reported.channel_id, channel_id);
        assert_eq!(reported.state, "closed");
        assert_eq!(reported.sequence, 1);
        assert_eq!(reported.revision, 2);
        server.await.unwrap();
    }

    #[cfg(windows)]
    #[tokio::test]
    async fn file_transfer_operations_use_real_websocket_and_preserve_digest() {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let transfer_request_id = Uuid::new_v4();
        let session_id = Uuid::new_v4();
        let transfer_id = Uuid::new_v4();
        let expected_sha256 = [11_u8; 32];
        let server = tokio::spawn(async move {
            let (stream, _) = listener.accept().await.unwrap();
            let mut socket = tokio_tungstenite::accept_async(stream).await.unwrap();
            let Message::Text(text) = socket.next().await.unwrap().unwrap() else {
                panic!("expected file transfer begin request");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::BeginFileTransfer {
                request_id,
                transfer,
            } = request
            else {
                panic!("expected file transfer begin request");
            };
            assert_eq!(transfer.transfer_request_id, transfer_request_id);
            assert_eq!(transfer.session_id, session_id);
            assert!(matches!(transfer.direction, TransferDirection::ToNode));
            assert_eq!(transfer.file_name, "payload.bin");
            assert_eq!(transfer.total_bytes, 4096);
            assert_eq!(transfer.expected_sha256, Some(expected_sha256));
            socket
                .send(Message::Text(
                    serde_json::to_string(&NodeResponse::FileTransferStarted {
                        request_id,
                        transfer_id,
                        state: "active".into(),
                        sequence: 0,
                        revision: 1,
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();

            let Message::Text(text) = socket.next().await.unwrap().unwrap() else {
                panic!("expected file transfer report request");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::ReportFileTransfer {
                request_id,
                transfer_id: actual_transfer_id,
                progress,
            } = request
            else {
                panic!("expected file transfer report request");
            };
            assert_eq!(actual_transfer_id, transfer_id);
            assert_eq!(progress.sequence, 1);
            assert_eq!(progress.transferred_bytes, 4096);
            assert!(matches!(
                progress.outcome,
                px_node_protocol::TransferOutcome::Completed { received_sha256 }
                    if received_sha256 == expected_sha256
            ));
            socket
                .send(Message::Text(
                    serde_json::to_string(&NodeResponse::FileTransferReported {
                        request_id,
                        transfer_id,
                        state: "completed".into(),
                        sequence: 1,
                        revision: 2,
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();
        });

        let endpoint = format!("ws://{address}/api/console/node-control");
        let (mut socket, _) = tokio_tungstenite::connect_async(endpoint).await.unwrap();
        let mut session = ProtocolSession::new();
        let (begin_completion, begin_result) = oneshot::channel();
        execute_operation(
            &mut socket,
            &mut session,
            NodeControlOperation::BeginFileTransfer {
                transfer_request_id,
                session_id,
                direction: TransferDirection::ToNode,
                file_name: "payload.bin".into(),
                total_bytes: 4096,
                expected_sha256: Some(expected_sha256),
                completion: begin_completion,
            },
        )
        .await
        .unwrap();
        let begun = begin_result.await.unwrap().unwrap();
        assert_eq!(begun.transfer_id, transfer_id);
        assert_eq!(begun.state, "active");
        assert_eq!(begun.sequence, 0);
        assert_eq!(begun.revision, 1);

        let temporary = tempfile::tempdir().unwrap();
        let outbox = Arc::new(std::sync::Mutex::new(FileTransferOutboxStore::new(
            temporary.path().to_path_buf(),
        )));
        outbox
            .lock()
            .unwrap()
            .append(
                transfer_id,
                &TransferProgress {
                    sequence: 1,
                    transferred_bytes: 4096,
                    outcome: px_node_protocol::TransferOutcome::Completed {
                        received_sha256: expected_sha256,
                    },
                },
            )
            .unwrap();
        sync_file_transfer_reports(&mut socket, &mut session, &outbox)
            .await
            .unwrap();
        assert!(outbox.lock().unwrap().pending(32).unwrap().is_empty());
        server.await.unwrap();
    }

    #[cfg(windows)]
    #[tokio::test]
    async fn file_transfer_outbox_retries_exact_report_after_response_loss() {
        let temporary = tempfile::tempdir().unwrap();
        let transfer_id = Uuid::new_v4();
        let outbox = Arc::new(std::sync::Mutex::new(FileTransferOutboxStore::new(
            temporary.path().to_path_buf(),
        )));
        outbox
            .lock()
            .unwrap()
            .append(
                transfer_id,
                &TransferProgress {
                    sequence: 4,
                    transferred_bytes: 8192,
                    outcome: px_node_protocol::TransferOutcome::Cancelled,
                },
            )
            .unwrap();

        let first_listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let first_address = first_listener.local_addr().unwrap();
        let first_server = tokio::spawn(async move {
            let (stream, _) = first_listener.accept().await.unwrap();
            let mut socket = tokio_tungstenite::accept_async(stream).await.unwrap();
            let Message::Text(text) = socket.next().await.unwrap().unwrap() else {
                panic!("expected persisted file transfer report");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::ReportFileTransfer {
                transfer_id: actual_transfer_id,
                progress,
                ..
            } = request
            else {
                panic!("expected persisted file transfer report");
            };
            assert_eq!(actual_transfer_id, transfer_id);
            assert_eq!(progress.sequence, 4);
            assert!(matches!(
                progress.outcome,
                px_node_protocol::TransferOutcome::Cancelled
            ));
            socket.close(None).await.unwrap();
        });
        let (mut first_socket, _) =
            tokio_tungstenite::connect_async(format!("ws://{first_address}"))
                .await
                .unwrap();
        let mut first_session = ProtocolSession::new();
        assert!(
            sync_file_transfer_reports(&mut first_socket, &mut first_session, &outbox)
                .await
                .is_err()
        );
        first_server.await.unwrap();
        assert_eq!(outbox.lock().unwrap().pending(32).unwrap().len(), 1);

        let second_listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let second_address = second_listener.local_addr().unwrap();
        let second_server = tokio::spawn(async move {
            let (stream, _) = second_listener.accept().await.unwrap();
            let mut socket = tokio_tungstenite::accept_async(stream).await.unwrap();
            let Message::Text(text) = socket.next().await.unwrap().unwrap() else {
                panic!("expected retried file transfer report");
            };
            let request: NodeRequest = serde_json::from_str(&text).unwrap();
            let NodeRequest::ReportFileTransfer {
                request_id,
                transfer_id: actual_transfer_id,
                progress,
            } = request
            else {
                panic!("expected retried file transfer report");
            };
            assert_eq!(actual_transfer_id, transfer_id);
            assert_eq!(progress.sequence, 4);
            socket
                .send(Message::Text(
                    serde_json::to_string(&NodeResponse::FileTransferReported {
                        request_id,
                        transfer_id,
                        state: "cancelled".into(),
                        sequence: 4,
                        revision: 5,
                    })
                    .unwrap()
                    .into(),
                ))
                .await
                .unwrap();
        });
        let (mut second_socket, _) =
            tokio_tungstenite::connect_async(format!("ws://{second_address}"))
                .await
                .unwrap();
        let mut second_session = ProtocolSession::new();
        sync_file_transfer_reports(&mut second_socket, &mut second_session, &outbox)
            .await
            .unwrap();
        second_server.await.unwrap();
        assert!(outbox.lock().unwrap().pending(32).unwrap().is_empty());
    }

    #[tokio::test]
    async fn recording_upload_streams_only_the_registered_content_identity() {
        use tokio::io::{AsyncReadExt, AsyncWriteExt};

        let temporary = tempfile::tempdir().unwrap();
        let data_root = temporary.path().join("px_data");
        let recording_root = temporary.path().join("px_render_records");
        std::fs::create_dir_all(&data_root).unwrap();
        std::fs::create_dir_all(&recording_root).unwrap();
        let recording_bytes = b"service recording upload";
        std::fs::write(
            recording_root.join("rec_mon0_20260919_04.30.00.mp4"),
            recording_bytes,
        )
        .unwrap();
        let inventory = RecordingInventory::load(&data_root).unwrap();
        inventory
            .lock()
            .unwrap()
            .register_completed(
                "rec_mon0_20260919_04.30.00.mp4".into(),
                Some(Uuid::new_v4()),
                px_node_protocol::RecordingCodec::H264,
            )
            .unwrap();
        let report = inventory.lock().unwrap().scan().unwrap().remove(0);
        let attempt_id = Uuid::new_v4();
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        let expected_token = "a".repeat(64);
        let server_token = expected_token.clone();
        let server = tokio::spawn(async move {
            let (mut connection, _) = listener.accept().await.unwrap();
            let mut request = Vec::new();
            let header_end = loop {
                let mut buffer = [0_u8; 1024];
                let count = connection.read(&mut buffer).await.unwrap();
                assert!(count > 0);
                request.extend_from_slice(&buffer[..count]);
                if let Some(position) = request.windows(4).position(|bytes| bytes == b"\r\n\r\n") {
                    break position + 4;
                }
            };
            let headers = String::from_utf8(request[..header_end].to_vec()).unwrap();
            assert!(headers.starts_with(&format!(
                "PUT /api/console/node-recording-cache/{attempt_id} HTTP/1.1\r\n"
            )));
            assert!(headers
                .to_ascii_lowercase()
                .contains(&format!("authorization: bearer {server_token}\r\n")));
            let content_length = headers
                .lines()
                .find_map(|line| {
                    line.to_ascii_lowercase()
                        .strip_prefix("content-length: ")
                        .and_then(|value| value.parse::<usize>().ok())
                })
                .unwrap();
            while request.len() - header_end < content_length {
                let mut buffer = [0_u8; 1024];
                let count = connection.read(&mut buffer).await.unwrap();
                assert!(count > 0);
                request.extend_from_slice(&buffer[..count]);
            }
            assert_eq!(
                &request[header_end..header_end + content_length],
                recording_bytes
            );
            connection
                .write_all(
                    b"HTTP/1.1 201 Created\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}",
                )
                .await
                .unwrap();
        });
        let upload = RecordingCacheUpload {
            attempt_id,
            recording_id: Uuid::new_v4(),
            source_id: report.source_id,
            size_bytes: report.size_bytes,
            source_sha256: report.source_sha256,
            upload_path: format!("/api/console/node-recording-cache/{attempt_id}"),
            upload_token: expected_token,
            valid_for_ms: 30_000,
        };
        let client = reqwest::Client::builder()
            .redirect(reqwest::redirect::Policy::none())
            .build()
            .unwrap();
        upload_recording(
            client,
            url::Url::parse(&format!("ws://{address}/api/console/node-control")).unwrap(),
            inventory,
            upload,
        )
        .await
        .unwrap();
        server.await.unwrap();
    }
}
