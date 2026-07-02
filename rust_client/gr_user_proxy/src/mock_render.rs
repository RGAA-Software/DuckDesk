use std::net::SocketAddr;
use std::sync::{Arc, Mutex};

use futures_util::{SinkExt, StreamExt};
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::mpsc;
use tokio::task::JoinHandle;
use tokio_tungstenite::accept_async;
use tokio_tungstenite::tungstenite::Message;

use crate::proto::{
    build_hello_resp_message, build_raw_render_message, build_tc_clipboard_info,
    clipboard_text_from_rp, parse_rp_message, tcrp::RpMessageType,
};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum MockRenderEvent {
    Connected,
    Disconnected,
    Hello,
    ClipboardText(String),
    ClipboardResp,
}

#[derive(Clone)]
pub struct MockRenderHandle {
    events: Arc<Mutex<Vec<MockRenderEvent>>>,
    outbound_tx: mpsc::UnboundedSender<Vec<u8>>,
    shutdown_tx: mpsc::UnboundedSender<()>,
}

impl MockRenderHandle {
    pub fn events(&self) -> Vec<MockRenderEvent> {
        self.events.lock().expect("lock").clone()
    }

    pub fn drain_events(&self) -> Vec<MockRenderEvent> {
        let mut guard = self.events.lock().expect("lock");
        std::mem::take(&mut *guard)
    }

    pub fn send_raw_render_clipboard(&self, text: &str) -> anyhow::Result<()> {
        let inner = build_tc_clipboard_info(text);
        let bytes = build_raw_render_message(&inner, false);
        self.outbound_tx
            .send(bytes)
            .map_err(|_| anyhow::anyhow!("mock render outbound channel closed"))
    }

    pub fn shutdown(&self) {
        let _ = self.shutdown_tx.send(());
    }
}

pub struct MockRenderServer {
    handle: MockRenderHandle,
    task: JoinHandle<()>,
    port: u16,
}

impl MockRenderServer {
    pub fn port(&self) -> u16 {
        self.port
    }

    pub fn handle(&self) -> MockRenderHandle {
        self.handle.clone()
    }
}

impl Drop for MockRenderServer {
    fn drop(&mut self) {
        self.handle.shutdown();
        self.task.abort();
    }
}

pub async fn start_on_port(port: u16) -> anyhow::Result<MockRenderServer> {
    let listener = TcpListener::bind(format!("127.0.0.1:{port}")).await?;
    spawn_server(listener, port).await
}

pub async fn start() -> anyhow::Result<MockRenderServer> {
    let listener = TcpListener::bind("127.0.0.1:0").await?;
    let port = listener.local_addr()?.port();
    spawn_server(listener, port).await
}

async fn spawn_server(listener: TcpListener, port: u16) -> anyhow::Result<MockRenderServer> {
    let events = Arc::new(Mutex::new(Vec::new()));
    let (outbound_tx, outbound_rx) = mpsc::unbounded_channel::<Vec<u8>>();
    let (shutdown_tx, mut shutdown_rx) = mpsc::unbounded_channel::<()>();
    let handle = MockRenderHandle {
        events: events.clone(),
        outbound_tx,
        shutdown_tx,
    };

    let task = tokio::spawn(async move {
        let outbound_rx = Arc::new(tokio::sync::Mutex::new(outbound_rx));
        loop {
            tokio::select! {
                _ = shutdown_rx.recv() => break,
                accept_result = listener.accept() => {
                    let Ok((stream, addr)) = accept_result else {
                        continue;
                    };
                    if !is_loopback(addr) {
                        continue;
                    }
                    let events = events.clone();
                    let outbound_rx = outbound_rx.clone();
                    tokio::spawn(async move {
                        if let Err(err) = serve_connection(stream, events.clone(), outbound_rx).await {
                            tracing::debug!("mock render connection ended: {err:#}");
                        }
                        events.lock().expect("lock").push(MockRenderEvent::Disconnected);
                    });
                }
            }
        }
    });

    Ok(MockRenderServer {
        handle,
        task,
        port,
    })
}

fn is_loopback(addr: SocketAddr) -> bool {
    addr.ip().is_loopback()
}

async fn serve_connection(
    stream: TcpStream,
    events: Arc<Mutex<Vec<MockRenderEvent>>>,
    outbound_rx: Arc<tokio::sync::Mutex<mpsc::UnboundedReceiver<Vec<u8>>>>,
) -> anyhow::Result<()> {
    let ws = accept_async(stream).await?;
    events
        .lock()
        .expect("lock")
        .push(MockRenderEvent::Connected);

    let (mut sink, mut stream) = ws.split();
    loop {
        tokio::select! {
            maybe_out = async {
                outbound_rx.lock().await.recv().await
            } => {
                let Some(bytes) = maybe_out else { break; };
                sink.send(Message::Binary(bytes.into())).await?;
            }
            maybe_in = stream.next() => {
                let Some(msg) = maybe_in else { break; };
                let msg = msg?;
                if let Message::Binary(bytes) = msg {
                    if let Some(resp) = handle_inbound(&bytes, &events)? {
                        sink.send(Message::Binary(resp.into())).await?;
                    }
                }
            }
        }
    }
    Ok(())
}

fn handle_inbound(
    bytes: &[u8],
    events: &Arc<Mutex<Vec<MockRenderEvent>>>,
) -> anyhow::Result<Option<Vec<u8>>> {
    let msg = parse_rp_message(bytes)?;
    match RpMessageType::try_from(msg.r#type) {
        Ok(RpMessageType::KRpHello) => {
            events.lock().expect("lock").push(MockRenderEvent::Hello);
            Ok(Some(build_hello_resp_message()))
        }
        Ok(RpMessageType::KRpClipboardEvent) => {
            if let Some(text) = clipboard_text_from_rp(&msg) {
                events
                    .lock()
                    .expect("lock")
                    .push(MockRenderEvent::ClipboardText(text));
            }
            Ok(None)
        }
        Ok(RpMessageType::KRpRawRenderMessage) => {
            events
                .lock()
                .expect("lock")
                .push(MockRenderEvent::ClipboardResp);
            Ok(None)
        }
        _ => Ok(None),
    }
}

pub async fn wait_for_event(
    handle: &MockRenderHandle,
    matcher: impl Fn(&MockRenderEvent) -> bool,
    timeout: std::time::Duration,
) -> bool {
    let start = std::time::Instant::now();
    while start.elapsed() < timeout {
        if handle.events().iter().any(&matcher) {
            return true;
        }
        tokio::time::sleep(std::time::Duration::from_millis(50)).await;
    }
    false
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn mock_render_accepts_hello() {
        let server = start().await.expect("start");
        let url = format!("ws://127.0.0.1:{}/", server.port());
        let (mut ws, _) = tokio_tungstenite::connect_async(url).await.expect("connect");
        ws.send(Message::Binary(
            crate::proto::build_hello_message().into(),
        ))
        .await
        .expect("send");
        assert!(
            wait_for_event(
                &server.handle(),
                |event| matches!(event, MockRenderEvent::Hello),
                std::time::Duration::from_secs(3),
            )
            .await
        );

        let mut got_hello_resp = false;
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(3);
        while std::time::Instant::now() < deadline {
            if let Some(Ok(Message::Binary(bytes))) = ws.next().await {
                let parsed = parse_rp_message(&bytes).expect("parse");
                if parsed.r#type == RpMessageType::KRpHelloResp as i32 {
                    got_hello_resp = true;
                    break;
                }
            }
        }
        assert!(got_hello_resp);
    }
}
