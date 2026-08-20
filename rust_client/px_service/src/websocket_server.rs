use std::sync::Arc;

use futures_util::{SinkExt, StreamExt};
use service_core::command::dispatch_message;
use service_core::encode_service_message;
use tokio::net::{TcpListener, TcpStream};
use tokio::sync::Mutex;
use tokio_tungstenite::accept_hdr_async;
use tokio_tungstenite::tungstenite::handshake::server::{Request, Response};
use tokio_tungstenite::tungstenite::Message;

use crate::service_host::ServiceRuntime;

pub struct WebsocketService {
    runtime: Arc<Mutex<ServiceRuntime>>,
}

impl WebsocketService {
    pub fn new(runtime: Arc<Mutex<ServiceRuntime>>) -> Self {
        Self { runtime }
    }

    pub async fn run_console(&self) -> Result<(), String> {
        let config = self.runtime.lock().await.config.clone();
        let addr = format!("{}:{}", config.listen_host, config.listen_port);
        let listener = TcpListener::bind(&addr)
            .await
            .map_err(|err| err.to_string())?;
        loop {
            let (stream, peer) = listener.accept().await.map_err(|err| err.to_string())?;
            if !peer.ip().is_loopback() {
                continue;
            }
            let runtime = self.runtime.clone();
            let expected_path = config.ws_path.clone();
            tokio::spawn(async move {
                let _ = handle_connection(stream, runtime, expected_path).await;
            });
        }
    }
}

async fn handle_connection(
    stream: TcpStream,
    runtime: Arc<Mutex<ServiceRuntime>>,
    expected_path: String,
) -> Result<(), String> {
    let ipc_token = runtime.lock().await.ipc_token.clone();
    let ws_stream = accept_hdr_async(stream, move |req: &Request, resp: Response| {
        let render_authorized = req
            .headers()
            .get("authorization")
            .and_then(|value| value.to_str().ok())
            .is_some_and(|value| value == format!("Bearer {ipc_token}"));
        // The listener itself only accepts loopback peers. The interactive
        // Panel is the local controller and predates the per-process Render
        // credential, so keep its explicit `from=panel` control channel
        // compatible. Render processes still must present the ephemeral
        // bearer token injected by px_service at launch.
        let panel_controller = req.uri().query().is_some_and(|query| {
            query
                .split('&')
                .any(|item| item.eq_ignore_ascii_case("from=panel"))
        });
        if req.uri().path() == expected_path && (render_authorized || panel_controller) {
            Ok(resp)
        } else {
            Err(
                tokio_tungstenite::tungstenite::handshake::server::ErrorResponse::new(Some(
                    "forbidden".to_string(),
                )),
            )
        }
    })
    .await
    .map_err(|err| err.to_string())?;

    let (mut sink, mut stream) = ws_stream.split();
    // 除 request/response 外,service 还需主动给 render 推消息(如 CMS 停止
    // 实例时的 kSrvStopServer),sink 交给独立 writer task,发送方走 channel。
    let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel::<Vec<u8>>();
    let writer = tokio::spawn(async move {
        while let Some(bytes) = rx.recv().await {
            if sink.send(Message::Binary(bytes.into())).await.is_err() {
                break;
            }
        }
    });

    let mut registered_renders: Vec<String> = Vec::new();
    while let Some(message) = stream.next().await {
        let message = match message {
            Ok(message) => message,
            Err(_) => break,
        };
        if let Message::Binary(bytes) = message {
            // render 心跳 from = "render_{port}": 据此注册该 render 的下发通道。
            if let Ok(sm) = service_core::decode_service_message(&bytes) {
                if let Some(hb) = sm.heart_beat.as_ref() {
                    if hb.from.starts_with("render_") && !registered_renders.contains(&hb.from) {
                        runtime
                            .lock()
                            .await
                            .render_senders
                            .insert(hb.from.clone(), tx.clone());
                        registered_renders.push(hb.from.clone());
                    }
                }
            }
            let command = match dispatch_message(&bytes) {
                Ok(result) => result.command,
                Err(_) => continue,
            };
            let response = match command {
                service_core::command::Command::RedeemConnectionTicket {
                    request_id,
                    ticket,
                    client_nonce,
                    instance_id,
                } => {
                    let channel = runtime.lock().await.ticket_redeem_tx.clone();
                    if let Some(channel) = channel {
                        let (reply_tx, reply_rx) = tokio::sync::oneshot::channel();
                        let request = crate::service_host::TicketRedeemRequest {
                            request_id: request_id.clone(),
                            ticket,
                            client_nonce,
                            instance_id,
                            response: reply_tx,
                        };
                        match channel.send(request).await {
                            Ok(()) => match tokio::time::timeout(
                                std::time::Duration::from_secs(3),
                                reply_rx,
                            )
                            .await
                            {
                                Ok(Ok(result)) => Some(ticket_response(request_id, result)),
                                _ => Some(ticket_response(
                                    request_id,
                                    crate::service_host::TicketRedeemResult {
                                        code: "CMS_TIMEOUT".to_string(),
                                        ..Default::default()
                                    },
                                )),
                            },
                            Err(_) => Some(ticket_response(
                                request_id,
                                crate::service_host::TicketRedeemResult {
                                    code: "CMS_UNAVAILABLE".to_string(),
                                    ..Default::default()
                                },
                            )),
                        }
                    } else {
                        Some(ticket_response(
                            request_id,
                            crate::service_host::TicketRedeemResult {
                                code: "CMS_UNAVAILABLE".to_string(),
                                ..Default::default()
                            },
                        ))
                    }
                }
                command => {
                    let mut guard = runtime.lock().await;
                    match guard.handle_command(command) {
                        Ok(response) => response,
                        Err(_) => continue,
                    }
                }
            };
            if let Some(response) = response {
                let _ = tx.send(encode_service_message(&response));
            }
        }
    }
    // 连接断开:注销本连接注册的 render 通道,避免向死连接发送。
    if !registered_renders.is_empty() {
        let mut guard = runtime.lock().await;
        for name in &registered_renders {
            guard.render_senders.remove(name);
        }
    }
    drop(tx);
    let _ = writer.await;
    Ok(())
}

