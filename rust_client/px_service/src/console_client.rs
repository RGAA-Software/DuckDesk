use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use futures_util::stream::SplitSink;
use futures_util::{SinkExt, StreamExt};
use prost::Message;
use rustls::client::danger::{HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier};
use rustls::pki_types::{CertificateDer, ServerName, UnixTime};
use rustls::{DigitallySignedStruct, SignatureScheme};
use service_core::MsgAuthInfo;
use tokio::net::TcpStream;
use tokio::sync::Mutex;
use tokio::time::{sleep, timeout};
use tokio_tungstenite::tungstenite::Message as TungsteniteMessage;
use tokio_tungstenite::{Connector, MaybeTlsStream, WebSocketStream};
use tracing::{error, info, warn};

use protocol::console_service::{
    ConsoleServiceCreateWallSession, ConsoleServiceCreateWallSessionResult,
    ConsoleServiceHeartBeat, ConsoleServiceHello, ConsoleServiceMessage, ConsoleServiceMessageType,
    ConsoleServiceRedeemConnectionTicket, ConsoleServiceRedeemConnectionTicketResult,
    ConsoleServiceStartAppInstance, ConsoleServiceStartAppInstanceResult,
    ConsoleServiceStopAppInstance, ConsoleServiceStopAppInstanceResult,
};
use px_auth_mgr::app_secret_util::calculate_app_secret;
use px_auth_mgr::auth_token::{generate_connection_token, ConnectionToken};
use service_core::StartAppRequest;

use crate::service_host::{ServiceRuntime, TicketRedeemRequest, TicketRedeemResult};

type WsSink = SplitSink<WebSocketStream<MaybeTlsStream<TcpStream>>, TungsteniteMessage>;

const CONNECT_TIMEOUT_SECS: u64 = 5;
const RECONNECT_DELAY_SECS: u64 = 2;
const HEARTBEAT_INTERVAL_SECS: u64 = 3;
const AUTH_INFO_POLL_SECS: u64 = 1;
const RTC_CONFIG_POLL_SECS: u64 = 60;

