use crate::cms_context::CmsContext;
use crate::gCmsClientConnMgr;
use crate::net_client::cms_client_conn::CmsClientConn;
use crate::net_client::cms_client_conn_mgr::StreamReservation;
use axum::extract::ws::{Message, WebSocket};
use axum::extract::{ConnectInfo, Extension, Query, State, WebSocketUpgrade};
use axum::response::IntoResponse;
use axum_extra::TypedHeader;
use futures_util::StreamExt;
use std::collections::HashMap;
use std::net::SocketAddr;
use std::ops::ControlFlow;
use std::sync::Arc;
use tokio::sync::Mutex;

pub(crate) async fn client_handler(
    State(context): State<Arc<Mutex<CmsContext>>>,
    query: Query<HashMap<String, String>>,
    ws: WebSocketUpgrade,
    user_agent: Option<TypedHeader<headers::UserAgent>>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    Extension(reservation): Extension<StreamReservation>,
) -> impl IntoResponse {
    let user_agent = if let Some(TypedHeader(user_agent)) = user_agent {
        user_agent.to_string()
    } else {
        String::from("Unknown browser")
    };
    tracing::info!("ws handshake from {}, agent: {}", addr, user_agent);
    for k in query.keys() {
        tracing::debug!("ws query param key: {}", k);
    }
    let params = query.0.clone();
    ws.on_upgrade(move |socket| {
        let reservation = reservation;
        async move { handle_socket(context.clone(), params, socket, addr, reservation).await }
    })
}

async fn handle_socket(
    context: Arc<Mutex<CmsContext>>,
    params: HashMap<String, String>,
    socket: WebSocket,
    who: SocketAddr,
    reservation: StreamReservation,
) {
    let (sender, mut receiver) = socket.split();

    let mut recv_task = tokio::spawn(async move {
        let appkey = params.get("appkey").unwrap();
        let device_id = params.get("device_id").unwrap_or(&"".to_string()).clone();
        let remote_device_id = params
            .get("remote_device_id")
            .unwrap_or(&"".to_string())
            .clone();
        let remote_device_ip = params
            .get("remote_device_ip")
            .unwrap_or(&"".to_string())
            .clone();
        tracing::info!(
            "client ws connected, device id: {}, remote device id: {}, remote device ip: {}",
            device_id,
            remote_device_id,
            remote_device_ip
        );

        if device_id.is_empty() {
            tracing::error!("cms client error, device id is empty!");
            return;
        }

        // make a special id
        let conn_id = format!(
            "{}-{}",
            device_id,
            if remote_device_id.is_empty() {
                if remote_device_ip.is_empty() {
                    px_base::get_current_timestamp().to_string()
                } else {
                    remote_device_ip.clone()
                }
            } else {
                remote_device_id.clone()
            }
        );
        let sender = Arc::new(Mutex::new(sender));
        let conn = CmsClientConn::new(
            context.clone(),
            sender,
            device_id.clone(),
            remote_device_id.clone(),
            remote_device_ip.clone(),
            appkey.clone(),
        )
        .await;
        tracing::info!("cms client connection from {}", conn_id);
        let conn = Arc::new(Mutex::new(conn));
        gCmsClientConnMgr
            .add_conn(conn_id.clone(), conn.clone())
            .await;
        reservation.forget();

        while let Some(Ok(msg)) = receiver.next().await {
            // print message and break if instructed to do so
            if process_message(context.clone(), conn.clone(), msg, who)
                .await
                .is_break()
            {
                break;
            }
        }

        // remove
        gCmsClientConnMgr.remove_conn(conn_id).await;
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
    cms_conn: Arc<Mutex<CmsClientConn>>,
    msg: Message,
    who: SocketAddr,
) -> ControlFlow<(), ()> {
    //tracing::info!("IN --> {}:{}", who.ip(), who.port());
    match msg {
        Message::Text(data) => {
            tracing::info!("receive message: {}", data);
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
