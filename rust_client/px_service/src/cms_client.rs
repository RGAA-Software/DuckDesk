use std::sync::Arc;
use std::time::Duration;

use futures_util::stream::SplitSink;
use futures_util::{SinkExt, StreamExt};
use prost::Message;
use rustls::client::danger::{
    HandshakeSignatureValid, ServerCertVerified, ServerCertVerifier,
};
use rustls::pki_types::{CertificateDer, ServerName, UnixTime};
use rustls::{DigitallySignedStruct, SignatureScheme};
use service_core::MsgAuthInfo;
use tokio::net::TcpStream;
use tokio::sync::Mutex;
use tokio::time::{sleep, timeout};
use tokio_tungstenite::tungstenite::Message as TungsteniteMessage;
use tokio_tungstenite::{Connector, MaybeTlsStream, WebSocketStream};
use tracing::{error, info, warn};

use px_auth_mgr::app_secret_util::calculate_app_secret;
use px_auth_mgr::auth_token::{generate_connection_token, ConnectionToken};
use protocol::cms_service::{
    CmsServiceHeartBeat, CmsServiceHello, CmsServiceMessage, CmsServiceMessageType,
    CmsServiceStartAppInstance, CmsServiceStartAppInstanceResult, CmsServiceStopAppInstance,
    CmsServiceStopAppInstanceResult,
};
use service_core::StartAppRequest;

use crate::service_host::ServiceRuntime;

type WsSink = SplitSink<WebSocketStream<MaybeTlsStream<TcpStream>>, TungsteniteMessage>;

const CONNECT_TIMEOUT_SECS: u64 = 5;
const RECONNECT_DELAY_SECS: u64 = 2;
const HEARTBEAT_INTERVAL_SECS: u64 = 3;
const AUTH_INFO_POLL_SECS: u64 = 1;

