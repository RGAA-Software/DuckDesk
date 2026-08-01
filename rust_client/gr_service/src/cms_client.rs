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

use gr_auth_mgr::app_secret_util::calculate_app_secret;
use gr_auth_mgr::auth_token::{generate_connection_token, ConnectionToken};
use protocol::spvr_service::{
    SpvrServiceHeartBeat, SpvrServiceHello, SpvrServiceMessage, SpvrServiceMessageType,
};

use crate::service_host::ServiceRuntime;

type WsSink = SplitSink<WebSocketStream<MaybeTlsStream<TcpStream>>, TungsteniteMessage>;

const CONNECT_TIMEOUT_SECS: u64 = 5;
const RECONNECT_DELAY_SECS: u64 = 2;
const HEARTBEAT_INTERVAL_SECS: u64 = 5;
const AUTH_INFO_POLL_SECS: u64 = 1;

/// WSS client loop towards the CMS (gr_cms_server) `/spvr/service` endpoint.
///
/// The CMS address (spvr_host/spvr_port), appkey and device_id all come from
/// the authorization info the panel pushes to this service, so the loop first
/// waits until `state.last_auth_info` is present. A fresh connection token is
/// generated on every reconnect.
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
            "cms auth info ready, spvr={}:{} device_id={}",
            auth_info.spvr_host, auth_info.spvr_port, auth_info.device_id
        );

        // regenerate the connection token on every (re)connect
        let app_secret = calculate_app_secret(auth_info.appkey.clone());
        let token = generate_connection_token(&auth_info.appkey, &app_secret);
        let url = build_cms_url(
            &auth_info.spvr_host,
            auth_info.spvr_port,
            &auth_info.appkey,
            &token,
            &auth_info.device_id,
        );

        *sender.lock().await = None;
        let connect = tokio_tungstenite::connect_async_tls_with_config(
            url.clone(),
            None,
            false,
            Some(tls_connector()),
        );
        let stream = match timeout(Duration::from_secs(CONNECT_TIMEOUT_SECS), connect).await {
            Ok(Ok((stream, _response))) => {
                info!("connected to cms: {url}");
                stream
            }
            Ok(Err(err)) => {
                error!("connect to cms {url} failed: {err}");
                sleep(Duration::from_secs(RECONNECT_DELAY_SECS)).await;
                continue;
            }
            Err(_) => {
                error!("connect to cms {url} timed out");
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
                        let (render_alive, auth_json) = {
                            let guard = hb_runtime.lock().await;
                            (
                                guard.state.desktop_alive,
                                guard
                                    .state
                                    .last_auth_info
                                    .as_ref()
                                    .map(auth_info_to_json)
                                    .unwrap_or_default(),
                            )
                        };
                        let frame =
                            encode_message(&heartbeat_message(hb_index, &device_id, render_alive, &auth_json));
                        if !send_frame(&hb_sender, frame).await {
                            break;
                        }
                    }
                    _ = hb_stop_rx.recv() => break,
                }
            }
        });

        // receive loop: server pushes nothing critical yet, but closing the
        // stream must trigger a reconnect
        let mut should_stop = false;
        loop {
            tokio::select! {
                msg = receiver.next() => {
                    match msg {
                        Some(Ok(TungsteniteMessage::Close(_))) => {
                            warn!("cms closed the connection");
                            break;
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

fn build_cms_url(
    host: &str,
    port: i32,
    appkey: &str,
    token: &ConnectionToken,
    device_id: &str,
) -> String {
    format!(
        "wss://{host}:{port}/spvr/service?appkey={appkey}&token={}&ts={}&nonce={}&device_id={device_id}",
        token.token, token.ts, token.nonce
    )
}

fn hello_message(device_id: &str, appkey: &str) -> SpvrServiceMessage {
    SpvrServiceMessage {
        msg_type: SpvrServiceMessageType::KSpvrServiceHello as i32,
        device_id: device_id.to_string(),
        hello: Some(SpvrServiceHello {
            device_id: device_id.to_string(),
            appkey: appkey.to_string(),
            version: env!("CARGO_PKG_VERSION").to_string(),
        }),
        heartbeat: None,
    }
}

fn heartbeat_message(
    hb_index: i64,
    device_id: &str,
    render_alive: bool,
    auth_info_json: &str,
) -> SpvrServiceMessage {
    SpvrServiceMessage {
        msg_type: SpvrServiceMessageType::KSpvrServiceHeartBeat as i32,
        device_id: device_id.to_string(),
        hello: None,
        heartbeat: Some(SpvrServiceHeartBeat {
            hb_index,
            device_id: device_id.to_string(),
            render_alive,
            auth_info_json: auth_info_json.to_string(),
        }),
    }
}

fn encode_message(message: &SpvrServiceMessage) -> Vec<u8> {
    message.encode_to_vec()
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
        "spvr_host": info.spvr_host,
        "spvr_port": info.spvr_port,
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
            spvr_host: "cms.example.com".to_string(),
            spvr_port: 8443,
        }
    }

    #[test]
    fn url_contains_all_query_params() {
        let token = ConnectionToken {
            token: "deadbeef".to_string(),
            ts: 1234567890,
            nonce: "cafe".to_string(),
        };
        let url = build_cms_url("cms.example.com", 8443, "ak-1", &token, "dev-1");
        assert_eq!(
            url,
            "wss://cms.example.com:8443/spvr/service?appkey=ak-1&token=deadbeef&ts=1234567890&nonce=cafe&device_id=dev-1"
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
        assert_eq!(value["spvr_host"], "cms.example.com");
        assert_eq!(value["spvr_port"], 8443);
    }

    #[test]
    fn hello_message_carries_device_appkey_version() {
        let message = hello_message("dev-1", "ak-1");
        assert_eq!(message.msg_type, SpvrServiceMessageType::KSpvrServiceHello);
        assert_eq!(message.device_id, "dev-1");
        let hello = message.hello.unwrap();
        assert_eq!(hello.device_id, "dev-1");
        assert_eq!(hello.appkey, "ak-1");
        assert_eq!(hello.version, env!("CARGO_PKG_VERSION"));
        assert!(message.heartbeat.is_none());
    }

    #[test]
    fn heartbeat_message_carries_index_and_liveness() {
        let message = heartbeat_message(7, "dev-1", true, "{\"a\":1}");
        assert_eq!(message.msg_type, SpvrServiceMessageType::KSpvrServiceHeartBeat);
        let heartbeat = message.heartbeat.unwrap();
        assert_eq!(heartbeat.hb_index, 7);
        assert_eq!(heartbeat.device_id, "dev-1");
        assert!(heartbeat.render_alive);
        assert_eq!(heartbeat.auth_info_json, "{\"a\":1}");
        assert!(message.hello.is_none());
    }

    #[test]
    fn encoded_hello_round_trips() {
        let bytes = encode_message(&hello_message("dev-1", "ak-1"));
        let decoded = SpvrServiceMessage::decode(bytes.as_slice()).unwrap();
        assert_eq!(decoded.msg_type, SpvrServiceMessageType::KSpvrServiceHello);
        assert_eq!(decoded.hello.unwrap().appkey, "ak-1");
    }
}
