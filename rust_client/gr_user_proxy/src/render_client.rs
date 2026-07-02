use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use futures_util::{SinkExt, StreamExt};
use tokio::sync::Mutex;
use tokio::time::{sleep, Duration};
use tokio_tungstenite::tungstenite::Message as WsMessage;
use tracing::{error, info, warn};

use crate::config::{UserProxyConfig, RECONNECT_SECS};
use crate::proto::{
    self, build_clipboard_text_event, build_heartbeat_message, build_raw_render_message,
    build_tc_clipboard_files_resp, build_tc_clipboard_info_resp, clipboard_files_from_tc,
    clipboard_text_from_rp, parse_rp_message, parse_tc_message, tcrp::RpMessageType,
    tc::MessageType, HEARTBEAT_INTERVAL_SECS,
};

type WsSink = futures_util::stream::SplitSink<
    tokio_tungstenite::WebSocketStream<
        tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>,
    >,
    WsMessage,
>;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionState {
    Disconnected,
    Connected,
}

pub struct RenderClient {
    config: UserProxyConfig,
    sender: Arc<Mutex<Option<WsSink>>>,
    connected: Arc<AtomicBool>,
}

impl RenderClient {
    pub fn new(config: UserProxyConfig) -> Arc<Self> {
        Arc::new(Self {
            config,
            sender: Arc::new(Mutex::new(None)),
            connected: Arc::new(AtomicBool::new(false)),
        })
    }

    pub fn is_connected(&self) -> bool {
        self.connected.load(Ordering::Relaxed)
    }

    pub fn connection_state(&self) -> ConnectionState {
        if self.is_connected() {
            ConnectionState::Connected
        } else {
            ConnectionState::Disconnected
        }
    }

    pub async fn send_bytes(&self, bytes: Vec<u8>) -> anyhow::Result<()> {
        let byte_len = bytes.len();
        let mut guard = self.sender.lock().await;
        let sender = guard
            .as_mut()
            .ok_or_else(|| anyhow::anyhow!("websocket not connected"))?;
        sender
            .send(WsMessage::Binary(bytes.into()))
            .await
            .map_err(|err| anyhow::anyhow!("ws send failed: {err}"))?;
        info!("ws send ok, byte_len={byte_len}");
        Ok(())
    }

    pub async fn send_clipboard_text(&self, text: &str) -> anyhow::Result<()> {
        self.send_bytes(build_clipboard_text_event(text)).await
    }

    pub async fn send_hello(&self) -> anyhow::Result<()> {
        self.send_bytes(proto::build_hello_message()).await
    }

    pub async fn wait_until_connected(&self, timeout: Duration) -> bool {
        let start = std::time::Instant::now();
        while !self.is_connected() {
            if start.elapsed() >= timeout {
                return false;
            }
            tokio::time::sleep(Duration::from_millis(50)).await;
        }
        true
    }

    pub fn spawn_reconnect_loop(
        self: Arc<Self>,
        mut on_inbound: impl FnMut(Vec<u8>) + Send + 'static,
    ) {
        let cfg = self.config.clone();
        let sender_slot = self.sender.clone();
        let connected = self.connected.clone();
        tokio::spawn(async move {
            loop {
                {
                    let mut guard = sender_slot.lock().await;
                    *guard = None;
                }
                connected.store(false, Ordering::Relaxed);

                let url = cfg.render_ws_url();
                info!("connect attempt url={url}");
                match tokio_tungstenite::connect_async(&url).await {
                    Ok((stream, _resp)) => {
                        info!("connected to render");
                        let (writer, mut reader) = stream.split();
                        {
                            let mut guard = sender_slot.lock().await;
                            *guard = Some(writer);
                        }
                        if let Err(err) = self.send_hello().await {
                            error!("hello send failed: {err:#}");
                        } else {
                            connected.store(true, Ordering::Relaxed);
                            self.spawn_heartbeat_loop();
                        }

                        while let Some(msg) = reader.next().await {
                            match msg {
                                Ok(WsMessage::Binary(data)) => on_inbound(data.to_vec()),
                                Ok(WsMessage::Close(frame)) => {
                                    warn!(
                                        "connection closed: {:?}, retry in {}s",
                                        frame, RECONNECT_SECS
                                    );
                                    break;
                                }
                                Ok(WsMessage::Ping(_)) | Ok(WsMessage::Pong(_)) => {}
                                Ok(other) => info!("ignored ws frame: {:?}", other),
                                Err(err) => {
                                    error!(
                                        "connection read error: {err}, retry in {RECONNECT_SECS}s"
                                    );
                                    break;
                                }
                            }
                        }
                    }
                    Err(err) => {
                        error!("connect failed: {err}, retry in {RECONNECT_SECS}s");
                    }
                }

                connected.store(false, Ordering::Relaxed);
                {
                    let mut guard = sender_slot.lock().await;
                    *guard = None;
                }
                sleep(Duration::from_secs(cfg.reconnect_secs)).await;
            }
        });
    }

    fn spawn_heartbeat_loop(self: &Arc<Self>) {
        let client = Arc::clone(self);
        tokio::spawn(async move {
            loop {
                sleep(Duration::from_secs(HEARTBEAT_INTERVAL_SECS)).await;
                if !client.is_connected() {
                    break;
                }
                if let Err(err) = client.send_bytes(build_heartbeat_message()).await {
                    warn!("heartbeat send failed: {err:#}");
                    break;
                }
                info!("heartbeat sent");
            }
        });
    }
}