fn ticket_response(
    request_id: String,
    result: crate::service_host::TicketRedeemResult,
) -> service_core::ServiceMessage {
    service_core::ServiceMessage {
        r#type: service_core::ServiceMessageType::RedeemConnectionTicketResp as i32,
        redeem_connection_ticket_resp: Some(service_core::MsgRedeemConnectionTicketResp {
            request_id,
            ok: result.ok,
            code: result.code,
            grant: result.ok.then_some(service_core::MsgConnectionGrant {
                kind: result.kind,
                device_id: result.device_id,
                app_id: result.app_id,
                instance_id: result.instance_id,
                subject_type: result.subject_type,
                subject_id: result.subject_id,
                permissions: result.permissions,
                expires_at: result.expires_at,
            }),
        }),
        ..Default::default()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc as StdArc, Mutex as StdMutex};
    use std::time::Duration;

    use crate::service_host::ServiceRuntime;
    use crate::windows_actions::SystemActions;
    use crate::windows_actions::WindowsActions;
    use crate::windows_process::{ProcessManager, WindowsProcessManager};
    use service_core::config::ServiceConfig;
    use service_core::process::ProcessSnapshot;
    use service_core::windows_util::{default_service_data_root, default_service_log_root};
    use service_core::{
        encode_service_message, MsgHeartBeat, MsgStartServer, RenderLaunchSpec, RenderStatus,
        ServiceMessage, ServiceMessageType,
    };
    use tokio_tungstenite::{
        connect_async, tungstenite::client::IntoClientRequest, tungstenite::Message,
    };

    struct MockProcessManager {
        processes: StdMutex<Vec<ProcessSnapshot>>,
        launches: StdMutex<Vec<RenderLaunchSpec>>,
    }

    impl MockProcessManager {
        fn new() -> Self {
            Self {
                processes: StdMutex::new(Vec::new()),
                launches: StdMutex::new(Vec::new()),
            }
        }
    }

    impl ProcessManager for MockProcessManager {
        fn list_processes(&self) -> Result<Vec<ProcessSnapshot>, String> {
            Ok(self.processes.lock().unwrap().clone())
        }

        fn kill_process(&self, pid: u32) -> Result<(), String> {
            self.processes
                .lock()
                .unwrap()
                .retain(|process| process.pid != pid);
            Ok(())
        }

        fn start_process_as_active_user(
            &self,
            work_dir: &str,
            app_path: &str,
            args: &[String],
        ) -> Result<(), String> {
            self.launches.lock().unwrap().push(RenderLaunchSpec {
                work_dir: work_dir.to_string(),
                app_path: app_path.to_string(),
                args: args.to_vec(),
            });
            self.processes.lock().unwrap().push(ProcessSnapshot::new(
                4242,
                app_path,
                args.join(" "),
            ));
            Ok(())
        }
    }

    struct MockActions;

    impl SystemActions for MockActions {
        fn send_ctrl_alt_delete(&self) -> Result<(), String> {
            Ok(())
        }
    }

    #[tokio::test]
    async fn websocket_service_keeps_runtime() {
        let runtime = Arc::new(Mutex::new(ServiceRuntime::new(
            ServiceConfig::new(
                20375,
                default_service_data_root(),
                default_service_log_root(),
            ),
            Arc::new(WindowsProcessManager::new()),
            Arc::new(WindowsActions::new()),
        )));
        let service = WebsocketService::new(runtime);
        assert_eq!(service.runtime.lock().await.config.listen_port, 20375);
    }

    #[tokio::test]
    async fn websocket_service_start_then_heartbeat_reports_working() {
        let port_listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = port_listener.local_addr().unwrap().port();
        drop(port_listener);

        let process_manager = StdArc::new(MockProcessManager::new());
        let mut config = ServiceConfig::new(
            port,
            default_service_data_root(),
            default_service_log_root(),
        );
        config.listen_host = "127.0.0.1".to_string();
        let runtime = Arc::new(Mutex::new(ServiceRuntime::new(
            config,
            process_manager.clone(),
            StdArc::new(MockActions),
        )));
        let service = WebsocketService::new(runtime.clone());
        let task = tokio::spawn(async move { service.run_console().await });
        tokio::time::sleep(Duration::from_millis(150)).await;

        let token = runtime.lock().await.ipc_token.clone();
        let mut request = format!("ws://127.0.0.1:{port}/service/message")
            .into_client_request()
            .unwrap();
        request
            .headers_mut()
            .insert("authorization", format!("Bearer {token}").parse().unwrap());
        let (mut ws, _) = connect_async(request).await.unwrap();
        let start = ServiceMessage {
            r#type: ServiceMessageType::StartServer as i32,
            start_server: Some(MsgStartServer {
                work_dir: "D:/px".to_string(),
                app_path: "D:/px/px_render.exe".to_string(),
                args: vec!["--app_mode=desktop".to_string()],
            }),
            ..Default::default()
        };
        ws.send(Message::Binary(encode_service_message(&start).into()))
            .await
            .unwrap();

        let heartbeat = ServiceMessage {
            r#type: ServiceMessageType::HeartBeat as i32,
            heart_beat: Some(MsgHeartBeat {
                index: 77,
                from: "panel".to_string(),
                ..Default::default()
            }),
            ..Default::default()
        };
        ws.send(Message::Binary(encode_service_message(&heartbeat).into()))
            .await
            .unwrap();
        let response = ws.next().await.unwrap().unwrap();
        let Message::Binary(bytes) = response else {
            panic!("expected binary response");
        };
        let decoded = service_core::decode_service_message(&bytes).unwrap();
        assert_eq!(
            decoded.heart_beat_resp.unwrap().render_status_enum(),
            Some(RenderStatus::Working)
        );
        assert_eq!(process_manager.launches.lock().unwrap().len(), 2);

        task.abort();
    }

    #[tokio::test]
    async fn websocket_service_accepts_loopback_panel_controller_without_render_token() {
        let port_listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = port_listener.local_addr().unwrap().port();
        drop(port_listener);

        let mut config = ServiceConfig::new(
            port,
            default_service_data_root(),
            default_service_log_root(),
        );
        config.listen_host = "127.0.0.1".to_string();
        let runtime = Arc::new(Mutex::new(ServiceRuntime::new(
            config,
            StdArc::new(MockProcessManager::new()),
            StdArc::new(MockActions),
        )));
        let service = WebsocketService::new(runtime);
        let task = tokio::spawn(async move { service.run_console().await });
        tokio::time::sleep(Duration::from_millis(150)).await;

        let panel_url = format!("ws://127.0.0.1:{port}/service/message?from=panel");
        let (mut panel, _) = connect_async(panel_url).await.unwrap();
        panel.close(None).await.unwrap();

        let render_url = format!("ws://127.0.0.1:{port}/service/message?from=render");
        assert!(connect_async(render_url).await.is_err());

        task.abort();
    }
}