/// WS/WSS client loop towards the CMS (px_cms_server) `/cms/service` endpoint.
///
/// The CMS address (cms_host/cms_port) and whether TLS is used (cms_ssl),
/// appkey and device_id all come from the authorization info the panel pushes
/// to this service, so the loop first waits until `state.last_auth_info` is
/// present. A fresh connection token is generated on every reconnect.
pub async fn cms_client_loop(runtime: Arc<Mutex<ServiceRuntime>>) -> Result<(), String> {
    let mut stop_rx = {
        let guard = runtime.lock().await;
        guard.subscribe_stop()
    };
    let sender: Arc<Mutex<Option<WsSink>>> = Arc::new(Mutex::new(None));
    let mut hb_index: i64 = 0;

    loop {
        // wait until the panel has delivered the authorization info
        let Some(auth_info) = wait_auth_info(&runtime, &mut stop_rx).await else {
            info!("cms client loop stopped before auth info arrived");
            return Ok(());
        };
        info!(
            "cms auth info ready, cms={}:{} device_id={}",
            auth_info.cms_host, auth_info.cms_port, auth_info.device_id
        );

        // regenerate the connection token on every (re)connect
        let app_secret = calculate_app_secret(auth_info.appkey.clone());
        let token = generate_connection_token(&auth_info.appkey, &app_secret);
        let url = build_cms_url(
            &auth_info.cms_host,
            auth_info.cms_port,
            auth_info.cms_ssl,
            &auth_info.appkey,
            &token,
            &auth_info.device_id,
        );

        *sender.lock().await = None;
        // Endpoint without credentials for logs (the URL carries appkey+token).
        let endpoint = cms_endpoint(&auth_info.cms_host, auth_info.cms_port, auth_info.cms_ssl);
        // the wss branch keeps the existing TLS config (certificate verification off);
        // both connect futures yield the same stream type, so wrap them in one async block
        let connect = async {
            if auth_info.cms_ssl {
                tokio_tungstenite::connect_async_tls_with_config(
                    url.clone(),
                    None,
                    false,
                    Some(tls_connector()),
                )
                .await
            } else {
                tokio_tungstenite::connect_async(url.clone()).await
            }
        };
        let stream = match timeout(Duration::from_secs(CONNECT_TIMEOUT_SECS), connect).await {
            Ok(Ok((stream, _response))) => {
                info!("connected to cms {endpoint}");
                stream
            }
            Ok(Err(err)) => {
                error!("connect to cms {endpoint} failed: {err}");
                sleep(Duration::from_secs(RECONNECT_DELAY_SECS)).await;
                continue;
            }
            Err(_) => {
                error!("connect to cms {endpoint} timed out");
                sleep(Duration::from_secs(RECONNECT_DELAY_SECS)).await;
                continue;
            }
        };

        let (sink, mut receiver) = stream.split();
        *sender.lock().await = Some(sink);

        // say hello right after connecting
        let hello = encode_message(&hello_message(&auth_info.device_id, &auth_info.appkey));
        if !send_frame(&sender, hello).await {
            warn!("send hello to cms failed, reconnecting");
            sleep(Duration::from_secs(RECONNECT_DELAY_SECS)).await;
            continue;
        }

        // heartbeat task: reports render liveness + the latest auth info as JSON
        let hb_sender = sender.clone();
        let hb_runtime = runtime.clone();
        let device_id = auth_info.device_id.clone();
        let mut hb_stop_rx = {
            let guard = runtime.lock().await;
            guard.subscribe_stop()
        };
        let heartbeat_task = tokio::spawn(async move {
            let mut interval =
                tokio::time::interval(Duration::from_secs(HEARTBEAT_INTERVAL_SECS));
            loop {
                tokio::select! {
                    _ = interval.tick() => {
                        hb_index += 1;
                        let (render_alive, auth_json, instances_json) = {
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
                            )
                        };
                        let frame = encode_message(&heartbeat_message(
                            hb_index,
                            &device_id,
                            render_alive,
                            &auth_json,
                            &instances_json,
                        ));
                        if !send_frame(&hb_sender, frame).await {
                            break;
                        }
                    }
                    _ = hb_stop_rx.recv() => break,
                }
            }
        });

        // receive loop: handle Start/Stop app commands from CMS
        let mut should_stop = false;
        loop {
            tokio::select! {
                msg = receiver.next() => {
                    match msg {
                        Some(Ok(TungsteniteMessage::Close(_))) => {
                            warn!("cms closed the connection");
                            break;
                        }
                        Some(Ok(TungsteniteMessage::Binary(bin))) => {
                            match parse_cms_inbound(&bin) {
                                Ok(Some(cmd)) => {
                                    // Start/Stop 可能耗时数秒(等 render 监听端口、
                                    // 等游戏进程),派生独立任务处理并异步回传结果,
                                    // 接收循环与心跳不被阻塞。
                                    spawn_cms_command_handler(
                                        runtime.clone(),
                                        sender.clone(),
                                        cmd,
                                    );
                                }
                                Ok(None) => {}
                                Err(err) => warn!("cms inbound parse error: {err}"),
                            }
                        }
                        Some(Ok(_)) => {}
                        Some(Err(err)) => {
                            error!("cms receive error: {err}");
                            break;
                        }
                        None => {
                            warn!("cms connection ended");
                            break;
                        }
                    }
                }
                _ = stop_rx.recv() => {
                    info!("cms client loop received stop signal");
                    should_stop = true;
                    break;
                }
            }
        }

        heartbeat_task.abort();
        *sender.lock().await = None;
        if should_stop {
            return Ok(());
        }
        info!(
            "will reconnect to cms, sender strong_count={}",
            Arc::strong_count(&sender)
        );
        sleep(Duration::from_secs(RECONNECT_DELAY_SECS)).await;
    }
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
        error!("no cms sender available");
        return false;
    };
    match sink.send(TungsteniteMessage::Binary(frame.into())).await {
        Ok(()) => true,
        Err(err) => {
            error!("send to cms failed: {err}");
            false
        }
    }
}

