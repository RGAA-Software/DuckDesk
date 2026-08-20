use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use futures_util::{SinkExt, StreamExt};
use tokio::sync::Mutex;
use tokio::time::{sleep, Duration};
use tokio_tungstenite::tungstenite::Message as WsMessage;
use tracing::{error, info, warn};

use crate::clipboard::virtual_file::RespBufferData;
use crate::config::{UserProxyConfig, RECONNECT_SECS};
use crate::proto::{
    self, build_clipboard_text_event, build_heartbeat_message, build_px_clipboard_files_resp,
    build_px_clipboard_info_resp, build_px_resp_buffer, build_raw_render_message,
    build_raw_render_message_routed, clipboard_files_from_px, clipboard_resp_buffer_from_px,
    clipboard_text_from_rp, parse_px_message, parse_rp_message, px::MessageType,
    pxrp::RpMessageType, stream_route_from_px, stream_route_from_rp_raw, HEARTBEAT_INTERVAL_SECS,
};

type WsSink = futures_util::stream::SplitSink<
    tokio_tungstenite::WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>,
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
        self.send_bytes_inner(bytes).await
    }

    pub fn blocking_send_bytes(&self, bytes: Vec<u8>) -> anyhow::Result<()> {
        let runtime = tokio::runtime::Handle::try_current().ok();
        if let Some(handle) = runtime {
            handle.block_on(self.send_bytes_inner(bytes))
        } else {
            tokio::runtime::Builder::new_current_thread()
                .enable_all()
                .build()?
                .block_on(self.send_bytes_inner(bytes))
        }
    }

    async fn send_bytes_inner(&self, bytes: Vec<u8>) -> anyhow::Result<()> {
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
                    handle_inbound_data_channel(&sub, clipboard, client);
                    return;
                }
                match parse_px_message(&sub.msg) {
                    Ok(px_msg) => handle_inbound_px(&px_msg, clipboard, client),
                    Err(err) => error!("parse px::Message failed: {err}, len={}", sub.msg.len()),
                }
            }
        }
        Ok(other) => info!("ignored inbound rp type: {:?}", other),
        Err(_) => error!("unknown RpMessage type: {}", msg.r#type),
    }
}

fn handle_inbound_data_channel(
    sub: &proto::pxrp::RpRawRenderMessage,
    clipboard: &crate::clipboard::ClipboardService,
    client: Arc<RenderClient>,
) {
    let px_msg = match parse_px_message(&sub.msg) {
        Ok(v) => v,
        Err(err) => {
            error!(
                "parse data_channel px::Message failed: {err}, stream_id={}, len={}",
                sub.stream_id,
                sub.msg.len()
            );
            return;
        }
    };
    let route = stream_route_from_rp_raw(sub);
    if MessageType::try_from(px_msg.r#type) == Ok(MessageType::KClipboardReqBuffer) {
        dispatch_req_buffer(&px_msg, client, &route);
        return;
    }
    dispatch_resp_buffer(&px_msg, clipboard, Some(route));
}

fn dispatch_req_buffer(
    msg: &proto::px::Message,
    client: Arc<RenderClient>,
    route: &proto::StreamRoute,
) {
    let Some(req) = msg.cp_req_buffer.as_ref() else {
        warn!("clipboard req buffer missing payload");
        return;
    };
    let start = req.req_start.max(0) as u64;
    let size = req.req_size.max(0) as usize;
    let mut buffer = Vec::with_capacity(size);
    let read_size = match std::fs::File::open(&req.full_name) {
        Ok(mut file) => {
            use std::io::{Read, Seek, SeekFrom};
            match file
                .seek(SeekFrom::Start(start))
                .and_then(|_| file.take(size as u64).read_to_end(&mut buffer))
            {
                Ok(n) => n as i64,
                Err(err) => {
                    warn!(
                        "clipboard req buffer read failed: {} ({err:#})",
                        req.full_name
                    );
                    0
                }
            }
        }
        Err(err) => {
            warn!(
                "clipboard req buffer open failed: {} ({err:#})",
                req.full_name
            );
            0
        }
    };

    let resp = RespBufferData {
        full_name: req.full_name.clone(),
        req_index: req.req_index,
        req_start: req.req_start,
        req_size: req.req_size,
        read_size,
        buffer,
    };
    let reply =
        build_raw_render_message_routed(&build_px_resp_buffer(&resp, route), true, Some(route));
    info!(
        "clipboard req buffer handled, full={}, start={}, size={}, read={}",
        req.full_name, req.req_start, req.req_size, read_size
    );
    tokio::spawn(async move {
        if let Err(err) = client.send_bytes(reply).await {
            error!("send clipboard resp buffer failed: {err:#}");
        }
    });
}

