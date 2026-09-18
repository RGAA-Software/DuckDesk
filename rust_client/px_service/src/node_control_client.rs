use std::sync::Arc;
use std::time::Duration;

use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use chrono::Utc;
use futures_util::{SinkExt, StreamExt};
use px_node_protocol::{
    ApplicationLaunch, ChannelKind, ChannelProgress, CommandOutcome, CommandReceipt,
    DeploymentAssignment, DeploymentObservation, DeploymentPreparation, NodeCommand,
    NodeCommandAction, NodeReport, NodeRequest, NodeResponse, ObservedRuntime,
    ObservedRuntimePhase, OpenChannel, PreparationFailure, PreparationState, RuntimeInventory,
    VideoCodec, MAX_MESSAGE_BYTES,
};
use service_core::{AppInstanceState, StartAppRequest};
use tokio::net::TcpStream;
use tokio::sync::{oneshot, Mutex};
use tokio::time::{sleep, timeout};
use tokio_tungstenite::tungstenite::protocol::WebSocketConfig;
use tokio_tungstenite::tungstenite::Message;
use tokio_tungstenite::{MaybeTlsStream, WebSocketStream};
use tracing::{info, warn};
use uuid::Uuid;
use zeroize::Zeroize;

use crate::node_control_store::{NodeControlConfiguration, NodeControlStore};
use crate::product_descriptor::ProductDescriptor;
use crate::service_host::ServiceRuntime;

type NodeSocket = WebSocketStream<MaybeTlsStream<TcpStream>>;

const CONNECT_TIMEOUT: Duration = Duration::from_secs(8);
const EXCHANGE_TIMEOUT: Duration = Duration::from_secs(10);
const RECONNECT_DELAY: Duration = Duration::from_secs(2);
const CONFIGURATION_POLL: Duration = Duration::from_secs(5);
const COMMAND_POLL: Duration = Duration::from_secs(1);
const REPORT_INTERVAL: Duration = Duration::from_secs(15);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct ConnectionIdentity {
    node_id: Uuid,
    generation: i64,
    control_epoch: i64,
}

struct ProtocolSession {
    next_request_id: u64,
    identity: Option<ConnectionIdentity>,
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
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) struct NodeChannelReceipt {
    pub channel_id: Uuid,
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
    ) -> Result<ConnectionIdentity, String> {
        match response {
            NodeResponse::Authenticated {
                request_id,
                node_id,
                generation,
                control_epoch,
            } if request_id == expected_request_id && generation > 0 && control_epoch > 0 => {
                let identity = ConnectionIdentity {
                    node_id,
                    generation,
                    control_epoch,
                };
                self.identity = Some(identity);
                Ok(identity)
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

pub async fn node_control_loop(runtime: Arc<Mutex<ServiceRuntime>>) -> Result<(), String> {
    let product = ProductDescriptor::load_for_current_executable()?;
    let (store, mut stop_rx, mut operations) = {
        let mut guard = runtime.lock().await;
        (
            NodeControlStore::new(guard.config.data_root.clone()),
            guard.subscribe_stop(),
            guard
                .node_control_receiver
                .take()
                .ok_or_else(|| "node-control operation receiver was already taken".to_string())?,
        )
    };
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
        match run_connection(
            &runtime,
            &configuration,
            &product,
            &mut stop_rx,
            &mut operations,
        )
        .await
        {
            Ok(ConnectionEnd::Stopped) => return Ok(()),
            Err(error) => warn!(%error, "node-control connection ended"),
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

async fn run_connection(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    configuration: &NodeControlConfiguration,
    product: &ProductDescriptor,
    stop_rx: &mut tokio::sync::broadcast::Receiver<()>,
    operations: &mut tokio::sync::mpsc::Receiver<NodeControlOperation>,
) -> Result<ConnectionEnd, String> {
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
    let identity = session.accept_authentication(authentication_id, response)?;
    info!(
        node_id = %identity.node_id,
        generation = identity.generation,
        control_epoch = identity.control_epoch,
        "node-control authenticated"
    );

    let endpoint_revision = report(
        &mut socket,
        &mut session,
        runtime,
        configuration,
        product,
        1,
    )
    .await?;
    sync_deployments(&mut socket, &mut session, product, endpoint_revision, 1).await?;
    reconcile(&mut socket, &mut session, runtime).await?;

    let mut report_sequence = 1_u64;
    let mut reports = tokio::time::interval(REPORT_INTERVAL);
    reports.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    reports.tick().await;
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
                let endpoint_revision = report(
                    &mut socket,
                    &mut session,
                    runtime,
                    configuration,
                    product,
                    report_sequence,
                ).await?;
                sync_deployments(
                    &mut socket,
                    &mut session,
                    product,
                    endpoint_revision,
                    report_sequence,
                ).await?;
            }
            _ = sleep(COMMAND_POLL) => {
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
                        Ok(()) => execute_command(runtime, &command).await,
                        Err(error) => {
                            warn!(command_id = %command.id, %error, "node command rejected locally");
                            CommandOutcome::Unknown
                        }
                    };
                    acknowledge(&mut socket, &mut session, &command, outcome).await?;
                }
            }
            operation = operations.recv() => {
                let Some(operation) = operation else {
                    return Err("node-control operation channel closed".into());
                };
                execute_operation(&mut socket, &mut session, operation).await?;
            }
        }
    }
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
    }
}