/// Endpoint string safe for logs: no appkey/token query params.
fn cms_endpoint(host: &str, port: i32, ssl: bool) -> String {
    let scheme = if ssl { "wss" } else { "ws" };
    format!("{scheme}://{host}:{port}/cms/service")
}

fn build_cms_url(
    host: &str,
    port: i32,
    ssl: bool,
    appkey: &str,
    token: &ConnectionToken,
    device_id: &str,
) -> String {
    let scheme = if ssl { "wss" } else { "ws" };
    format!(
        "{scheme}://{host}:{port}/cms/service?appkey={appkey}&token={}&ts={}&nonce={}&device_id={device_id}",
        token.token, token.ts, token.nonce
    )
}

fn hello_message(device_id: &str, appkey: &str) -> CmsServiceMessage {
    CmsServiceMessage {
        msg_type: CmsServiceMessageType::KCmsServiceHello as i32,
        device_id: device_id.to_string(),
        hello: Some(CmsServiceHello {
            device_id: device_id.to_string(),
            appkey: appkey.to_string(),
            version: env!("CARGO_PKG_VERSION").to_string(),
        }),
        heartbeat: None,
        start_app_instance: None,
        stop_app_instance: None,
        start_app_instance_result: None,
        stop_app_instance_result: None,
    }
}