/// WSS client loop towards the Console (px_console_server) `/console/service` endpoint.
///
/// The Console address, appkey and device_id come from the authorization info
/// the panel pushes to this service. The legacy console_ssl field is normalized
/// and cannot downgrade the transport. A fresh connection token is generated
/// on every reconnect.
pub async fn console_client_loop(runtime: Arc<Mutex<ServiceRuntime>>) -> Result<(), String> {
    let mut stop_rx = {
        let guard = runtime.lock().await;
        guard.subscribe_stop()
    };
    let sender: Arc<Mutex<Option<WsSink>>> = Arc::new(Mutex::new(None));
    let mut hb_index: i64 = 0;
    let (ticket_tx, mut ticket_rx) = tokio::sync::mpsc::channel::<TicketRedeemRequest>(32);
    runtime.lock().await.ticket_redeem_tx = Some(ticket_tx);

    loop {
        // wait until the panel has delivered the authorization info
        let Some(auth_info) = wait_auth_info(&runtime, &mut stop_rx).await else {
            info!("console client loop stopped before auth info arrived");
            return Ok(());
        };
        let auth_info = require_console_tls(auth_info);
        info!(
            "console auth info ready, console={}:{} device_id={}",
            auth_info.console_host, auth_info.console_port, auth_info.device_id
        );

        // regenerate the connection token on every (re)connect
        let app_secret = calculate_app_secret(auth_info.appkey.clone());
        let token = generate_connection_token(&auth_info.appkey, &app_secret);
        *sender.lock().await = None;
        let mut connected_stream = None;
        for legacy_route in [false, true] {
            let url = build_console_url_for_route(
                &auth_info.console_host,
                auth_info.console_port,
                auth_info.console_ssl,
                &auth_info.appkey,
                &token,
                &auth_info.device_id,
                legacy_route,
            );
            let endpoint = console_endpoint_for_route(
                &auth_info.console_host,
                auth_info.console_port,
                auth_info.console_ssl,
                legacy_route,
            );
            let connect = tokio_tungstenite::connect_async_tls_with_config(
                url.clone(),
                None,
                false,
                Some(tls_connector()),
            );
            match timeout(Duration::from_secs(CONNECT_TIMEOUT_SECS), connect).await {
                Ok(Ok((stream, _response))) => {
                    info!("connected to console {endpoint}");
                    connected_stream = Some(stream);
                    break;
                }
                Ok(Err(err)) => {
                    warn!("connect to console {endpoint} failed: {err}");
                }
                Err(_) => {
                    warn!("connect to console {endpoint} timed out");
                }
            }
        }
        let Some(stream) = connected_stream else {
            error!("canonical and legacy Console routes are unavailable");
            sleep(Duration::from_secs(RECONNECT_DELAY_SECS)).await;
            continue;
        };

        let (sink, mut receiver) = stream.split();
        *sender.lock().await = Some(sink);

        // say hello right after connecting
        let hello = encode_message(&hello_message(&auth_info.device_id, &auth_info.appkey));
        if !send_frame(&sender, hello).await {
            warn!("send hello to console failed, reconnecting");
            sleep(Duration::from_secs(RECONNECT_DELAY_SECS)).await;
            continue;
        }
        spawn_rtc_config_refresh(runtime.clone(), auth_info.clone(), 0);

        // heartbeat task: reports render liveness + the latest auth info as JSON
        let hb_sender = sender.clone();
        let hb_runtime = runtime.clone();
        let device_id = auth_info.device_id.clone();
        let mut hb_stop_rx = {
            let guard = runtime.lock().await;
            guard.subscribe_stop()
        };
        let heartbeat_task = tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(HEARTBEAT_INTERVAL_SECS));
            loop {
                tokio::select! {
                    _ = interval.tick() => {
                        hb_index += 1;
                        let (render_alive, auth_json, instances_json, logical_sessions_json) = {
                            let mut guard = hb_runtime.lock().await;
                            guard.reap_dead_app_instances();
                            let has_active = guard.app_registry.list().iter().any(|r| {
                                matches!(
                                    r.state,
                                    service_core::AppInstanceState::Starting
                                        | service_core::AppInstanceState::Running
                                        | service_core::AppInstanceState::Stopping
                                )
                            });
                            (
                                guard.state.desktop_alive || has_active,
                                guard
                                    .state
                                    .last_auth_info
                                    .as_ref()
                                    .map(auth_info_to_json)
                                    .unwrap_or_default(),
                                guard.app_registry.instances_json(),
                                guard.state.logical_sessions_json.clone(),
                            )
                        };
                        let frame = encode_message(&heartbeat_message(
                            hb_index,
                            &device_id,
                            render_alive,
                            &auth_json,
                            &instances_json,
                            &logical_sessions_json,
                        ));
                        if !send_frame(&hb_sender, frame).await {
                            break;
                        }
                    }
                    _ = hb_stop_rx.recv() => break,
                }
            }
        });

        // receive loop: handle Start/Stop app commands from Console
        let mut should_stop = false;
        let mut pending_tickets: HashMap<String, tokio::sync::oneshot::Sender<TicketRedeemResult>> =
            HashMap::new();
        let jitter = auth_info
            .device_id
            .bytes()
            .fold(0_u64, |sum, byte| sum.wrapping_add(byte as u64))
            % 31;
        let poll_seconds = RTC_CONFIG_POLL_SECS + jitter - 15;
        let mut rtc_config_interval = tokio::time::interval(Duration::from_secs(poll_seconds));
        rtc_config_interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        // Initial pull was already scheduled above.
        rtc_config_interval.tick().await;
        loop {
            tokio::select! {
                msg = receiver.next() => {
                    match msg {
                        Some(Ok(TungsteniteMessage::Close(_))) => {
                            warn!("console closed the connection");
                            break;
                        }
                        Some(Ok(TungsteniteMessage::Binary(bin))) => {
                            match parse_console_inbound(&bin) {
                                Ok(Some(ConsoleInboundCommand::RedeemTicketResult(result))) => {
                                    if let Some(reply) = pending_tickets.remove(&result.request_id) {
                                        let grant_present = result.grant.is_some();
                                        let grant = result.grant.unwrap_or_default();
                                        info!(
                                            ticket_redemption_ok = result.ok,
                                            grant_present,
                                            grant_permission_count = grant.permissions.len(),
                                            "received connection ticket redemption result from Console"
                                        );
                                        let _ = reply.send(TicketRedeemResult {
                                            ok: result.ok,
                                            code: result.code,
                                            kind: grant.kind,
                                            device_id: grant.device_id,
                                            app_id: grant.app_id,
                                            instance_id: grant.instance_id,
                                            subject_type: grant.subject_type,
                                            subject_id: grant.subject_id,
                                            logical_session_id: grant.logical_session_id,
                                            stream_id: grant.stream_id,
                                            join_mode: grant.join_mode,
                                            allow_observer: grant.allow_observer,
                                            allow_takeover: grant.allow_takeover,
                                            permissions: grant.permissions,
                                            expires_at: grant.expires_at,
                                            rtc_ice_config_json: result.rtc_ice_config_json,
                                        });
                                    }
                                }
                                Ok(Some(ConsoleInboundCommand::RtcIceConfigChanged(revision))) => {
                                    spawn_rtc_config_refresh(runtime.clone(), auth_info.clone(), revision);
                                }
                                Ok(Some(cmd)) => {
                                    // Start/Stop 可能耗时数秒(等 render 监听端口、
                                    // 等游戏进程),派生独立任务处理并异步回传结果,
                                    // 接收循环与心跳不被阻塞。
                                    spawn_console_command_handler(
                                        runtime.clone(),
                                        sender.clone(),
                                        cmd,
                                    );
                                }
                                Ok(None) => {}
                                Err(err) => warn!("console inbound parse error: {err}"),
                            }
                        }
                        Some(Ok(_)) => {}
                        Some(Err(err)) => {
                            error!("console receive error: {err}");
                            break;
                        }
                        None => {
                            warn!("console connection ended");
                            break;
                        }
                    }
                }
                request = ticket_rx.recv() => {
                    let Some(request) = request else { continue; };
                    if pending_tickets.contains_key(&request.request_id) {
                        let _ = request.response.send(TicketRedeemResult {
                            code: "DUPLICATE_REQUEST_ID".to_string(),
                            ..Default::default()
                        });
                        continue;
                    }
                    let frame = encode_message(&redeem_ticket_message(
                        &auth_info.device_id,
                        &request.request_id,
                        &request.ticket,
                        &request.client_nonce,
                        &request.instance_id,
                    ));
                    if send_frame(&sender, frame).await {
                        pending_tickets.insert(request.request_id, request.response);
                    } else {
                        let _ = request.response.send(TicketRedeemResult {
                            code: "CONSOLE_UNAVAILABLE".to_string(),
                            ..Default::default()
                        });
                    }
                }
                _ = rtc_config_interval.tick() => {
                    spawn_rtc_config_refresh(runtime.clone(), auth_info.clone(), 0);
                }
                _ = stop_rx.recv() => {
                    info!("console client loop received stop signal");
                    should_stop = true;
                    break;
                }
            }
        }

        heartbeat_task.abort();
        for (_, response) in pending_tickets.drain() {
            let _ = response.send(TicketRedeemResult {
                code: "CONSOLE_DISCONNECTED".to_string(),
                ..Default::default()
            });
        }
        *sender.lock().await = None;
        if should_stop {
            return Ok(());
        }
        info!(
            "will reconnect to console, sender strong_count={}",
            Arc::strong_count(&sender)
        );
        sleep(Duration::from_secs(RECONNECT_DELAY_SECS)).await;
    }
}

