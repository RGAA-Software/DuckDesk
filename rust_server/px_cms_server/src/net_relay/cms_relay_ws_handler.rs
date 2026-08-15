use crate::cms_context::CmsContext;
use crate::cms_grpc_ws_client_trait::CmsGrpcWsClientTrait;
use crate::{gCmsInnerConnMgr};
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
use crate::net_px_relay::cms_relay_conn::CmsRelayConn;

pub(crate) async fn inner_handler(
    State(context): State<Arc<Mutex<CmsContext>>>,
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
        handle_socket(context.clone(), params, socket, addr)
    })
}

async fn handle_socket(context: Arc<Mutex<CmsContext>>,
                       params: HashMap<String, String>,
                       socket: WebSocket,
                       who: SocketAddr) {
    let (sender, mut receiver) = socket.split();

    let mut recv_task = tokio::spawn(async move {
        let server_id = params.get("server_id").unwrap_or(&"".to_string()).clone();
        if server_id.is_empty() {
            tracing::error!("cms, server_id is empty!");
            return;
        }

        let server_type = params.get("server_type").unwrap_or(&"".to_string()).clone();
        let server_type = server_type.parse::<i32>().unwrap_or(-1);
        if server_type == -1 {
            tracing::error!("cms, server_type is invalid!");
            return;
        }

        let sender = Arc::new(Mutex::new(sender));
        let cms_conn = CmsRelayConn::new(context.clone(),
                                           sender,
                                           server_id.clone()).await;
        let cms_conn = Arc::new(Mutex::new(cms_conn));
        gCmsInnerConnMgr
            .lock().await
            .add_conn(cms_conn.clone()).await;

        while let Some(Ok(msg)) = receiver.next().await {
            // print message and break if instructed to do so
            if process_message(context.clone(), cms_conn.clone(), msg, who).await.is_break() {
                break;
            }
        }

        // remove
        gCmsInnerConnMgr
            .lock().await
            .remove_conn().await;

    });

    tokio::select! {
            cms_rv = (&mut recv_task) => {
                match cms_rv {
                    Ok(_) => {},
                    Err(e) => {
                        tracing::error!("receive task error: {e:?}")
                    }
                }
                recv_task.abort();
            },
        }
}

async fn process_message(_cms_ctx: Arc<Mutex<CmsContext>>,
                         cms_conn: Arc<Mutex<CmsRelayConn>>,
                         msg: Message,
                         who: SocketAddr)
                         -> ControlFlow<(), ()> {
    //tracing::info!("IN --> {}:{}", who.ip(), who.port());
    match msg {
        Message::Text(data) => {
            return if cms_conn
                .lock().await
                .process_text_message(data).await {
                ControlFlow::Continue(())
            } else {
                ControlFlow::Break(())
            }
        }
        Message::Binary(data) => {
            return if cms_conn
                .lock().await
                .process_bin_message(who.ip().to_string(), data).await {
                ControlFlow::Continue(())
            } else {
                ControlFlow::Break(())
            }
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