fn heartbeat_message(
    hb_index: i64,
    device_id: &str,
    render_alive: bool,
    auth_info_json: &str,
    instances_json: &str,
) -> CmsServiceMessage {
    CmsServiceMessage {
        msg_type: CmsServiceMessageType::KCmsServiceHeartBeat as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: Some(CmsServiceHeartBeat {
            hb_index,
            device_id: device_id.to_string(),
            render_alive,
            auth_info_json: auth_info_json.to_string(),
            instances_json: instances_json.to_string(),
        }),
        start_app_instance: None,
        stop_app_instance: None,
        start_app_instance_result: None,
        stop_app_instance_result: None,
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CmsInboundCommand {
    StartApp(StartAppRequest),
    StopApp {
        request_id: String,
        instance_id: String,
    },
}

/// Parse a CMS binary frame into an inbound command (if any).
pub fn parse_cms_inbound(bytes: &[u8]) -> Result<Option<CmsInboundCommand>, String> {
    let msg = CmsServiceMessage::decode(bytes).map_err(|e| e.to_string())?;
    match CmsServiceMessageType::try_from(msg.msg_type) {
        Ok(CmsServiceMessageType::KCmsServiceStartAppInstance) => {
            let s = msg
                .start_app_instance
                .ok_or("missing start_app_instance")?;
            Ok(Some(CmsInboundCommand::StartApp(StartAppRequest {
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
            })))
        }
        Ok(CmsServiceMessageType::KCmsServiceStopAppInstance) => {
            let s = msg.stop_app_instance.ok_or("missing stop_app_instance")?;
            Ok(Some(CmsInboundCommand::StopApp {
                request_id: s.request_id,
                instance_id: s.instance_id,
            }))
        }
        Ok(CmsServiceMessageType::KCmsServiceHello)
        | Ok(CmsServiceMessageType::KCmsServiceHeartBeat)
        | Ok(CmsServiceMessageType::KCmsServiceStartAppInstanceResult)
        | Ok(CmsServiceMessageType::KCmsServiceStopAppInstanceResult) => Ok(None),
        Err(_) => Err(format!("unknown cms service msg_type {}", msg.msg_type)),
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
) -> CmsServiceMessage {
    CmsServiceMessage {
        msg_type: CmsServiceMessageType::KCmsServiceStartAppInstanceResult as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: None,
        start_app_instance: None,
        stop_app_instance: None,
        start_app_instance_result: Some(CmsServiceStartAppInstanceResult {
            request_id: request_id.to_string(),
            instance_id: instance_id.to_string(),
            ok,
            error: error.to_string(),
            listen_port,
            pid,
        }),
        stop_app_instance_result: None,
    }
}

pub fn stop_app_result_message(
    device_id: &str,
    request_id: &str,
    instance_id: &str,
    ok: bool,
    error: &str,
) -> CmsServiceMessage {
    CmsServiceMessage {
        msg_type: CmsServiceMessageType::KCmsServiceStopAppInstanceResult as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: None,
        start_app_instance: None,
        stop_app_instance: None,
        start_app_instance_result: None,
        stop_app_instance_result: Some(CmsServiceStopAppInstanceResult {
            request_id: request_id.to_string(),
            instance_id: instance_id.to_string(),
            ok,
            error: error.to_string(),
        }),
    }
}

pub fn encode_start_app_command(device_id: &str, req: &StartAppRequest) -> Vec<u8> {
    CmsServiceMessage {
        msg_type: CmsServiceMessageType::KCmsServiceStartAppInstance as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: None,
        start_app_instance: Some(CmsServiceStartAppInstance {
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
        }),
        stop_app_instance: None,
        start_app_instance_result: None,
        stop_app_instance_result: None,
    }
    .encode_to_vec()
}

pub fn encode_stop_app_command(device_id: &str, request_id: &str, instance_id: &str) -> Vec<u8> {
    CmsServiceMessage {
        msg_type: CmsServiceMessageType::KCmsServiceStopAppInstance as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: None,
        start_app_instance: None,
        stop_app_instance: Some(CmsServiceStopAppInstance {
            request_id: request_id.to_string(),
            instance_id: instance_id.to_string(),
        }),
        start_app_instance_result: None,
        stop_app_instance_result: None,
    }
    .encode_to_vec()
}

fn encode_message(message: &CmsServiceMessage) -> Vec<u8> {
    message.encode_to_vec()
}

/// Handle a CMS command in its own task so the receive loop stays responsive.
/// The result message is sent back asynchronously when the operation finishes.
fn spawn_cms_command_handler(
    runtime: Arc<Mutex<ServiceRuntime>>,
    sender: Arc<Mutex<Option<WsSink>>>,
    cmd: CmsInboundCommand,
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
            CmsInboundCommand::StartApp(req) => {
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
                        error!("cms start app failed: {err}");
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
            CmsInboundCommand::StopApp {
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
                    // Service restarted / registry lost: treat as already stopped so CMS
                    // does not stick in Stopping with "unknown instance_id".
                    if err.contains("unknown instance_id") {
                        warn!("cms stop: {err} — treat as already stopped");
                        Some(encode_message(&stop_app_result_message(
                            &device_id,
                            &request_id,
                            &instance_id,
                            true,
                            "",
                        )))
                    } else {
                        error!("cms stop app failed: {err}");
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
        };
        if let Some(frame) = reply {
            if !send_frame(&sender, frame).await {
                warn!("send cms command result failed");
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
        "cms_host": info.cms_host,
        "cms_port": info.cms_port,
        "cms_ssl": info.cms_ssl,
    })
    .to_string()
}

/// The CMS serves a self-signed certificate, so skip rustls certificate
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
            cms_host: "cms.example.com".to_string(),
            cms_port: 8443,
            cms_ssl: true,
        }
    }

    #[test]
    fn url_contains_all_query_params() {
        let token = ConnectionToken {
            token: "deadbeef".to_string(),
            ts: 1234567890,
            nonce: "cafe".to_string(),
        };
        let url = build_cms_url("cms.example.com", 8443, true, "ak-1", &token, "dev-1");
        assert_eq!(
            url,
            "wss://cms.example.com:8443/cms/service?appkey=ak-1&token=deadbeef&ts=1234567890&nonce=cafe&device_id=dev-1"
        );
    }

    #[test]
    fn url_uses_ws_scheme_when_ssl_off() {
        let token = ConnectionToken {
            token: "deadbeef".to_string(),
            ts: 1234567890,
            nonce: "cafe".to_string(),
        };
        let url = build_cms_url("cms.example.com", 8080, false, "ak-1", &token, "dev-1");
        assert!(url.starts_with("ws://cms.example.com:8080/cms/service?"));
        assert!(!url.contains("wss://"));
    }

    #[test]
    fn log_endpoint_carries_no_credentials() {
        let endpoint = cms_endpoint("cms.example.com", 8443, true);
        assert_eq!(endpoint, "wss://cms.example.com:8443/cms/service");
        assert!(!endpoint.contains("appkey"));
        assert!(!endpoint.contains("token"));
        assert_eq!(cms_endpoint("cms.example.com", 8080, false), "ws://cms.example.com:8080/cms/service");
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
        assert_eq!(value["cms_host"], "cms.example.com");
        assert_eq!(value["cms_port"], 8443);
        assert_eq!(value["cms_ssl"], true);
    }

    #[test]
    fn hello_message_carries_device_appkey_version() {
        let message = hello_message("dev-1", "ak-1");
        assert_eq!(message.msg_type, CmsServiceMessageType::KCmsServiceHello);
        assert_eq!(message.device_id, "dev-1");
        let hello = message.hello.unwrap();
        assert_eq!(hello.device_id, "dev-1");
        assert_eq!(hello.appkey, "ak-1");
        assert_eq!(hello.version, env!("CARGO_PKG_VERSION"));
        assert!(message.heartbeat.is_none());
    }

    #[test]
    fn heartbeat_message_carries_index_and_liveness() {
        let message = heartbeat_message(7, "dev-1", true, "{\"a\":1}", "[{\"instance_id\":\"i1\"}]");
        assert_eq!(message.msg_type, CmsServiceMessageType::KCmsServiceHeartBeat);
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
        let decoded = CmsServiceMessage::decode(bytes.as_slice()).unwrap();
        assert_eq!(decoded.msg_type, CmsServiceMessageType::KCmsServiceHello);
        assert_eq!(decoded.hello.unwrap().appkey, "ak-1");
    }

    #[test]
    fn parse_start_and_stop_commands_round_trip() {
        let req = StartAppRequest {
            request_id: "req-1".into(),
            instance_id: "inst-1".into(),
            app_id: "app-1".into(),
            install_root: r"D:\apps\Car".into(),
            game_exe_rel: r"Binaries\game.exe".into(),
            game_arguments: "-dx11".into(),
            listen_port: 32000,
            encoder_fps: 60,
            encoder_bitrate: 20,
            encoder_format: "h264".into(),
            webrtc_enabled: true,
            websocket_enabled: true,
        };
        let bytes = encode_start_app_command("dev-1", &req);
        match parse_cms_inbound(&bytes).unwrap().unwrap() {
            CmsInboundCommand::StartApp(got) => {
                assert_eq!(got.instance_id, "inst-1");
                assert_eq!(got.install_root, r"D:\apps\Car");
                assert_eq!(got.listen_port, 32000);
            }
            other => panic!("unexpected {other:?}"),
        }

        let stop = encode_stop_app_command("dev-1", "req-2", "inst-1");
        match parse_cms_inbound(&stop).unwrap().unwrap() {
            CmsInboundCommand::StopApp {
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
            CmsServiceMessageType::KCmsServiceStartAppInstanceResult
        );
        let body = ok.start_app_instance_result.unwrap();
        assert!(body.ok);
        assert_eq!(body.listen_port, 32001);
        assert_eq!(body.pid, 99);

        let fail = start_app_result_message("dev-1", "r", "i", false, "boom", 0, 0);
        assert!(!fail.start_app_instance_result.unwrap().ok);
    }
}
