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

use crate::connection_ticket::manager::ConnectionTicketManager;
use crate::console_api_error::ConsoleApiError;
use crate::console_context::ConsoleContext;
use crate::console_relay::relay_conn::{RelayConn, RelayMediaTicketAuthorization};
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

/// A browser guest has no Console capability. Its Relay connection is limited
/// to the explicitly named device; Render remains the authentication authority
/// and validates the password digest carried by the SDP offer.
pub(crate) fn is_scoped_guest_rtc_signal(params: &HashMap<String, String>) -> bool {
    if !params
        .get("guest_password")
        .is_some_and(|value| value == "1")
        || params.contains_key("ticket")
    {
        return false;
    }
    let (Some(client), Some(remote), Some(device_id), Some(stream_id)) = (
        params.get("device_id"),
        params.get("remote_device_id"),
        params.get("ticket_device_id"),
        params.get("stream_id"),
    ) else {
        return false;
    };
    client.starts_with("web_")
        && !stream_id.is_empty()
        && !device_id.is_empty()
        && remote == &format!("server_{device_id}")
}

pub(crate) async fn validate_ticketed_media_relay(
    params: &HashMap<String, String>,
) -> Result<Option<RelayMediaTicketAuthorization>, ConsoleApiError> {
    if !params.get("media_ticket").is_some_and(|value| value == "1") {
        return Ok(None);
    }
    let (
        Some(ticket),
        Some(client_nonce),
        Some(ticket_device_id),
        Some(remote),
        Some(client),
        Some(stream_id),
    ) = (
        params.get("ticket"),
        params.get("client_nonce"),
        params.get("ticket_device_id"),
        params.get("remote_device_id"),
        params.get("device_id"),
        params.get("stream_id"),
    )
    else {
        return Err(ConsoleApiError::InvalidParams);
    };
    if !client.starts_with("client_") || stream_id.is_empty() {
        return Err(ConsoleApiError::InvalidParams);
    }
    let instance_id = params.get("instance_id").map(String::as_str);
    let active =
        ConnectionTicketManager::lookup_active(ticket, ticket_device_id, client_nonce, instance_id)
            .await?;
    let expected_remote = active
        .instance_id
        .as_deref()
        .filter(|value| !value.is_empty())
        .map(|instance| format!("server_{}__instance__{}", active.device_id, instance))
        .unwrap_or_else(|| format!("server_{}", active.device_id));
    if remote != &expected_remote || stream_id != &active.stream_id {
        tracing::warn!(
            ticket_device_id,
            remote_device_id = remote,
            "ticketed media Relay target or stream does not match ticket binding"
        );
        return Err(ConsoleApiError::Forbidden);
    }
    Ok(Some(RelayMediaTicketAuthorization {
        ticket: ticket.clone(),
        client_nonce: client_nonce.clone(),
        instance_id: instance_id.unwrap_or_default().to_string(),
        remote_device_id: expected_remote,
    }))
}

