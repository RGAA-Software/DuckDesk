use axum::body::Bytes;
use axum::extract::{ConnectInfo, Query, State};
use axum::routing::{get, post};
use axum::serve::ListenerExt;
use axum::{
    extract::ws::{Message, WebSocket, WebSocketUpgrade},
    middleware,
    response::IntoResponse,
    routing::any,
    Json, Router,
};
use axum_extra::TypedHeader;
use futures_util::StreamExt;
use prost::Message as ProstMessage;
use std::collections::HashMap;
use std::net::SocketAddr;
use std::ops::ControlFlow;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::Mutex;

use crate::console_context::ConsoleContext;
use crate::console_relay::relay_conn::RelayConn;
use crate::console_relay::{relay_device_handler, relay_room_handler};
use crate::filter::{console_appkey_filter, console_statistics_filter, console_timer_filter};
use crate::{gRelayConnMgr, gRelayRoomMgr};
use protocol::px_relay::{RelayMessage, RelayMessageType, RelayRequestControlMessage};
use px_base::{get_current_timestamp, RespMessage};
use tower_http::services::ServeDir;

pub struct RelayServer {
    pub host: String,
    pub port: u16,
    pub context: Arc<Mutex<ConsoleContext>>,
}

/// Browser signaling is limited to its declared Render target. Render remains
/// the authentication authority and validates the password digest in the SDP offer.
pub(crate) fn is_password_rtc_signal(params: &HashMap<String, String>) -> bool {
    if !params
        .get("password_auth")
        .is_some_and(|value| value == "1")
    {
        return false;
    }
    let (Some(client), Some(remote), Some(device_id), Some(stream_id)) = (
        params.get("device_id"),
        params.get("remote_device_id"),
        params.get("target_device_id"),
        params.get("stream_id"),
    ) else {
        return false;
    };
    let device_identity = format!("server_{device_id}");
    let instance_prefix = format!("{device_identity}__instance__");
    client.starts_with("web_")
        && !stream_id.is_empty()
        && !device_id.is_empty()
        && (remote == &device_identity || remote.starts_with(&instance_prefix))
}

fn authorize_relay_control(
    connection_device_id: &str,
    authorized_remote_device_id: Option<&str>,
    control: &RelayRequestControlMessage,
) -> bool {
    control.device_id == connection_device_id
        && authorized_remote_device_id.is_none_or(|expected| control.remote_device_id == expected)
}

impl RelayServer {
    pub fn new(host: String, port: u16, context: Arc<Mutex<ConsoleContext>>) -> RelayServer {
        RelayServer {
            host,
            port,
            context,
        }
    }