fn dispatch_resp_buffer(
    msg: &proto::px::Message,
    clipboard: &crate::clipboard::ClipboardService,
    route: Option<proto::StreamRoute>,
) {
    if MessageType::try_from(msg.r#type) != Ok(MessageType::KClipboardRespBuffer) {
        info!(
            "ignored data_channel px type: {:?}, stream_id={}",
            msg.r#type,
            route.as_ref().map(|r| r.stream_id.as_str()).unwrap_or("")
        );
        return;
    }
    let Some(resp) = clipboard_resp_buffer_from_px(msg) else {
        warn!("clipboard resp buffer missing payload");
        return;
    };
    let Some(coordinator) = clipboard.virtual_file_coordinator() else {
        warn!("clipboard resp buffer without virtual file coordinator");
        return;
    };
    if coordinator.on_resp_buffer(resp) {
        info!(
            "virtual file resp buffer applied, stream_id={}",
            route
                .as_ref()
                .map(|r| r.stream_id.as_str())
                .unwrap_or(&msg.stream_id)
        );
    }
}

fn handle_inbound_px(
    msg: &proto::px::Message,
    clipboard: &crate::clipboard::ClipboardService,
    client: Arc<RenderClient>,
) {
    match MessageType::try_from(msg.r#type) {
        Ok(MessageType::KClipboardInfo) => {
            let Some(info) = &msg.clipboard_info else {
                return;
            };
            if info.r#type == proto::px::ClipboardType::KClipboardText as i32 {
                let text = String::from_utf8_lossy(&info.msg).into_owned();
                if text.trim().is_empty() {
                    info!("no syncable text");
                    return;
                }
                if let Err(err) = clipboard.apply_remote_text(&text) {
                    error!("apply remote clipboard failed: {err:#}");
                    return;
                }
                let resp = build_raw_render_message(&build_px_clipboard_info_resp(&text), false);
                tokio::spawn(async move {
                    if let Err(err) = client.send_bytes(resp).await {
                        error!("send clipboard resp failed: {err:#}");
                    }
                });
                return;
            }

            if info.r#type == proto::px::ClipboardType::KClipboardFiles as i32 {
                let Some(files) = clipboard_files_from_px(msg) else {
                    return;
                };
                let route = stream_route_from_px(msg);
                if let Err(err) = clipboard.apply_remote_files(&files, &route) {
                    error!("apply remote clipboard files failed: {err:#}");
                    return;
                }
                let resp = build_raw_render_message(&build_px_clipboard_files_resp(&files), false);
                tokio::spawn(async move {
                    if let Err(err) = client.send_bytes(resp).await {
                        error!("send clipboard files resp failed: {err:#}");
                    }
                });
            }
        }
        Ok(MessageType::KClipboardRespBuffer) => {
            dispatch_resp_buffer(msg, clipboard, Some(stream_route_from_px(msg)));
        }
        Ok(other) => info!("ignored inbound px type: {:?}", other),
        Err(_) => error!("unknown px::Message type: {}", msg.r#type),
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
    fn build_raw_render_from_px_message() {
        let inner = proto::build_px_clipboard_info("x");
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