fn authorize_relay_control(
    connection_device_id: &str,
    authorized_remote_device_id: Option<&str>,
    media_ticket_authorization: Option<&RelayMediaTicketAuthorization>,
    control: &mut RelayRequestControlMessage,
) -> bool {
    if control.device_id != connection_device_id
        || authorized_remote_device_id.is_some_and(|expected| control.remote_device_id != expected)
    {
        return false;
    }
    if let Some(ticket) = media_ticket_authorization {
        if control.remote_device_id != ticket.remote_device_id {
            return false;
        }
        control.connection_ticket = ticket.ticket.clone();
        control.client_nonce = ticket.client_nonce.clone();
        control.instance_id = ticket.instance_id.clone();
        return true;
    }
    control.connection_ticket.is_empty()
        && control.client_nonce.is_empty()
        && control.instance_id.is_empty()
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
            let sensitive =
                matches!(k.as_str(), "ticket" | "appkey" | "client_nonce") || k.contains("pwd");
            tracing::info!(
                "ws query param {}:{}",
                k,
                if sensitive { "<redacted>" } else { v }
            );
        }
        let params = query.0.clone();
        let media_ticket_authorization = match validate_ticketed_media_relay(&params).await {
            Ok(value) => value,
            Err(error) => return error.into_response(),
        };
        // File-only clients carry their Console capability in the Relay handshake.
        // Redeem it before allocating any relay connection/room, so a ticket
        // cannot be used to create a media room and cannot be replayed.
        let standalone_file = params.get("file_only").is_some_and(|value| value == "1");
        if standalone_file {
            let (Some(ticket), Some(nonce), Some(remote), Some(client)) = (
                params.get("ticket"),
                params.get("client_nonce"),
                params.get("remote_device_id"),
                params.get("device_id"),
            ) else {
                return crate::console_api_error::ConsoleApiError::InvalidParams.into_response();
            };
            let (Some(device_id), true) = (
                remote.strip_prefix("ft_server_"),
                client.starts_with("ft_client_"),
            ) else {
                return crate::console_api_error::ConsoleApiError::InvalidParams.into_response();
            };
            let request_id = format!(
                "relay:{}",
                params.get("stream_id").cloned().unwrap_or_default()
            );
            match ConnectionTicketManager::redeem(ticket, device_id, nonce, None, &request_id).await
            {
                Ok(grant) if grant.permissions.iter().any(|p| p == "file") => {}
                Ok(_) => {
                    return crate::console_api_error::ConsoleApiError::Forbidden.into_response()
                }
                Err(err) => return err.into_response(),
            }
        } else if media_ticket_authorization.is_some() {
            // Validation above intentionally does not consume the one-time
            // ticket. Render redeems it when deciding whether to accept the
            // control request and bind the logical session.
        } else if params.get("rtc_signal").is_some_and(|value| value == "1") {
            if is_scoped_guest_rtc_signal(&params) {
                tracing::info!(
                    remote_device_id = params
                        .get("remote_device_id")
                        .map(String::as_str)
                        .unwrap_or(""),
                    "accepted password-authenticated guest RTC signaling socket"
                );
            } else {
                let (Some(ticket), Some(nonce), Some(remote), Some(ticket_device_id)) = (
                    params.get("ticket"),
                    params.get("client_nonce"),
                    params.get("remote_device_id"),
                    params.get("ticket_device_id"),
                ) else {
                    return crate::console_api_error::ConsoleApiError::InvalidParams
                        .into_response();
                };
                let active = ConnectionTicketManager::lookup_active(
                    ticket,
                    ticket_device_id,
                    nonce,
                    params.get("instance_id").map(String::as_str),
                )
                .await;
                let active = match active {
                    Ok(value) => value,
                    Err(error) => return error.into_response(),
                };
                let expected_remote = active
                    .instance_id
                    .as_deref()
                    .filter(|value| !value.is_empty())
                    .map(|instance_id| {
                        format!("server_{}__instance__{}", active.device_id, instance_id)
                    })
                    .unwrap_or_else(|| format!("server_{}", active.device_id));
                if remote != &expected_remote {
                    tracing::warn!(
                        ticket_device_id,
                        remote_device_id = remote,
                        expected_remote_device_id = expected_remote,
                        "RTC signaling target does not match ticket binding"
                    );
                    return crate::console_api_error::ConsoleApiError::Forbidden.into_response();
                }
            }
        } else if params.contains_key("ticket") || params.contains_key("client_nonce") {
            // Capability material is accepted only on the explicitly scoped
            // standalone file route.
            return crate::console_api_error::ConsoleApiError::InvalidParams.into_response();
        }
        ws.on_upgrade(move |socket| {
            RelayServer::handle_socket(
                context.clone(),
                params,
                media_ticket_authorization,
                socket,
                addr,
            )
        })
    }

    async fn handle_socket(
        context: Arc<Mutex<ConsoleContext>>,
        params: HashMap<String, String>,
        media_ticket_authorization: Option<RelayMediaTicketAuthorization>,
        socket: WebSocket,
        who: SocketAddr,
    ) {
        let (sender, mut receiver) = socket.split();

        let mut recv_task = tokio::spawn(async move {
            // device id
            let device_id = params.get("device_id").unwrap_or(&"".to_string()).clone();
            let device_name = params.get("device_name").unwrap_or(&"".to_string()).clone();
            let stream_id = params.get("stream_id").unwrap_or(&"".to_string()).clone();
            let authorized_remote_device_id = params
                .get("remote_device_id")
                .filter(|_| {
                    params.get("file_only").is_some_and(|value| value == "1")
                        || params.get("media_ticket").is_some_and(|value| value == "1")
                        || params.get("rtc_signal").is_some_and(|value| value == "1")
                })
                .cloned();
            // socket sender
            let sender = Arc::new(Mutex::new(sender));

            // www host
            let addr = who.clone().to_string();
            let mut t = addr.splitn(2, ':');
            let client_w3c_host = t.next().unwrap_or("").to_string();

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
                media_ticket_authorization,
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
                    Err(e) => {
                        tracing::error!("receive task error: {e:?}")
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
                    .append_upload_data_size(data.len() as i64)
                    .await;
                let m = RelayMessage::decode(data.clone());
                if let Err(e) = m {
                    tracing::error!("decode relay message failed: {}", e);
                    return ControlFlow::Break(());
                }
                let m = m.unwrap();
                let m_type = m.r#type;
                //tracing::info!("from: {} message type: {}", m.from_device_id, m_type);

                if m_type == RelayMessageType::KRelayHello {
                    relay_conn.lock().await.on_hello(m).await;

                    // send back
                    let data_cpy = data.clone();
                    tokio::spawn(async move {
                        relay_conn.lock().await.send_bin_message(data_cpy).await;
                    });
                } else if m_type == RelayMessageType::KRelayHeartBeat {
                    relay_conn.lock().await.on_heartbeat(m).await;

                    // send back
                    let data_cpy = data.clone();
                    tokio::spawn(async move {
                        relay_conn.lock().await.send_bin_message(data_cpy).await;
                    });
                } else if m_type == RelayMessageType::KRelayError {
                    relay_conn.lock().await.on_error(m).await
                } else if m_type == RelayMessageType::KRelayTargetMessage {
                    gRelayRoomMgr.on_relay(m, data).await;
                } else if m_type == RelayMessageType::KRelayCreateRoom {
                    let allowed = {
                        let conn = relay_conn.lock().await;
                        match (&conn.authorized_remote_device_id, m.create_room.as_ref()) {
                            (Some(expected), Some(room)) => {
                                room.device_id == conn.device_id
                                    && room.remote_device_id == *expected
                            }
                            (Some(_), None) => false,
                            (None, _) => true,
                        }
                    };
                    if !allowed {
                        tracing::warn!("reject relay room outside standalone file ticket scope");
                        return ControlFlow::Break(());
                    }
                    gRelayRoomMgr.on_create_room(m, data).await;
                } else if m_type == RelayMessageType::KRelayRequestControl {
                    let mut request = m;
                    let authorized = {
                        let conn = relay_conn.lock().await;
                        match request.request_control.as_mut() {
                            Some(control) => authorize_relay_control(
                                &conn.device_id,
                                conn.authorized_remote_device_id.as_deref(),
                                conn.media_ticket_authorization.as_ref(),
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
                } else if m_type == RelayMessageType::KRelayRequestControlResp {
                    gRelayRoomMgr.on_request_control_resp(m, data).await;
                } else if m_type == RelayMessageType::KRelayRequestPausedStream
                    || m_type == RelayMessageType::KRelayRequestResumeStream
                {
                    gRelayRoomMgr.on_request_resume_pause_stream(m, data).await;
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
    use super::{authorize_relay_control, is_scoped_guest_rtc_signal};
    use crate::console_relay::relay_conn::RelayMediaTicketAuthorization;
    use protocol::px_relay::RelayRequestControlMessage;
    use std::collections::HashMap;

    fn valid_params() -> HashMap<String, String> {
        HashMap::from([
            ("guest_password".into(), "1".into()),
            ("device_id".into(), "web_123".into()),
            ("remote_device_id".into(), "server_001190520".into()),
            ("ticket_device_id".into(), "001190520".into()),
            ("stream_id".into(), "desktop".into()),
        ])
    }

    #[test]
    fn accepts_only_a_scoped_password_guest() {
        assert!(is_scoped_guest_rtc_signal(&valid_params()));

        let mut wrong_target = valid_params();
        wrong_target.insert("remote_device_id".into(), "server_other".into());
        assert!(!is_scoped_guest_rtc_signal(&wrong_target));

        let mut ticket_mixed_in = valid_params();
        ticket_mixed_in.insert("ticket".into(), "must-not-mix".into());
        assert!(!is_scoped_guest_rtc_signal(&ticket_mixed_in));

        let mut native_client = valid_params();
        native_client.insert("device_id".into(), "native_123".into());
        assert!(!is_scoped_guest_rtc_signal(&native_client));
    }

    #[test]
    fn ticketed_control_uses_authoritative_connection_binding() {
        let ticket = RelayMediaTicketAuthorization {
            ticket: "ticket-from-query".into(),
            client_nonce: "nonce-from-query".into(),
            instance_id: "instance-from-query".into(),
            remote_device_id: "server_target__instance__instance-from-query".into(),
        };
        let mut control = RelayRequestControlMessage {
            device_id: "client_visitor".into(),
            remote_device_id: ticket.remote_device_id.clone(),
            connection_ticket: "attacker-ticket".into(),
            client_nonce: "attacker-nonce".into(),
            instance_id: "attacker-instance".into(),
            ..Default::default()
        };

        assert!(authorize_relay_control(
            "client_visitor",
            Some(&ticket.remote_device_id),
            Some(&ticket),
            &mut control,
        ));
        assert_eq!(control.connection_ticket, ticket.ticket);
        assert_eq!(control.client_nonce, ticket.client_nonce);
        assert_eq!(control.instance_id, ticket.instance_id);
    }

    #[test]
    fn relay_control_rejects_scope_escalation() {
        let mut wrong_target = RelayRequestControlMessage {
            device_id: "client_visitor".into(),
            remote_device_id: "server_other".into(),
            ..Default::default()
        };
        assert!(!authorize_relay_control(
            "client_visitor",
            Some("server_target"),
            None,
            &mut wrong_target,
        ));

        let mut legacy_with_ticket = RelayRequestControlMessage {
            device_id: "client_visitor".into(),
            remote_device_id: "server_target".into(),
            connection_ticket: "untrusted-ticket".into(),
            ..Default::default()
        };
        assert!(!authorize_relay_control(
            "client_visitor",
            None,
            None,
            &mut legacy_with_ticket,
        ));
    }
}
