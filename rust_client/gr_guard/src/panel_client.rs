use std::sync::mpsc::{self, Sender};
use std::thread::{self, JoinHandle};

use futures_util::StreamExt;
use tokio::net::TcpStream;
use tokio::runtime::Builder;
use tokio::time::{sleep, Duration};
use tokio_tungstenite::connect_async;
use tokio_tungstenite::tungstenite::client::IntoClientRequest;
use tokio_tungstenite::MaybeTlsStream;
use tokio_tungstenite::WebSocketStream;
use tracing::{info, warn};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PanelClientConfig {
    pub url: String,
    pub reconnect_delay: Duration,
}

impl PanelClientConfig {
    pub fn new(url: String) -> Self {
        Self {
            url,
            reconnect_delay: Duration::from_secs(5),
        }
    }
}

pub struct PanelClient {
    stop_tx: Sender<()>,
    handle: Option<JoinHandle<()>>,
}

impl PanelClient {
    pub fn start(config: PanelClientConfig) -> Self {
        let (stop_tx, stop_rx) = mpsc::channel();
        let handle = thread::spawn(move || {
            let runtime = Builder::new_current_thread()
                .enable_all()
                .build()
                .expect("tokio runtime");
            runtime.block_on(async move {
                loop {
                    if stop_rx.try_recv().is_ok() {
                        info!("guard panel client stop requested");
                        break;
                    }

                    match connect_async(build_request(&config.url)).await {
                        Ok((mut ws, _)) => {
                            info!("guard panel client connected");
                            if let Err(err) = drive_connection(&mut ws, &stop_rx).await {
                                warn!("guard panel client connection loop failed: {err}");
                            }
                        }
                        Err(err) => {
                            warn!("guard panel client connect failed: {err}");
                        }
                    }

                    sleep(config.reconnect_delay).await;
                }
            });
        });
        Self {
            stop_tx,
            handle: Some(handle),
        }
    }

    pub fn stop(&mut self) {
        let _ = self.stop_tx.send(());
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        }
    }
}

impl Drop for PanelClient {
    fn drop(&mut self) {
        self.stop();
    }
}

fn build_request(url: &str) -> tokio_tungstenite::tungstenite::http::Request<()> {
    let mut request = url
        .into_client_request()
        .expect("guard panel websocket request");
    request.headers_mut().insert(
        "authorization",
        "websocket-client-authorization".parse().expect("auth header"),
    );
    request
}

async fn drive_connection(
    ws: &mut WebSocketStream<MaybeTlsStream<TcpStream>>,
    stop_rx: &mpsc::Receiver<()>,
) -> Result<(), String> {
    loop {
        tokio::select! {
            _ = sleep(Duration::from_millis(200)) => {
                if stop_rx.try_recv().is_ok() {
                    info!("guard panel client stop requested during active connection");
                    return Ok(());
                }
            }
            message = ws.next() => {
                match message {
                    Some(Ok(_)) => {}
                    Some(Err(err)) => return Err(format!("receive error: {err}")),
                    None => return Err("remote closed websocket".to_string()),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tokio_tungstenite::tungstenite::http::HeaderValue;

    #[test]
    fn config_uses_five_second_reconnect_delay() {
        let config = PanelClientConfig::new("ws://127.0.0.1:20369/panel/renderer?from=guard".to_string());
        assert_eq!(config.reconnect_delay, Duration::from_secs(5));
    }

    #[test]
    fn config_preserves_panel_url() {
        let url = "ws://127.0.0.1:20369/panel/renderer?from=guard".to_string();
        let config = PanelClientConfig::new(url.clone());
        assert_eq!(config.url, url);
    }

    #[test]
    fn panel_client_can_start_and_stop_without_server() {
        let mut client = PanelClient::start(PanelClientConfig {
            url: "ws://127.0.0.1:1/panel/renderer?from=guard".to_string(),
            reconnect_delay: Duration::from_millis(10),
        });
        std::thread::sleep(std::time::Duration::from_millis(20));
        client.stop();
    }

    #[test]
    fn request_includes_guard_authorization_header() {
        let request = build_request("ws://127.0.0.1:20369/panel/renderer?from=guard");
        assert_eq!(
            request.headers().get("authorization"),
            Some(&HeaderValue::from_static("websocket-client-authorization"))
        );
    }
}
