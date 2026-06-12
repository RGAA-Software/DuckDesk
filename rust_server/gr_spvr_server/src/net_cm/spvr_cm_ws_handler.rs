use std::collections::HashMap;
use std::net::SocketAddr;
use std::ops::ControlFlow;
use std::sync::Arc;
use axum::extract::{ConnectInfo, Query, State, WebSocketUpgrade};
use axum::extract::ws::{Message, WebSocket};
use axum::response::IntoResponse;
use axum_extra::TypedHeader;
use futures_util::StreamExt;
use tokio::sync::Mutex;
use crate::{gSpvrCMMgr};
use crate::net_cm::spvr_cm_conn::SpvrCmConn;
use crate::spvr_context::SpvrContext;

pub(crate) async fn cm_handler(
    State(context): State<Arc<Mutex<SpvrContext>>>,
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
    ws.on_upgrade(move |socket| {
        handle_ws_socket(context.clone(), params, socket, addr)
    })
}

async fn handle_ws_socket(context: Arc<Mutex<SpvrContext>>,
                       params: HashMap<String, String>,
                       socket: WebSocket,
                       who: SocketAddr) {
    let (sender, mut receiver) = socket.split();

    let mut recv_task = tokio::spawn(async move {
        let appkey = params.get("appkey").unwrap();

        let sender = Arc::new(Mutex::new(sender));
        let panel_conn = SpvrCmConn::new(context.clone(),
                                            sender,
                                            appkey.clone()).await;

        let id = format!("{}-{}", who.ip().to_string(), who.port());
        tracing::info!("ws connect from {}, id: {}", who, id);
        let spvr_conn = Arc::new(Mutex::new(panel_conn));

        // start hardware info back streamer
        SpvrCmConn::start_hardware_info_streamer(spvr_conn.clone()).await;

        gSpvrCMMgr
            .add_cm_conn(id.clone(), spvr_conn.clone()).await;

        while let Some(Ok(msg)) = receiver.next().await {
            // print message and break if instructed to do so
            if process_cm_message(context.clone(), spvr_conn.clone(), msg, who).await.is_break() {
                break;
            }
        }

        // remove
        gSpvrCMMgr
            .remove_cm_conn(id).await;

    });

    tokio::select! {
            spvr_rv = (&mut recv_task) => {
                match spvr_rv {
                    Ok(_) => {},
                    Err(e) => {
                        tracing::error!("receive task error: {e:?}")
                    }
                }
                recv_task.abort();
            },
        }
}

async fn process_cm_message(_spvr_ctx: Arc<Mutex<SpvrContext>>,
                         cm_conn: Arc<Mutex<SpvrCmConn>>,
                         msg: Message,
                         who: SocketAddr)
                         -> ControlFlow<(), ()> {
    //tracing::info!("IN --> {}:{}", who.ip(), who.port());
    match msg {
        Message::Text(data) => {
            return if cm_conn
                .lock().await
                .process_message(who.ip().to_string(), data.to_string()).await {
                ControlFlow::Continue(())
            } else {
                ControlFlow::Break(())
            }
        }
        Message::Binary(data) => {
            return ControlFlow::Continue(());
        }
        Message::Close(c) => {
            if let Some(cf) = c {
                println!(">>> {} sent close with code {} and reason `{}`", who, cf.code, cf.reason);
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