pub fn handle_inbound_rp(
    bytes: &[u8],
    clipboard: &crate::clipboard::ClipboardService,
    client: Arc<RenderClient>,
) {
    let msg = match parse_rp_message(bytes) {
        Ok(v) => v,
        Err(err) => {
            error!(
                "parse RpMessage failed: {err}, len={}, preview={:02x?}",
                bytes.len(),
                &bytes[..bytes.len().min(8)]
            );
            return;
        }
    };

    match RpMessageType::try_from(msg.r#type) {
        Ok(RpMessageType::KRpHelloResp) => info!("received hello resp"),
        Ok(RpMessageType::KRpHeartBeatResp) => info!("received heartbeat resp"),
        Ok(RpMessageType::KRpRawRenderMessage) => {
            if let Some(sub) = msg.raw_render_msg {
                if sub.data_channel {
                    warn!(
                        "data_channel raw render received, stream_id={}, byte_len={}; virtual file stream pending",
                        sub.stream_id,
                        sub.msg.len()
                    );
                    return;
                }
                match parse_tc_message(&sub.msg) {
                    Ok(tc_msg) => handle_inbound_tc(&tc_msg, clipboard, client),
                    Err(err) => error!("parse tc::Message failed: {err}, len={}", sub.msg.len()),
                }
            }
        }
        Ok(other) => info!("ignored inbound rp type: {:?}", other),
        Err(_) => error!("unknown RpMessage type: {}", msg.r#type),
    }
}

fn handle_inbound_tc(
    msg: &proto::tc::Message,
    clipboard: &crate::clipboard::ClipboardService,
    client: Arc<RenderClient>,
) {
    match MessageType::try_from(msg.r#type) {
        Ok(MessageType::KClipboardInfo) => {
            let Some(info) = &msg.clipboard_info else {
                return;
            };
            if info.r#type == proto::tc::ClipboardType::KClipboardText as i32 {
                let text = String::from_utf8_lossy(&info.msg).into_owned();
                if text.trim().is_empty() {
                    info!("no syncable text");
                    return;
                }
                if let Err(err) = clipboard.apply_remote_text(&text) {
                    error!("apply remote clipboard failed: {err:#}");
                    return;
                }
                let resp = build_raw_render_message(&build_tc_clipboard_info_resp(&text), false);
                tokio::spawn(async move {
                    if let Err(err) = client.send_bytes(resp).await {
                        error!("send clipboard resp failed: {err:#}");
                    }
                });
                return;
            }

            if info.r#type == proto::tc::ClipboardType::KClipboardFiles as i32 {
                let Some(files) = clipboard_files_from_tc(msg) else {
                    return;
                };
                if let Err(err) = clipboard.apply_remote_files(&files) {
                    error!("apply remote clipboard files failed: {err:#}");
                    return;
                }
                let resp =
                    build_raw_render_message(&build_tc_clipboard_files_resp(&files), false);
                tokio::spawn(async move {
                    if let Err(err) = client.send_bytes(resp).await {
                        error!("send clipboard files resp failed: {err:#}");
                    }
                });
            }
        }
        Ok(MessageType::KClipboardRespBuffer) => {
            warn!(
                "clipboard resp buffer received; virtual file stream handling pending, len={}",
                msg.cp_resp_buffer.as_ref().map(|v| v.buffer.len()).unwrap_or(0)
            );
        }
        Ok(other) => info!("ignored inbound tc type: {:?}", other),
        Err(_) => error!("unknown tc::Message type: {}", msg.r#type),
    }
}

pub fn extract_clipboard_text(bytes: &[u8]) -> Option<String> {
    parse_rp_message(bytes)
        .ok()
        .and_then(|msg| clipboard_text_from_rp(&msg))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reconnect_interval_is_two_seconds() {
        assert_eq!(RECONNECT_SECS, 2);
    }

    #[test]
    fn build_hello_message_type() {
        let bytes = proto::build_hello_message();
        let parsed = parse_rp_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpHello as i32);
    }

    #[test]
    fn parse_hello_resp() {
        let bytes = proto::build_hello_resp_message();
        let parsed = parse_rp_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpHelloResp as i32);
    }

    #[test]
    fn parse_clipboard_event_text() {
        let bytes = build_clipboard_text_event("local");
        assert_eq!(extract_clipboard_text(&bytes).as_deref(), Some("local"));
    }

    #[test]
    fn build_clipboard_event_bytes() {
        let bytes = build_clipboard_text_event("outbound");
        let parsed = parse_rp_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpClipboardEvent as i32);
    }

    #[test]
    fn build_raw_render_from_tc_message() {
        let inner = proto::build_tc_clipboard_info("x");
        let outer = build_raw_render_message(&inner, false);
        let rp = parse_rp_message(&outer).expect("rp");
        assert_eq!(rp.r#type, RpMessageType::KRpRawRenderMessage as i32);
    }

    #[test]
    fn connection_state_machine() {
        let client = RenderClient::new(UserProxyConfig::default());
        assert_eq!(client.connection_state(), ConnectionState::Disconnected);
        assert!(!client.is_connected());
    }

    #[test]
    fn heartbeat_message_type() {
        let bytes = build_heartbeat_message();
        let parsed = parse_rp_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpHeartBeat as i32);
    }
}