    pub async fn start(&self) {
        let assets_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("assets");

        let app = Router::new()
            .fallback_service(ServeDir::new(assets_dir).append_index_html_on_directories(true))
            .route("/ping", get(RelayServer::ping))
            .route("/relay", any(RelayServer::ws_handler))
            .route("/query/room", get(relay_room_handler::hr_query_room))
            .route(
                "/query/total/rooms",
                get(relay_room_handler::hr_query_total_rooms),
            )
            .route(
                "/query/total/alive/rooms",
                get(relay_room_handler::hr_query_total_alive_rooms),
            )
            .route(
                "/query/devices",
                get(relay_device_handler::hd_query_devices),
            )
            .route("/query/device", get(relay_device_handler::hd_query_device))
            .route("/notify/event", post(relay_device_handler::hd_notify_event))
            .layer(middleware::from_fn(console_appkey_filter::filter))
            .layer(middleware::from_fn(console_statistics_filter::filter))
            .layer(middleware::from_fn(console_timer_filter::filter))
            .with_state(self.context.clone());

        // run our app with hyper, listening globally on port 3000
        let listener = tokio::net::TcpListener::bind(format!("{}:{}", self.host, self.port))
            .await
            .unwrap()
            .tap_io(|tcp_stream| {
                if let Ok(nodelay) = tcp_stream.nodelay() {
                    if !nodelay {
                        if let Err(err) = tcp_stream.set_nodelay(true) {
                            tracing::error!(
                                "failed to set TCP_NODELAY on incoming connection: {err:#}"
                            );
                        }
                    }
                }
            });
        //axum::serve(listener, app).await.unwrap();
        axum::serve(
            listener,
            app.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .await
        .unwrap();
    }

    pub async fn ping(State(_ctx): State<Arc<Mutex<ConsoleContext>>>) -> Json<RespMessage<String>> {
        Json(RespMessage::<String> {
            code: 200,
            message: "ok".to_string(),
            timestamp: get_current_timestamp(),
            data: "Pong".to_string(),
        })
    }

    pub async fn ws_handler(
        State(context): State<Arc<Mutex<ConsoleContext>>>,
        query: Query<HashMap<String, String>>,
        ws: WebSocketUpgrade,
        user_agent: Option<TypedHeader<headers::UserAgent>>,
        ConnectInfo(addr): ConnectInfo<SocketAddr>,
    ) -> impl IntoResponse {
        let user_agent = if let Some(TypedHeader(user_agent)) = user_agent {
            user_agent.to_string()
        } else {
            String::from("Unknown browser")
        };
        tracing::info!("ws handshake from {}, agent: {}", addr, user_agent);
        for (k, v) in query.iter() {
            let sensitive = matches!(k.as_str(), "appkey" | "client_nonce")
                || k.contains("pwd")
                || k.contains("password");
            tracing::info!(
                "ws query param {}:{}",
                k,
                if sensitive { "<redacted>" } else { v }
            );
        }
        let params = query.0.clone();
        if params.get("rtc_signal").is_some_and(|value| value == "1")
            && !is_password_rtc_signal(&params)
        {
            return crate::console_api_error::ConsoleApiError::InvalidParams.into_response();
        }
        ws.on_upgrade(move |socket| {
            RelayServer::handle_socket(context.clone(), params, socket, addr)
        })
    }

    async fn handle_socket(
        context: Arc<Mutex<ConsoleContext>>,
        params: HashMap<String, String>,
        socket: WebSocket,
        who: SocketAddr,
    ) {
        let (sender, mut receiver) = socket.split();

        let mut recv_task = tokio::spawn(async move {
            // device id
            let device_id = params.get("device_id").unwrap_or(&"".to_string()).clone();
            let device_name = params.get("device_name").unwrap_or(&"".to_string()).clone();
            let stream_id = params.get("stream_id").unwrap_or(&"".to_string()).clone();
            // Every Relay socket is restricted to the target declared by its
            // handshake. Password-authenticated native sessions do not need a
            // authenticated connection, but they must not be able to switch targets after
            // the connection has been accepted.
            let authorized_remote_device_id = params.get("remote_device_id").cloned();
            // socket sender
            let sender = Arc::new(Mutex::new(sender));

            // www host
            let addr = who.clone().to_string();
            let mut address_parts = addr.splitn(2, ':');
            let client_w3c_host = address_parts.next().unwrap_or("").to_string();

            tracing::info!(
                "connected device id: {}, client w3c host: {}, device name: {}, stream id: {}",
                device_id,
                client_w3c_host,
                device_name,
                stream_id
            );

            // make relay conn
            let relay_conn = RelayConn::new(
                context.clone(),
                sender,
                device_id.clone(),
                client_w3c_host,
                device_name,
                stream_id,
                authorized_remote_device_id,
            )
            .await;

            // add to manager
            gRelayConnMgr
                .add_connection(device_id.clone(), relay_conn.clone())
                .await;

            tracing::info!("will receive messages");
            // wait for messages
            while let Some(Ok(msg)) = receiver.next().await {
                // print message and break if instructed to do so
                if RelayServer::process_message(context.clone(), relay_conn.clone(), msg, who)
                    .await
                    .is_break()
                {
                    break;
                }
            }

            tracing::info!("remove device: {}", device_id);

            // remove connection
            relay_conn.lock().await.last_relay_msg_index = 0;
            if gRelayConnMgr
                .remove_connection_if_current(&device_id, &relay_conn)
                .await
            {
                // Only the currently registered socket owns device-level
                // cleanup. A stale receive task must not tear down rooms or the
                // replacement socket that was registered during reconnect.
                gRelayRoomMgr
                    .notify_remote_device_offline(device_id.clone())
                    .await;
                gRelayRoomMgr
                    .destroy_room_i_created(device_id.clone())
                    .await;
                gRelayRoomMgr
                    .clear_info_in_rooms_i_was_invited(device_id)
                    .await;
            } else {
                tracing::info!(
                    "skip stale relay disconnect cleanup because a replacement is active: {}",
                    device_id
                );
            }
        });

        tokio::select! {
            rv_a = (&mut recv_task) => {
                match rv_a {
                    Ok(_) => {},
                    Err(receive_error) => {
                        tracing::error!("receive task error: {receive_error:?}")
                    }
                }
                recv_task.abort();
            },
        }
    }

    async fn process_message(
        _context: Arc<Mutex<ConsoleContext>>,
        relay_conn: Arc<Mutex<RelayConn>>,
        msg: Message,
        who: SocketAddr,
    ) -> ControlFlow<(), ()> {
        match msg {
            Message::Text(_data) => {
                // // append received data size
                // relay_conn.lock().await.append_received_data_size(data.len() as i64).await;
                // // parse json
                // let value: serde_json::error::Result<serde_json::Value> = serde_json::from_str(data.as_str());
                // if let Err(e) = value {
                //     tracing::error!("parse json error: {e}, json: {}", data.to_string());
                //     //return ControlFlow::Break(());
                // }
            }
            Message::Binary(data) => {
                relay_conn
                    .lock()
                    .await
                    .append_upload_data_size(data.len() as i64);
                let decoded_message = RelayMessage::decode(data.clone());
                if let Err(decode_error) = decoded_message {
                    tracing::error!("decode relay message failed: {}", decode_error);
                    return ControlFlow::Break(());
                }
                let decoded_message = decoded_message.unwrap();
                let message_type = decoded_message.r#type;
                //tracing::info!("from: {} message type: {}", m.from_device_id, m_type);

                if message_type == RelayMessageType::KRelayHello {
                    relay_conn.lock().await.on_hello(decoded_message).await;

                    // send back
                    let data_cpy = data.clone();
                    tokio::spawn(async move {
                        relay_conn.lock().await.send_bin_message(data_cpy).await;
                    });
                } else if message_type == RelayMessageType::KRelayHeartBeat {
                    relay_conn.lock().await.on_heartbeat(decoded_message).await;

                    // send back
                    let data_cpy = data.clone();
                    tokio::spawn(async move {
                        relay_conn.lock().await.send_bin_message(data_cpy).await;
                    });
                } else if message_type == RelayMessageType::KRelayError {
                    relay_conn.lock().await.on_error(decoded_message).await
                } else if message_type == RelayMessageType::KRelayTargetMessage {
                    gRelayRoomMgr.on_relay(decoded_message, data).await;
                } else if message_type == RelayMessageType::KRelayCreateRoom {
                    let allowed = {
                        let conn = relay_conn.lock().await;
                        match (
                            &conn.authorized_remote_device_id,
                            decoded_message.create_room.as_ref(),
                        ) {
                            (Some(expected), Some(room)) => {
                                room.device_id == conn.device_id
                                    && room.remote_device_id == *expected
                            }
                            (Some(_), None) => false,
                            (None, _) => true,
                        }
                    };
                    if !allowed {
                        tracing::warn!("reject Relay room outside the declared remote target");
                        return ControlFlow::Break(());
                    }
                    gRelayRoomMgr.on_create_room(decoded_message, data).await;
                } else if message_type == RelayMessageType::KRelayRequestControl {
                    let request = decoded_message;
                    let authorized = {
                        let conn = relay_conn.lock().await;
                        match request.request_control.as_ref() {
                            Some(control) => authorize_relay_control(
                                &conn.device_id,
                                conn.authorized_remote_device_id.as_deref(),
                                control,
                            ),
                            _ => false,
                        }
                    };
                    if !authorized {
                        tracing::warn!(
                            "reject Relay control request outside authenticated connection scope"
                        );
                        return ControlFlow::Break(());
                    }
                    let encoded = Bytes::from(request.encode_to_vec());
                    gRelayRoomMgr.on_request_control(request, encoded).await;
                } else if message_type == RelayMessageType::KRelayRequestControlResp {
                    gRelayRoomMgr
                        .on_request_control_resp(decoded_message, data)
                        .await;
                } else if message_type == RelayMessageType::KRelayRequestPausedStream
                    || message_type == RelayMessageType::KRelayRequestResumeStream
                {
                    gRelayRoomMgr
                        .on_request_resume_pause_stream(decoded_message, data)
                        .await;
                }
                return ControlFlow::Continue(());
            }
            Message::Close(c) => {
                if let Some(cf) = c {
                    println!(
                        ">>> {} sent close with code {} and reason `{}`",
                        who, cf.code, cf.reason
                    );
                } else {
                    println!(">>> {who} somehow sent close message without CloseFrame");
                }
                return ControlFlow::Break(());
            }

            Message::Pong(_v) => {}
            // You should never need to manually handle Message::Ping, as axum's websocket library
            // will do so for you automagically by replying with Pong and copying the v according to
            // spec. But if you need the contents of the pings you can see them here.
            Message::Ping(v) => {
                println!(">>> {who} sent ping with {v:?}");
            }
        }
        ControlFlow::Continue(())
    }
}

#[cfg(test)]
mod relay_scope_tests {
    use super::{authorize_relay_control, is_password_rtc_signal};
    use protocol::px_relay::RelayRequestControlMessage;
    use std::collections::HashMap;

