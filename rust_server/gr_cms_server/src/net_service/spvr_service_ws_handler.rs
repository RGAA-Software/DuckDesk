use crate::app_schedule::gAppScheduleManager;
use crate::gSpvrServiceConnMgr;
use crate::net_service::spvr_service_conn::SpvrServiceConn;
use crate::spvr_context::SpvrContext;
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

pub(crate) async fn service_handler(
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
    ws.on_upgrade(move |socket| handle_socket(context.clone(), params, socket, addr))
}

async fn handle_socket(
    context: Arc<Mutex<SpvrContext>>,
    params: HashMap<String, String>,
    socket: WebSocket,
    who: SocketAddr,
) {
    let (sender, mut receiver) = socket.split();

    let mut recv_task = tokio::spawn(async move {
        let appkey = params.get("appkey").unwrap();
        let device_id = params.get("device_id").unwrap_or(&"".to_string()).clone();
        if device_id.is_empty() {
            tracing::error!("spvr service error, device id is empty!");
            return;
        }

        let sender = Arc::new(Mutex::new(sender));
        let service_conn =
            SpvrServiceConn::new(context.clone(), sender, device_id.clone(), appkey.clone())
                .await;
        let spvr_conn = Arc::new(Mutex::new(service_conn));
        gSpvrServiceConnMgr
            .add_conn(device_id.clone(), spvr_conn.clone())
            .await;

        while let Some(Ok(msg)) = receiver.next().await {
            // print message and break if instructed to do so
            if process_message(context.clone(), spvr_conn.clone(), msg, who)
                .await
                .is_break()
            {
                break;
            }
        }

        // remove — Service gone: clear sticky Running/Stopping on this device.
        gSpvrServiceConnMgr.remove_conn(device_id.clone()).await;
        gAppScheduleManager
            .reconcile_from_service_hb(device_id, "[]")
            .await;
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

async fn process_message(
    _spvr_ctx: Arc<Mutex<SpvrContext>>,
    spvr_conn: Arc<Mutex<SpvrServiceConn>>,
    msg: Message,
    who: SocketAddr,
) -> ControlFlow<(), ()> {
    //tracing::info!("IN --> {}:{}", who.ip(), who.port());
    match msg {
        Message::Text(data) => {
            tracing::info!("** receive message: {}", data);
            return ControlFlow::Continue(());
        }
        Message::Binary(data) => {
            return if spvr_conn
                .lock()
                .await
                .process_message(who.ip().to_string(), data)
                .await
            {
                ControlFlow::Continue(())
            } else {
                ControlFlow::Break(())
            }
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
