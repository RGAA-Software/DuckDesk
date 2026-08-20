use crate::app_schedule::gAppScheduleManager;
use crate::cms_context::CmsContext;
use crate::gCmsServiceConnMgr;
use crate::net_service::cms_service_conn::CmsServiceConn;
use axum::extract::ws::{Message, WebSocket};
use axum::extract::{ConnectInfo, Query, State, WebSocketUpgrade};
use axum::response::IntoResponse;
use axum_extra::TypedHeader;
use futures_util::StreamExt;
use std::collections::HashMap;
use std::net::SocketAddr;
use std::ops::ControlFlow;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::Mutex;

/// Wait before reconciling with an empty instance list after a disconnect —
/// ~2-3 HB periods, so a brief TCP blip does not mark live instances stopped.
const DISCONNECT_RECONCILE_DELAY: Duration = Duration::from_secs(15);

pub(crate) async fn service_handler(
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
        // Never log credentials in full — prefix only.
        let shown = if k.contains("token") || k.contains("appkey") || k.contains("secret") {
            format!("{}...", &v[..v.len().min(8)])
        } else {
            v.clone()
        };
        tracing::info!("ws query param {}:{}", k, shown);
    }
    let params = query.0.clone();
    ws.on_upgrade(move |socket| handle_socket(context.clone(), params, socket, addr))
}

async fn handle_socket(
    context: Arc<Mutex<CmsContext>>,
    params: HashMap<String, String>,
    socket: WebSocket,
    who: SocketAddr,
) {
    let (sender, mut receiver) = socket.split();

    let mut recv_task = tokio::spawn(async move {
        let appkey = params.get("appkey").unwrap();
        let device_id = params.get("device_id").unwrap_or(&"".to_string()).clone();
        if device_id.is_empty() {
            tracing::error!("cms service error, device id is empty!");
            return;
        }

        let sender = Arc::new(Mutex::new(sender));
        let service_conn =
            CmsServiceConn::new(context.clone(), sender, device_id.clone(), appkey.clone()).await;
        let cms_conn = Arc::new(Mutex::new(service_conn));
        let epoch = gCmsServiceConnMgr
            .add_conn(device_id.clone(), cms_conn.clone())
            .await;

        while let Some(Ok(msg)) = receiver.next().await {
            // print message and break if instructed to do so
            if process_message(context.clone(), cms_conn.clone(), msg, who)
                .await
                .is_break()
            {
                break;
            }
        }

        // Compare-and-remove so a reconnect's newer connection survives.
        gCmsServiceConnMgr
            .remove_conn(device_id.clone(), &cms_conn)
            .await;
        gAppScheduleManager.mark_device_suspect(&device_id).await;
        // Do NOT reconcile immediately: a TCP blip would permanently mark live
        // instances stopped. Wait a few HB periods; if the device reconnects
        // meanwhile (epoch bumped by add_conn), cancel the reconcile.
        tokio::spawn(async move {
            tokio::time::sleep(DISCONNECT_RECONCILE_DELAY).await;
            if gCmsServiceConnMgr.device_epoch(&device_id).await != Some(epoch) {
                return;
            }
            gAppScheduleManager
                .reconcile_from_service_hb(device_id, "[]")
                .await;
        });
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

async fn process_message(
    _cms_ctx: Arc<Mutex<CmsContext>>,
    cms_conn: Arc<Mutex<CmsServiceConn>>,
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
            return if cms_conn
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