async fn report(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    runtime: &Arc<Mutex<ServiceRuntime>>,
    configuration: &NodeControlConfiguration,
    product: &ProductDescriptor,
    sequence: u64,
) -> Result<i64, String> {
    let node = runtime.lock().await.config.node.clone();
    let capability = |name: &str| product.capabilities.iter().any(|value| value == name);
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
            // RDP remains unavailable until the new protocol carries a workspace envelope.
            rdp: false,
        },
    };
    let expected = request.request_id();
    match exchange(socket, request).await? {
        NodeResponse::Reported {
            request_id,
            endpoint_revision,
            ..
        } if request_id == expected && endpoint_revision > 0 => Ok(endpoint_revision),
        NodeResponse::Error { code, .. } => Err(format!("node report rejected: {code}")),
        _ => Err("unexpected node report response".into()),
    }
}

async fn sync_deployments(
    socket: &mut NodeSocket,
    session: &mut ProtocolSession,
    product: &ProductDescriptor,
    endpoint_revision: i64,
    sequence: u64,
) -> Result<(), String> {
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
                status: preparation_state(product, &deployment),
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
            if gpu_key.is_some() {
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
            } else if gpu_key.is_some() {
                PreparationState::Failed {
                    reason: PreparationFailure::BindingUnverified,
                }
            } else {
                PreparationState::Ready
            }
        }
        DeploymentPreparation::Rdp { .. } => PreparationState::Failed {
            reason: PreparationFailure::UnsupportedMode,
        },
    }
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

async fn execute_command(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    command: &NodeCommand,
) -> CommandOutcome {
    let deadline = std::cmp::min(command.lease_until, command.deadline);
    if deadline <= Utc::now() {
        return CommandOutcome::Unknown;
    }
    let outcome = execute_command_before_deadline(runtime, command).await;
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

async fn execute_command_before_deadline(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    command: &NodeCommand,
) -> CommandOutcome {
    match &command.action {
        NodeCommandAction::Start {
            port,
            launch,
            install_root,
            gpu_key,
        } => {
            if gpu_key.is_some() || matches!(launch, ApplicationLaunch::Rdp) {
                return CommandOutcome::Absent;
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
            let request = match start_request(command, *port, launch, install_root.as_deref()) {
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

fn start_request(
    command: &NodeCommand,
    port: u16,
    launch: &ApplicationLaunch,
    install_root: Option<&str>,
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
        ApplicationLaunch::Rdp => return Err("RDP workspace envelope is unavailable".into()),
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
    Ok(StartAppRequest {
        request_id: command.launch_id.to_string(),
        instance_id: command.instance_id.to_string(),
        app_id: command.application_id.to_string(),
        install_root,
        game_exe_rel: executable,
        game_arguments: arguments,
        listen_port: i32::from(port),
        encoder_fps: 60,
        encoder_bitrate: i32::try_from(bitrate)
            .map_err(|_| "video bitrate is outside the Render range".to_string())?,
        encoder_format: match codec {
            VideoCodec::H264 => "h264",
            VideoCodec::H265 => "h265",
        }
        .into(),
        webrtc_enabled: true,
        websocket_enabled: true,
        live_stream_id: String::new(),
        push_rtmp_url: String::new(),
        app_mode: mode.into(),
        webview_url_b64: webview,
        rdp_node_id: String::new(),
        rdp_account: None,
        device_id: command.instance_id.to_string(),
        relay_device_id: String::new(),
        relay_server_host: String::new(),
        relay_server_port: 0,
        relay_appkey: String::new(),
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
    use px_node_protocol::VideoSpec;

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
        session.identity = Some(ConnectionIdentity {
            node_id: Uuid::new_v4(),
            generation: 2,
            control_epoch: 3,
        });
        session
    }

    fn cloud_product() -> ProductDescriptor {
        ProductDescriptor {
            schema_version: 2,
            product: "cloud_node".into(),
            edition: "CLOUD_NODE".into(),
            company: "Pixels".into(),
            product_version: "3.3.67".into(),
            product_version_code: 30367,
            capabilities: vec!["game_hook".into(), "webview_host".into()],
        }
    }

    #[test]
    fn request_ids_are_strictly_increasing_and_authentication_is_bound() {
        let mut session = ProtocolSession::new();
        let request = session.authenticate(&"a".repeat(64)).unwrap();
        assert_eq!(request.request_id(), 1);
        let node_id = Uuid::new_v4();
        let identity = session
            .accept_authentication(
                1,
                NodeResponse::Authenticated {
                    request_id: 1,
                    node_id,
                    generation: 4,
                    control_epoch: 5,
                },
            )
            .unwrap();
        assert_eq!(identity.node_id, node_id);
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
            gpu_key: None,
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
        let request = start_request(&command, *port, launch, install_root.as_deref()).unwrap();
        assert_eq!(request.request_id, command.launch_id.to_string());
        assert_eq!(request.game_exe_rel, "游戏 目录\\game.exe");
        assert_eq!(request.game_arguments, "--name \"two words\"");
        assert_eq!(request.encoder_format, "h265");
        assert_eq!(request.encoder_bitrate, 24_000);
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
            gpu_key: None,
        });
        let NodeCommandAction::Start { port, launch, .. } = &command.action else {
            unreachable!();
        };
        let request = start_request(&command, *port, launch, None).unwrap();
        assert_eq!(
            URL_SAFE_NO_PAD.decode(request.webview_url_b64).unwrap(),
            "https://example.com/云应用".as_bytes()
        );
        assert!(start_request(&command, *port, launch, Some("D:\\wrong")).is_err());
    }

    #[test]
    fn deployment_preparation_is_fail_closed_and_checks_real_files() {
        let product = cloud_product();
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
            preparation_state(&product, &game),
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
            preparation_state(&product, &missing),
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
            preparation_state(&product, &rdp),
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
                        generation: 2,
                        control_epoch: 3,
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
}
