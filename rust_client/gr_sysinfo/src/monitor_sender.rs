use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

use futures_util::SinkExt;
use tokio::runtime::Runtime;
use tokio::time::sleep;
use tokio_tungstenite::connect_async;
use tokio_tungstenite::tungstenite::Message;

#[derive(Clone, Default)]
struct SenderDesired {
    host: String,
    port: u16,
    enabled: bool,
    revision: u64,
}

#[derive(Clone, Default)]
pub struct SenderStatus {
    pub connected: bool,
    pub target: String,
    pub state: String,
    pub last_error: String,
}

#[derive(Clone)]
pub struct MonitorSenderHandle {
    desired: Arc<Mutex<SenderDesired>>,
    latest_json: Arc<Mutex<String>>,
    status: Arc<Mutex<SenderStatus>>,
}

impl MonitorSenderHandle {
    pub fn new() -> Self {
        let desired = Arc::new(Mutex::new(SenderDesired {
            host: "127.0.0.1".to_string(),
            port: 20379,
            enabled: true,
            revision: 0,
        }));
        let latest_json = Arc::new(Mutex::new(String::new()));
        let status = Arc::new(Mutex::new(SenderStatus {
            connected: false,
            target: "ws://127.0.0.1:20379/sys/info".to_string(),
            state: "Connecting".to_string(),
            last_error: String::new(),
        }));

        spawn_sender_thread(desired.clone(), latest_json.clone(), status.clone());

        Self {
            desired,
            latest_json,
            status,
        }
    }

    pub fn set_latest_json(&self, json: String) {
        if let Ok(mut latest) = self.latest_json.lock() {
            *latest = json;
        }
    }

    pub fn connect(&self, host: String, port: u16) {
        if let Ok(mut desired) = self.desired.lock() {
            desired.host = host;
            desired.port = port;
            desired.enabled = true;
            desired.revision = desired.revision.saturating_add(1);
        }
    }

    pub fn disconnect(&self) {
        if let Ok(mut desired) = self.desired.lock() {
            desired.enabled = false;
            desired.revision = desired.revision.saturating_add(1);
        }
    }

    pub fn status(&self) -> SenderStatus {
        self.status.lock().map(|status| status.clone()).unwrap_or_default()
    }
}

fn spawn_sender_thread(
    desired: Arc<Mutex<SenderDesired>>,
    latest_json: Arc<Mutex<String>>,
    status: Arc<Mutex<SenderStatus>>,
) {
    thread::spawn(move || {
        let runtime = Runtime::new().expect("failed to create monitor sender runtime");
        runtime.block_on(async move {
            let mut active_revision = 0_u64;
            let mut target = String::new();
            let mut ws = None;

            loop {
                let desired_snapshot = desired.lock().map(|value| value.clone()).unwrap_or_default();
                let current_target =
                    format!("ws://{}:{}/sys/info", desired_snapshot.host, desired_snapshot.port);

                if !desired_snapshot.enabled {
                    ws = None;
                    active_revision = desired_snapshot.revision;
                    update_status(&status, false, current_target, "Idle", "");
                    sleep(Duration::from_millis(300)).await;
                    continue;
                }

                if ws.is_none()
                    || desired_snapshot.revision != active_revision
                    || target != current_target
                {
                    ws = None;
                    target = current_target.clone();
                    active_revision = desired_snapshot.revision;
                    update_status(&status, false, current_target.clone(), "Connecting", "");
                    match connect_async(&current_target).await {
                        Ok((stream, _)) => {
                            ws = Some(stream);
                            update_status(&status, true, current_target, "Connected", "");
                        }
                        Err(err) => {
                            update_status(
                                &status,
                                false,
                                current_target,
                                "ConnectFailed",
                                &err.to_string(),
                            );
                            sleep(Duration::from_secs(1)).await;
                            continue;
                        }
                    }
                }

                let Some(stream) = &mut ws else {
                    sleep(Duration::from_millis(300)).await;
                    continue;
                };

                let payload = latest_json
                    .lock()
                    .map(|value| value.clone())
                    .unwrap_or_default();
                if payload.is_empty() {
                    sleep(Duration::from_millis(300)).await;
                    continue;
                }

                if let Err(err) = stream.send(Message::Text(payload.into())).await {
                    update_status(
                        &status,
                        false,
                        target.clone(),
                        "SendFailed",
                        &err.to_string(),
                    );
                    ws = None;
                    sleep(Duration::from_secs(1)).await;
                    continue;
                }

                update_status(&status, true, target.clone(), "Connected", "");
                sleep(Duration::from_secs(1)).await;
            }
        });
    });
}

fn update_status(
    status: &Arc<Mutex<SenderStatus>>,
    connected: bool,
    target: String,
    state: &str,
    last_error: &str,
) {
    if let Ok(mut current) = status.lock() {
        current.connected = connected;
        current.target = target;
        current.state = state.to_string();
        current.last_error = last_error.to_string();
    }
}