fn require_console_tls(mut auth_info: MsgAuthInfo) -> MsgAuthInfo {
    if !auth_info.console_ssl {
        warn!("ignoring legacy console_ssl=false; Console requires HTTPS/WSS");
        auth_info.console_ssl = true;
    }
    auth_info
}

/// Poll the shared state until the panel-delivered auth info shows up.
/// Returns `None` when the service is stopping.
async fn wait_auth_info(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    stop_rx: &mut tokio::sync::broadcast::Receiver<()>,
) -> Option<MsgAuthInfo> {
    loop {
        let snapshot = {
            let guard = runtime.lock().await;
            guard.state.last_auth_info.clone()
        };
        if snapshot.is_some() {
            return snapshot;
        }
        tokio::select! {
            _ = sleep(Duration::from_secs(AUTH_INFO_POLL_SECS)) => {}
            _ = stop_rx.recv() => return None,
        }
    }
}

async fn send_frame(sender: &Arc<Mutex<Option<WsSink>>>, frame: Vec<u8>) -> bool {
    let mut guard = sender.lock().await;
    let Some(sink) = guard.as_mut() else {
        error!("no console sender available");
        return false;
    };
    match sink.send(TungsteniteMessage::Binary(frame.into())).await {
        Ok(()) => true,
        Err(err) => {
            error!("send to console failed: {err}");
            false
        }
    }
}

/// Endpoint string safe for logs: no appkey/token query params.
#[cfg(test)]
fn console_endpoint(host: &str, port: i32, ssl: bool) -> String {
    console_endpoint_for_route(host, port, ssl, false)
}

fn console_endpoint_for_route(host: &str, port: i32, _ssl: bool, legacy_route: bool) -> String {
    let scheme = "wss";
    let path = if legacy_route {
        "/cms/service"
    } else {
        "/console/service"
    };
    format!("{scheme}://{host}:{port}{path}")
}

#[cfg(test)]
fn build_console_url(
    host: &str,
    port: i32,
    _ssl: bool,
    appkey: &str,
    token: &ConnectionToken,
    device_id: &str,
) -> String {
    build_console_url_for_route(host, port, _ssl, appkey, token, device_id, false)
}

fn build_console_url_for_route(
    host: &str,
    port: i32,
    _ssl: bool,
    appkey: &str,
    token: &ConnectionToken,
    device_id: &str,
    legacy_route: bool,
) -> String {
    let scheme = "wss";
    let path = if legacy_route {
        "/cms/service"
    } else {
        "/console/service"
    };
    format!(
        "{scheme}://{host}:{port}{path}?appkey={appkey}&token={}&ts={}&nonce={}&device_id={device_id}",
        token.token, token.ts, token.nonce,
    )
}

#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
struct RtcIceConfigView {
    revision: u64,
    direct_probe_enabled: bool,
    servers: Vec<serde_json::Value>,
}

#[derive(Debug, serde::Deserialize)]
struct RtcConfigApiResponse {
    code: i32,
    data: RtcIceConfigView,
}

fn spawn_rtc_config_refresh(
    runtime: Arc<Mutex<ServiceRuntime>>,
    auth_info: MsgAuthInfo,
    expected_revision: u64,
) {
    tokio::spawn(async move {
        if expected_revision != 0 {
            let delay_ms = auth_info
                .device_id
                .bytes()
                .fold(expected_revision, |sum, byte| sum.wrapping_add(byte as u64))
                % 3001;
            sleep(Duration::from_millis(delay_ms)).await;
        }
        if let Err(error) = refresh_rtc_config(&runtime, &auth_info, expected_revision).await {
            warn!("refresh RTC ICE configuration failed: {error}");
        }
    });
}

async fn refresh_rtc_config(
    runtime: &Arc<Mutex<ServiceRuntime>>,
    auth_info: &MsgAuthInfo,
    expected_revision: u64,
) -> Result<(), String> {
    let scheme = "https";
    let url = format!(
        "{scheme}://{}:{}/api/v1/rtc/ice-config",
        auth_info.console_host, auth_info.console_port
    );
    let client = reqwest::Client::builder()
        .connect_timeout(Duration::from_secs(3))
        .timeout(Duration::from_secs(5))
        // Pixels deployments commonly use the bundled self-signed Console
        // certificate. Authentication still uses the installation appkey.
        .danger_accept_invalid_certs(true)
        .build()
        .map_err(|error| error.to_string())?;
    let response = client
        .get(url)
        .header("x-px-appkey", &auth_info.appkey)
        .send()
        .await
        .map_err(|error| error.to_string())?;
    if !response.status().is_success() {
        return Err(format!("Console returned {}", response.status()));
    }
    let envelope: RtcConfigApiResponse =
        response.json().await.map_err(|error| error.to_string())?;
    if envelope.code != 0 && envelope.code != 200 {
        return Err(format!("Console RTC API returned code {}", envelope.code));
    }
    if expected_revision != 0 && envelope.data.revision < expected_revision {
        return Err(format!(
            "received stale RTC revision {}, expected at least {}",
            envelope.data.revision, expected_revision
        ));
    }
    let (path, current_revision) = {
        let guard = runtime.lock().await;
        let path = guard.config.data_root.join("rtc_ice_config.json");
        let current_revision = std::fs::read(&path)
            .ok()
            .and_then(|bytes| serde_json::from_slice::<RtcIceConfigView>(&bytes).ok())
            .map(|config| config.revision)
            .unwrap_or_default();
        (path, current_revision)
    };
    if envelope.data.revision <= current_revision {
        return Ok(());
    }
    let bytes = serde_json::to_vec_pretty(&envelope.data).map_err(|error| error.to_string())?;
    tokio::fs::write(&path, bytes)
        .await
        .map_err(|error| format!("write {} failed: {error}", path.display()))?;
    info!(
        revision = envelope.data.revision,
        direct_probe_enabled = envelope.data.direct_probe_enabled,
        servers = envelope.data.servers.len(),
        "RTC ICE configuration cache updated"
    );
    Ok(())
}