    fn valid_params() -> HashMap<String, String> {
        HashMap::from([
            ("password_auth".into(), "1".into()),
            ("device_id".into(), "web_123".into()),
            ("remote_device_id".into(), "server_001190520".into()),
            ("target_device_id".into(), "001190520".into()),
            ("stream_id".into(), "desktop".into()),
        ])
    }

    #[test]
    fn accepts_only_scoped_password_signaling() {
        assert!(is_password_rtc_signal(&valid_params()));

        let mut wrong_target = valid_params();
        wrong_target.insert("remote_device_id".into(), "server_other".into());
        assert!(!is_password_rtc_signal(&wrong_target));

        let mut native_client = valid_params();
        native_client.insert("device_id".into(), "native_123".into());
        assert!(!is_password_rtc_signal(&native_client));
    }

    #[test]
    fn password_control_preserves_render_credential() {
        let control = RelayRequestControlMessage {
            device_id: "client_visitor".into(),
            remote_device_id: "server_target".into(),
            safety_pwd_md5: "password-hash".into(),
            ..Default::default()
        };

        assert!(authorize_relay_control(
            "client_visitor",
            Some("server_target"),
            &control,
        ));
        assert_eq!(control.safety_pwd_md5, "password-hash");
    }

    #[test]
    fn relay_control_rejects_scope_escalation() {
        let wrong_target = RelayRequestControlMessage {
            device_id: "client_visitor".into(),
            remote_device_id: "server_other".into(),
            ..Default::default()
        };
        assert!(!authorize_relay_control(
            "client_visitor",
            Some("server_target"),
            &wrong_target,
        ));

        let wrong_client = RelayRequestControlMessage {
            device_id: "client_other".into(),
            remote_device_id: "server_target".into(),
            ..Default::default()
        };
        assert!(!authorize_relay_control(
            "client_visitor",
            None,
            &wrong_client,
        ));
    }

    #[test]
    fn password_authenticated_relay_control_accepts_bound_target() {
        let control = RelayRequestControlMessage {
            device_id: "client_visitor".into(),
            remote_device_id: "server_target".into(),
            ..Default::default()
        };

        assert!(authorize_relay_control(
            "client_visitor",
            Some("server_target"),
            &control,
        ));
    }
}
