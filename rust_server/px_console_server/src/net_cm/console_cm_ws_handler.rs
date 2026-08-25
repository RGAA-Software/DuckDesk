use crate::console_context::ConsoleContext;
use crate::gConsoleCMMgr;
use crate::net_cm::console_cm_conn::ConsoleCmConn;
use axum::extract::ws::{Message, WebSocket};
use axum::extract::{ConnectInfo, Query, State, WebSocketUpgrade};
use axum::response::IntoResponse;
use axum_extra::TypedHeader;
use futures_util::StreamExt;
use std::collections::HashMap;
use std::net::SocketAddr;
use std::ops::ControlFlow;
use std::sync::Arc;
use tokio::sync::Mutex;

pub(crate) async fn cm_handler(
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
        tracing::info!("ws query param {}:{}", k, v);
    }
    let params = query.0.clone();
    ws.on_upgrade(move |socket| handle_ws_socket(context.clone(), params, socket, addr))
}

async fn handle_ws_socket(
    context: Arc<Mutex<ConsoleContext>>,
    params: HashMap<String, String>,
    socket: WebSocket,
    who: SocketAddr,
) {
    let (sender, mut receiver) = socket.split();

    let mut recv_task = tokio::spawn(async move {
        // Browser administrators authenticate `/console/website` with their
        // HttpOnly session cookie, so that endpoint intentionally has no
        // appkey query parameter. The value is retained only for legacy
        // connections and is otherwise unused by ConsoleCmConn.
        let appkey = connection_appkey(&params);

        let sender = Arc::new(Mutex::new(sender));
        let panel_conn = ConsoleCmConn::new(context.clone(), sender, appkey).await;

        let id = format!("{}-{}", who.ip(), who.port());
        tracing::info!("ws connect from {}, id: {}", who, id);
        let console_conn = Arc::new(Mutex::new(panel_conn));

        // start hardware info back streamer
        ConsoleCmConn::start_hardware_info_streamer(console_conn.clone()).await;

        gConsoleCMMgr
            .add_cm_conn(id.clone(), console_conn.clone())
            .await;

        while let Some(Ok(msg)) = receiver.next().await {
            // print message and break if instructed to do so
            if process_cm_message(context.clone(), console_conn.clone(), msg, who)
                .await
                .is_break()
            {
                break;
            }
        }

        // remove
        gConsoleCMMgr.remove_cm_conn(id).await;
    });

    tokio::select! {
        console_rv = (&mut recv_task) => {
            match console_rv {
                Ok(_) => {},
                Err(e) => {
                    tracing::error!("receive task error: {e:?}")
                }
            }
            recv_task.abort();
        },
    }
}

fn connection_appkey(params: &HashMap<String, String>) -> String {
    params.get("appkey").cloned().unwrap_or_default()
}

async fn process_cm_message(
    _console_ctx: Arc<Mutex<ConsoleContext>>,
    cm_conn: Arc<Mutex<ConsoleCmConn>>,
    msg: Message,
    who: SocketAddr,
) -> ControlFlow<(), ()> {
    //tracing::info!("IN --> {}:{}", who.ip(), who.port());
    match msg {
        Message::Text(data) => {
            return if cm_conn
                .lock()
                .await
                .process_message(who.ip().to_string(), data.to_string())
                .await
            {
                ControlFlow::Continue(())
            } else {
                ControlFlow::Break(())
            }
        }
        Message::Binary(_data) => {
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
        Message::Ping(_v) => {}
    }
    ControlFlow::Continue(())
}

#[cfg(test)]
mod tests {
    use super::connection_appkey;
    use std::collections::HashMap;

    #[test]
    fn website_session_does_not_require_appkey_query_parameter() {
        assert_eq!(connection_appkey(&HashMap::new()), "");

        let mut legacy = HashMap::new();
        legacy.insert("appkey".to_string(), "legacy-key".to_string());
        assert_eq!(connection_appkey(&legacy), "legacy-key");
    }
}