fn hello_message(device_id: &str, appkey: &str) -> ConsoleServiceMessage {
    ConsoleServiceMessage {
        msg_type: ConsoleServiceMessageType::KConsoleServiceHello as i32,
        device_id: device_id.to_string(),
        hello: Some(ConsoleServiceHello {
            device_id: device_id.to_string(),
            appkey: appkey.to_string(),
            version: env!("CARGO_PKG_VERSION").to_string(),
        }),
        heartbeat: None,
        start_app_instance: None,
        stop_app_instance: None,
        start_app_instance_result: None,
        stop_app_instance_result: None,
        create_wall_session: None,
        create_wall_session_result: None,
        ..Default::default()
    }
}

fn heartbeat_message(
    hb_index: i64,
    device_id: &str,
    render_alive: bool,
    auth_info_json: &str,
    instances_json: &str,
    logical_sessions_json: &str,
) -> ConsoleServiceMessage {
    ConsoleServiceMessage {
        msg_type: ConsoleServiceMessageType::KConsoleServiceHeartBeat as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: Some(ConsoleServiceHeartBeat {
            hb_index,
            device_id: device_id.to_string(),
            render_alive,
            auth_info_json: auth_info_json.to_string(),
            instances_json: instances_json.to_string(),
            logical_sessions_json: logical_sessions_json.to_string(),
        }),
        start_app_instance: None,
        stop_app_instance: None,
        start_app_instance_result: None,
        stop_app_instance_result: None,
        create_wall_session: None,
        create_wall_session_result: None,
        ..Default::default()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ConsoleInboundCommand {
    StartApp(StartAppRequest),
    StopApp {
        request_id: String,
        instance_id: String,
    },
    CreateWallSession(ConsoleServiceCreateWallSession),
    RedeemTicketResult(ConsoleServiceRedeemConnectionTicketResult),
    RtcIceConfigChanged(u64),
}

/// Parse a Console binary frame into an inbound command (if any).
pub fn parse_console_inbound(bytes: &[u8]) -> Result<Option<ConsoleInboundCommand>, String> {
    let msg = ConsoleServiceMessage::decode(bytes).map_err(|e| e.to_string())?;
    match ConsoleServiceMessageType::try_from(msg.msg_type) {
        Ok(ConsoleServiceMessageType::KConsoleServiceStartAppInstance) => {
            let s = msg.start_app_instance.ok_or("missing start_app_instance")?;
            Ok(Some(ConsoleInboundCommand::StartApp(StartAppRequest {
                request_id: s.request_id,
                instance_id: s.instance_id,
                app_id: s.app_id,
                install_root: s.install_root,
                game_exe_rel: s.game_exe_rel,
                game_arguments: s.game_arguments,
                listen_port: s.listen_port,
                encoder_fps: s.encoder_fps,
                encoder_bitrate: s.encoder_bitrate,
                encoder_format: s.encoder_format,
                webrtc_enabled: s.webrtc_enabled,
                websocket_enabled: s.websocket_enabled,
                live_stream_id: s.live_stream_id,
                push_rtmp_url: s.push_rtmp_url,
                app_mode: s.app_mode,
                webview_url_b64: s.webview_url_b64,
                device_id: s.device_id,
                relay_device_id: s.relay_device_id,
                relay_server_host: s.relay_server_host,
                relay_server_port: s.relay_server_port,
                relay_appkey: s.relay_appkey,
            })))
        }
        Ok(ConsoleServiceMessageType::KConsoleServiceStopAppInstance) => {
            let s = msg.stop_app_instance.ok_or("missing stop_app_instance")?;
            Ok(Some(ConsoleInboundCommand::StopApp {
                request_id: s.request_id,
                instance_id: s.instance_id,
            }))
        }
        Ok(ConsoleServiceMessageType::KConsoleServiceCreateWallSession) => {
            Ok(Some(ConsoleInboundCommand::CreateWallSession(
                msg.create_wall_session
                    .ok_or("missing create_wall_session")?,
            )))
        }
        Ok(ConsoleServiceMessageType::KConsoleServiceRedeemConnectionTicketResult) => {
            Ok(Some(ConsoleInboundCommand::RedeemTicketResult(
                msg.redeem_connection_ticket_result
                    .ok_or("missing redeem_connection_ticket_result")?,
            )))
        }
        Ok(ConsoleServiceMessageType::KRtcIceConfigChanged) => {
            Ok(Some(ConsoleInboundCommand::RtcIceConfigChanged(
                msg.rtc_ice_config_changed
                    .ok_or("missing rtc_ice_config_changed")?
                    .revision,
            )))
        }
        Ok(ConsoleServiceMessageType::KConsoleServiceHello)
        | Ok(ConsoleServiceMessageType::KConsoleServiceHeartBeat)
        | Ok(ConsoleServiceMessageType::KConsoleServiceStartAppInstanceResult)
        | Ok(ConsoleServiceMessageType::KConsoleServiceStopAppInstanceResult)
        | Ok(ConsoleServiceMessageType::KConsoleServiceCreateWallSessionResult) => Ok(None),
        Ok(ConsoleServiceMessageType::KConsoleServiceRedeemConnectionTicket) => {
            Err("Console must not send ticket redemption requests".to_string())
        }
        Err(_) => Err(format!("unknown console service msg_type {}", msg.msg_type)),
    }
}

fn redeem_ticket_message(
    device_id: &str,
    request_id: &str,
    ticket: &str,
    client_nonce: &str,
    instance_id: &str,
) -> ConsoleServiceMessage {
    ConsoleServiceMessage {
        msg_type: ConsoleServiceMessageType::KConsoleServiceRedeemConnectionTicket as i32,
        device_id: device_id.to_string(),
        redeem_connection_ticket: Some(ConsoleServiceRedeemConnectionTicket {
            request_id: request_id.to_string(),
            device_id: device_id.to_string(),
            ticket: ticket.to_string(),
            client_nonce: client_nonce.to_string(),
            instance_id: instance_id.to_string(),
        }),
        ..Default::default()
    }
}

pub fn start_app_result_message(
    device_id: &str,
    request_id: &str,
    instance_id: &str,
    ok: bool,
    error: &str,
    listen_port: i32,
    pid: u32,
) -> ConsoleServiceMessage {
    ConsoleServiceMessage {
        msg_type: ConsoleServiceMessageType::KConsoleServiceStartAppInstanceResult as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: None,
        start_app_instance: None,
        stop_app_instance: None,
        start_app_instance_result: Some(ConsoleServiceStartAppInstanceResult {
            request_id: request_id.to_string(),
            instance_id: instance_id.to_string(),
            ok,
            error: error.to_string(),
            listen_port,
            pid,
        }),
        stop_app_instance_result: None,
        create_wall_session: None,
        create_wall_session_result: None,
        ..Default::default()
    }
}

pub fn stop_app_result_message(
    device_id: &str,
    request_id: &str,
    instance_id: &str,
    ok: bool,
    error: &str,
) -> ConsoleServiceMessage {
    ConsoleServiceMessage {
        msg_type: ConsoleServiceMessageType::KConsoleServiceStopAppInstanceResult as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: None,
        start_app_instance: None,
        stop_app_instance: None,
        start_app_instance_result: None,
        stop_app_instance_result: Some(ConsoleServiceStopAppInstanceResult {
            request_id: request_id.to_string(),
            instance_id: instance_id.to_string(),
            ok,
            error: error.to_string(),
        }),
        create_wall_session: None,
        create_wall_session_result: None,
        ..Default::default()
    }
}

fn wall_session_result_message(
    device_id: &str,
    request_id: &str,
    session_id: &str,
    result: Result<String, String>,
) -> ConsoleServiceMessage {
    let (ok, answer_sdp, error) = match result {
        Ok(answer) => (true, answer, String::new()),
        Err(error) => (false, String::new(), error),
    };
    ConsoleServiceMessage {
        msg_type: ConsoleServiceMessageType::KConsoleServiceCreateWallSessionResult as i32,
        device_id: device_id.to_string(),
        create_wall_session_result: Some(ConsoleServiceCreateWallSessionResult {
            request_id: request_id.to_string(),
            session_id: session_id.to_string(),
            ok,
            error,
            answer_sdp,
        }),
        ..Default::default()
    }
}

async fn create_wall_session_on_local_render(
    request: &ConsoleServiceCreateWallSession,
) -> Result<String, String> {
    if request.render_port <= 0 || request.render_port > u16::MAX as i32 {
        return Err("invalid render port".to_string());
    }
    if request.device_id.is_empty()
        || request.session_id.is_empty()
        || request.safety_pwd_md5.is_empty()
        || request.offer_sdp.is_empty()
    {
        return Err("invalid wall session request".to_string());
    }
    let url = format!("http://127.0.0.1:{}/alloc/local/rtc", request.render_port);
    let response = reqwest::Client::builder()
        .connect_timeout(Duration::from_secs(2))
        .timeout(Duration::from_secs(13))
        .build()
        .map_err(|err| err.to_string())?
        .post(url)
        .query(&[
            ("device_id", request.device_id.as_str()),
            ("stream_id", request.session_id.as_str()),
            ("safety_pwd_md5", request.safety_pwd_md5.as_str()),
            ("session_role", "wall_observer"),
        ])
        .json(&serde_json::json!({ "sdp": &request.offer_sdp }))
        .send()
        .await
        .map_err(|err| format!("local render unavailable: {err}"))?;
    let status = response.status();
    let body: serde_json::Value = response
        .json()
        .await
        .map_err(|err| format!("invalid render response: {err}"))?;
    let code = body
        .get("code")
        .and_then(serde_json::Value::as_i64)
        .unwrap_or_default();
    let answer = body
        .get("data")
        .and_then(|value| value.get("answer_sdp"))
        .and_then(serde_json::Value::as_str)
        .unwrap_or_default();
    if status.is_success() && code == 200 && !answer.is_empty() {
        Ok(answer.to_string())
    } else {
        let message = body
            .get("message")
            .and_then(serde_json::Value::as_str)
            .unwrap_or("render rejected wall session");
        Err(format!("{message} (HTTP {status}, code {code})"))
    }
}

pub fn encode_start_app_command(device_id: &str, req: &StartAppRequest) -> Vec<u8> {
    ConsoleServiceMessage {
        msg_type: ConsoleServiceMessageType::KConsoleServiceStartAppInstance as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: None,
        start_app_instance: Some(ConsoleServiceStartAppInstance {
            request_id: req.request_id.clone(),
            instance_id: req.instance_id.clone(),
            app_id: req.app_id.clone(),
            install_root: req.install_root.clone(),
            game_exe_rel: req.game_exe_rel.clone(),
            game_arguments: req.game_arguments.clone(),
            listen_port: req.listen_port,
            encoder_fps: req.encoder_fps,
            encoder_bitrate: req.encoder_bitrate,
            encoder_format: req.encoder_format.clone(),
            webrtc_enabled: req.webrtc_enabled,
            websocket_enabled: req.websocket_enabled,
            live_stream_id: req.live_stream_id.clone(),
            push_rtmp_url: req.push_rtmp_url.clone(),
            app_mode: req.app_mode.clone(),
            webview_url_b64: req.webview_url_b64.clone(),
            device_id: req.device_id.clone(),
            relay_device_id: req.relay_device_id.clone(),
            relay_server_host: req.relay_server_host.clone(),
            relay_server_port: req.relay_server_port,
            relay_appkey: req.relay_appkey.clone(),
        }),
        stop_app_instance: None,
        start_app_instance_result: None,
        stop_app_instance_result: None,
        create_wall_session: None,
        create_wall_session_result: None,
        ..Default::default()
    }
    .encode_to_vec()
}

pub fn encode_stop_app_command(device_id: &str, request_id: &str, instance_id: &str) -> Vec<u8> {
    ConsoleServiceMessage {
        msg_type: ConsoleServiceMessageType::KConsoleServiceStopAppInstance as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: None,
        start_app_instance: None,
        stop_app_instance: Some(ConsoleServiceStopAppInstance {
            request_id: request_id.to_string(),
            instance_id: instance_id.to_string(),
        }),
        start_app_instance_result: None,
        stop_app_instance_result: None,
        create_wall_session: None,
        create_wall_session_result: None,
        ..Default::default()
    }
    .encode_to_vec()
}

fn encode_message(message: &ConsoleServiceMessage) -> Vec<u8> {
    message.encode_to_vec()
}

/// Handle a Console command in its own task so the receive loop stays responsive.
/// The result message is sent back asynchronously when the operation finishes.
fn spawn_console_command_handler(
    runtime: Arc<Mutex<ServiceRuntime>>,
    sender: Arc<Mutex<Option<WsSink>>>,
    cmd: ConsoleInboundCommand,
) {
    tokio::spawn(async move {
        let device_id = {
            let guard = runtime.lock().await;
            guard
                .state
                .last_auth_info
                .as_ref()
                .map(|a| a.device_id.clone())
                .unwrap_or_default()
        };
        let reply = match cmd {
            ConsoleInboundCommand::StartApp(req) => {
                let request_id = req.request_id.clone();
                let instance_id = req.instance_id.clone();
                match ServiceRuntime::start_app_instance(&runtime, req).await {
                    Ok((port, pid)) => Some(encode_message(&start_app_result_message(
                        &device_id,
                        &request_id,
                        &instance_id,
                        true,
                        "",
                        port as i32,
                        pid,
                    ))),
                    Err(err) => {
                        error!("console start app failed: {err}");
                        Some(encode_message(&start_app_result_message(
                            &device_id,
                            &request_id,
                            &instance_id,
                            false,
                            &err,
                            0,
                            0,
                        )))
                    }
                }
            }
            ConsoleInboundCommand::StopApp {
                request_id,
                instance_id,
            } => match ServiceRuntime::stop_app_instance(&runtime, &instance_id).await {
                Ok(()) => Some(encode_message(&stop_app_result_message(
                    &device_id,
                    &request_id,
                    &instance_id,
                    true,
                    "",
                ))),
                Err(err) => {
                    // Service restarted / registry lost: treat as already stopped so Console
                    // does not stick in Stopping with "unknown instance_id".
                    if err.contains("unknown instance_id") {
                        warn!("console stop: {err} — treat as already stopped");
                        Some(encode_message(&stop_app_result_message(
                            &device_id,
                            &request_id,
                            &instance_id,
                            true,
                            "",
                        )))
                    } else {
                        error!("console stop app failed: {err}");
                        Some(encode_message(&stop_app_result_message(
                            &device_id,
                            &request_id,
                            &instance_id,
                            false,
                            &err,
                        )))
                    }
                }
            },
            ConsoleInboundCommand::CreateWallSession(request) => {
                let result = create_wall_session_on_local_render(&request).await;
                if let Err(err) = &result {
                    warn!("create wall session failed: {err}");
                }
                Some(encode_message(&wall_session_result_message(
                    &device_id,
                    &request.request_id,
                    &request.session_id,
                    result,
                )))
            }
            ConsoleInboundCommand::RedeemTicketResult(_) => None,
            ConsoleInboundCommand::RtcIceConfigChanged(_) => None,
        };
        if let Some(frame) = reply {
            if !send_frame(&sender, frame).await {
                warn!("send console command result failed");
            }
        }
    });
}

/// Serialize the panel-pushed authorization info as a JSON object, field by field.
fn auth_info_to_json(info: &MsgAuthInfo) -> String {
    serde_json::json!({
        "device_id": info.device_id,
        "auth_id": info.auth_id,
        "auth_name": info.auth_name,
        "machine_code": info.machine_code,
        "appkey": info.appkey,
        "role": info.role,
        "days": info.days,
        "max_streams": info.max_streams,
        "end_timestamp_ms": info.end_timestamp_ms,
        "console_host": info.console_host,
        "console_port": info.console_port,
        "console_ssl": info.console_ssl,
        // Compatibility for an older Console server parsing heartbeat JSON.
        "cms_host": info.console_host,
        "cms_port": info.console_port,
        "cms_ssl": info.console_ssl,
    })
    .to_string()
}

/// The Console serves a self-signed certificate, so skip rustls certificate
/// verification (the connection token in the URL is the actual credential).
#[derive(Debug)]
struct NoCertVerifier;

impl ServerCertVerifier for NoCertVerifier {
    fn verify_server_cert(
        &self,
        _end_entity: &CertificateDer<'_>,
        _intermediates: &[CertificateDer<'_>],
        _server_name: &ServerName<'_>,
        _ocsp_response: &[u8],
        _now: UnixTime,
    ) -> Result<ServerCertVerified, rustls::Error> {
        Ok(ServerCertVerified::assertion())
    }

    fn verify_tls12_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        Ok(HandshakeSignatureValid::assertion())
    }

    fn verify_tls13_signature(
        &self,
        _message: &[u8],
        _cert: &CertificateDer<'_>,
        _dss: &DigitallySignedStruct,
    ) -> Result<HandshakeSignatureValid, rustls::Error> {
        Ok(HandshakeSignatureValid::assertion())
    }

    fn supported_verify_schemes(&self) -> Vec<SignatureScheme> {
        vec![
            SignatureScheme::RSA_PKCS1_SHA256,
            SignatureScheme::RSA_PKCS1_SHA384,
            SignatureScheme::RSA_PKCS1_SHA512,
            SignatureScheme::ECDSA_NISTP256_SHA256,
            SignatureScheme::ECDSA_NISTP384_SHA384,
            SignatureScheme::ECDSA_NISTP521_SHA512,
            SignatureScheme::RSA_PSS_SHA256,
            SignatureScheme::RSA_PSS_SHA384,
            SignatureScheme::RSA_PSS_SHA512,
            SignatureScheme::ED25519,
        ]
    }
}

fn tls_connector() -> Connector {
    // 依赖图里同时存在 aws-lc-rs 与 ring 两个 provider 时,rustls 无法自动
    // 选择,ClientConfig::builder() 会 panic。显式安装默认 provider(幂等)。
    let _ = rustls::crypto::aws_lc_rs::default_provider().install_default();
    let config = rustls::ClientConfig::builder()
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(NoCertVerifier))
        .with_no_client_auth();
    Connector::Rustls(Arc::new(config))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_auth_info() -> MsgAuthInfo {
        MsgAuthInfo {
            device_id: "dev-1".to_string(),
            auth_id: "aid-1".to_string(),
            auth_name: "license".to_string(),
            machine_code: "mc".to_string(),
            appkey: "ak-1".to_string(),
            role: 2,
            days: 365,
            max_streams: 4,
            end_timestamp_ms: 1_900_000_000_000,
            console_host: "console.example.com".to_string(),
            console_port: 8443,
            console_ssl: true,
        }
    }

    #[test]
    fn legacy_auth_info_cannot_disable_console_tls() {
        let mut info = sample_auth_info();
        info.console_ssl = false;
        assert!(require_console_tls(info).console_ssl);
    }

    #[test]
    fn url_contains_all_query_params() {
        let token = ConnectionToken {
            token: "deadbeef".to_string(),
            ts: 1234567890,
            nonce: "cafe".to_string(),
        };
        let url = build_console_url("console.example.com", 8443, true, "ak-1", &token, "dev-1");
        assert_eq!(
            url,
            "wss://console.example.com:8443/console/service?appkey=ak-1&token=deadbeef&ts=1234567890&nonce=cafe&device_id=dev-1"
        );
    }

    #[test]
    fn legacy_ssl_off_cannot_downgrade_url() {
        let token = ConnectionToken {
            token: "deadbeef".to_string(),
            ts: 1234567890,
            nonce: "cafe".to_string(),
        };
        let url = build_console_url("console.example.com", 8080, false, "ak-1", &token, "dev-1");
        assert!(url.starts_with("wss://console.example.com:8080/console/service?"));
    }

    #[test]
    fn log_endpoint_carries_no_credentials() {
        let endpoint = console_endpoint("console.example.com", 8443, true);
        assert_eq!(endpoint, "wss://console.example.com:8443/console/service");
        assert!(!endpoint.contains("appkey"));
        assert!(!endpoint.contains("token"));
        assert_eq!(
            console_endpoint("console.example.com", 8080, false),
            "wss://console.example.com:8080/console/service"
        );
    }

    #[test]
    fn auth_info_json_covers_all_fields() {
        let json = auth_info_to_json(&sample_auth_info());
        let value: serde_json::Value = serde_json::from_str(&json).unwrap();
        assert_eq!(value["device_id"], "dev-1");
        assert_eq!(value["auth_id"], "aid-1");
        assert_eq!(value["auth_name"], "license");
        assert_eq!(value["machine_code"], "mc");
        assert_eq!(value["appkey"], "ak-1");
        assert_eq!(value["role"], 2);
        assert_eq!(value["days"], 365);
        assert_eq!(value["max_streams"], 4);
        assert_eq!(value["end_timestamp_ms"], 1_900_000_000_000i64);
        assert_eq!(value["console_host"], "console.example.com");
        assert_eq!(value["console_port"], 8443);
        assert_eq!(value["console_ssl"], true);
    }

    #[test]
    fn hello_message_carries_device_appkey_version() {
        let message = hello_message("dev-1", "ak-1");
        assert_eq!(
            message.msg_type,
            ConsoleServiceMessageType::KConsoleServiceHello
        );
        assert_eq!(message.device_id, "dev-1");
        let hello = message.hello.unwrap();
        assert_eq!(hello.device_id, "dev-1");
        assert_eq!(hello.appkey, "ak-1");
        assert_eq!(hello.version, env!("CARGO_PKG_VERSION"));
        assert!(message.heartbeat.is_none());
    }

    #[test]
    fn heartbeat_message_carries_index_and_liveness() {
        let message =
            heartbeat_message(7, "dev-1", true, "{\"a\":1}", "[{\"instance_id\":\"i1\"}]", "[]");
        assert_eq!(
            message.msg_type,
            ConsoleServiceMessageType::KConsoleServiceHeartBeat
        );
        let heartbeat = message.heartbeat.unwrap();
        assert_eq!(heartbeat.instances_json, "[{\"instance_id\":\"i1\"}]");
        assert_eq!(heartbeat.hb_index, 7);
        assert_eq!(heartbeat.device_id, "dev-1");
        assert!(heartbeat.render_alive);
        assert_eq!(heartbeat.auth_info_json, "{\"a\":1}");
        assert!(message.hello.is_none());
    }

    #[test]
    fn encoded_hello_round_trips() {
        let bytes = encode_message(&hello_message("dev-1", "ak-1"));
        let decoded = ConsoleServiceMessage::decode(bytes.as_slice()).unwrap();
        assert_eq!(
            decoded.msg_type,
            ConsoleServiceMessageType::KConsoleServiceHello
        );
        assert_eq!(decoded.hello.unwrap().appkey, "ak-1");
    }

    #[test]
    fn parse_start_and_stop_commands_round_trip() {
        let req = StartAppRequest {
            request_id: "req-1".into(),
            instance_id: "inst-1".into(),
            app_id: "app-1".into(),
            app_mode: "game-hook".into(),
            webview_url_b64: String::new(),
            install_root: r"D:\apps\Car".into(),
            game_exe_rel: r"Binaries\game.exe".into(),
            game_arguments: "-dx11".into(),
            listen_port: 32000,
            encoder_fps: 60,
            encoder_bitrate: 20,
            encoder_format: "h264".into(),
            webrtc_enabled: true,
            websocket_enabled: true,
            live_stream_id: "device-test__app__app-test".into(),
            push_rtmp_url: "rtmp://127.0.0.1:1935/live/{live_stream_id}".into(),
            device_id: "device-test".into(),
            relay_device_id: "device-test__instance__inst-1".into(),
            relay_server_host: "console.test".into(),
            relay_server_port: 30502,
            relay_appkey: "app-key".into(),
        };
        let bytes = encode_start_app_command("dev-1", &req);
        match parse_console_inbound(&bytes).unwrap().unwrap() {
            ConsoleInboundCommand::StartApp(got) => {
                assert_eq!(got.instance_id, "inst-1");
                assert_eq!(got.install_root, r"D:\apps\Car");
                assert_eq!(got.listen_port, 32000);
            }
            other => panic!("unexpected {other:?}"),
        }

        let stop = encode_stop_app_command("dev-1", "req-2", "inst-1");
        match parse_console_inbound(&stop).unwrap().unwrap() {
            ConsoleInboundCommand::StopApp {
                request_id,
                instance_id,
            } => {
                assert_eq!(request_id, "req-2");
                assert_eq!(instance_id, "inst-1");
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn start_result_message_encodes_ok_and_error() {
        let ok = start_app_result_message("dev-1", "r", "i", true, "", 32001, 99);
        assert_eq!(
            ok.msg_type,
            ConsoleServiceMessageType::KConsoleServiceStartAppInstanceResult
        );
        let body = ok.start_app_instance_result.unwrap();
        assert!(body.ok);
        assert_eq!(body.listen_port, 32001);
        assert_eq!(body.pid, 99);

        let fail = start_app_result_message("dev-1", "r", "i", false, "boom", 0, 0);
        assert!(!fail.start_app_instance_result.unwrap().ok);
    }

    #[test]
    fn wall_session_command_and_result_round_trip() {
        let command = ConsoleServiceMessage {
            msg_type: ConsoleServiceMessageType::KConsoleServiceCreateWallSession as i32,
            device_id: "dev-1".into(),
            create_wall_session: Some(ConsoleServiceCreateWallSession {
                request_id: "wall-req-1".into(),
                session_id: "wall-session-1".into(),
                device_id: "dev-1".into(),
                render_port: 20371,
                safety_pwd_md5: "secret-md5".into(),
                offer_sdp: "v=0\r\nthis-is-a-test-offer".into(),
            }),
            ..Default::default()
        };

        match parse_console_inbound(&command.encode_to_vec())
            .unwrap()
            .unwrap()
        {
            ConsoleInboundCommand::CreateWallSession(got) => {
                assert_eq!(got.request_id, "wall-req-1");
                assert_eq!(got.session_id, "wall-session-1");
                assert_eq!(got.render_port, 20371);
                assert_eq!(got.safety_pwd_md5, "secret-md5");
            }
            other => panic!("unexpected {other:?}"),
        }

        let response = wall_session_result_message(
            "dev-1",
            "wall-req-1",
            "wall-session-1",
            Ok("v=0\r\nthis-is-a-test-answer".into()),
        );
        let decoded = ConsoleServiceMessage::decode(response.encode_to_vec().as_slice()).unwrap();
        let body = decoded.create_wall_session_result.unwrap();
        assert!(body.ok);
        assert_eq!(body.request_id, "wall-req-1");
        assert_eq!(body.session_id, "wall-session-1");
        assert!(body.answer_sdp.contains("test-answer"));
        assert!(body.error.is_empty());
    }
}
