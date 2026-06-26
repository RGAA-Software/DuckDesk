use crate::{gSysInfoMgr, gSysPanelClient};
use futures_util::stream::SplitSink;
use futures_util::{SinkExt, StreamExt};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::TcpStream;
use tokio::sync::Mutex;
use tokio::time::{sleep, timeout};
use tokio_tungstenite::tungstenite::Message as TungsteniteMessage;
use tokio_tungstenite::{connect_async, MaybeTlsStream, WebSocketStream};

type WsSink = SplitSink<WebSocketStream<MaybeTlsStream<TcpStream>>, TungsteniteMessage>;

// [this] ---> Panel ws server
pub struct SysPanelClient {
    sender: Arc<Mutex<Option<WsSink>>>,
    pub duration: i32,
}

impl Default for SysPanelClient {
    fn default() -> Self {
        Self::new()
    }
}

impl SysPanelClient {
    pub fn new() -> SysPanelClient {
        SysPanelClient {
            sender: Arc::new(Default::default()),
            duration: 1,
        }
    }

    pub async fn connect(&self, address: String) {
        tracing::info!("Connecting to {}", address);
        let self_sender = self.sender.clone();
        tokio::spawn(async move {
            loop {
                // 重连前清空旧 sender，释放 SplitSink 及其内部 TLS/TCP 缓冲
                *self_sender.lock().await = None;

                let ws_stream =
                    match timeout(Duration::from_secs(5), connect_async(address.clone())).await {
                        Ok(Ok((mut stream, _response))) => {
                            tracing::info!("connect success....");
                            let message = "Hello, WebSocket!";
                            if let Err(e) = timeout(
                                Duration::from_secs(5),
                                stream.send(TungsteniteMessage::Text(message.into())),
                            )
                            .await
                            {
                                tracing::error!("handshake send timeout or failed: {:?}", e);
                                None
                            } else {
                                Some(stream)
                            }
                        }
                        Ok(Err(e)) => {
                            tracing::error!("Failed to connect to {}: {}", address, e);
                            None
                        }
                        Err(_) => {
                            tracing::error!("Connect to {} timed out", address);
                            None
                        }
                    };

                if let Some(stream) = ws_stream {
                    let (sender, mut receiver) = stream.split();
                    *self_sender.lock().await = Some(sender);

                    let sender = self_sender.clone();
                    tokio::spawn(async move {
                        loop {
                            if let Some(sender) = &mut *sender.lock().await {
                                let start = Instant::now();
                                let info =
                                    gSysInfoMgr.lock().await.load_system_info_as_encrypt_json();
                                let elapsed = start.elapsed();
                                tracing::info!("used: {}ms", elapsed.as_millis());

                                let payload_len = info.len();
                                let send_start = Instant::now();
                                match timeout(
                                    Duration::from_secs(10),
                                    sender.send(TungsteniteMessage::Text(info.into())),
                                )
                                .await
                                {
                                    Ok(Ok(())) => {
                                        tracing::info!(
                                            "send info success, size={} bytes, elapsed={}ms",
                                            payload_len,
                                            send_start.elapsed().as_millis()
                                        );
                                    }
                                    Ok(Err(e)) => {
                                        tracing::error!("send info failed, break: {}", e);
                                        break;
                                    }
                                    Err(_) => {
                                        tracing::error!("send info timeout ({}s), break", 10);
                                        break;
                                    }
                                }
                            } else {
                                tracing::error!("No sender, Break the loop.");
                                break;
                            }
                            let duration = gSysPanelClient.lock().await.duration as u64;
                            tokio::time::sleep(Duration::from_secs(duration)).await;
                        }
                    });

                    // receive message
                    while let Some(msg) = receiver.next().await {
                        match msg {
                            Ok(TungsteniteMessage::Binary(_data)) => {}
                            Ok(TungsteniteMessage::Text(text)) => {
                                tracing::info!("Received message: {}", text);
                            }
                            Ok(TungsteniteMessage::Close(_)) => {
                                tracing::error!("Connection closed by server");
                                break;
                            }
                            Err(e) => {
                                tracing::error!("Error receiving message: {}", e);
                                break;
                            }
                            _ => {
                                tracing::error!("Unexpected message received");
                            }
                        }
                    }

                    // 接收循环退出，清空 sender，避免旧 SplitSink 残留
                    *self_sender.lock().await = None;
                    tracing::info!("receiver loop ended, sender cleared");
                }

                tracing::info!(
                    "will reconnect to {}, sender strong_count={}",
                    address,
                    Arc::strong_count(&self_sender)
                );
                sleep(Duration::from_secs(2)).await;
            }
        });
    }
}
