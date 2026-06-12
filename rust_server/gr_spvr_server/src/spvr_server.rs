use crate::device::spvr_device_handler::{append_used_time, handle_create_new_device, handle_query_devices, query_device_by_id, update_random_password, update_safety_password, verify_device_info};
use crate::filter::spvr_appkey_filter::filter as spvr_appkey_filter;
use crate::filter::spvr_statistics_filter::filter as spvr_statistics_filter;
use crate::filter::spvr_timer_filter::filter as spvr_timer_filter;
use crate::spvr_context::SpvrContext;
use crate::{gSpvrContext, gSpvrSettings};
use axum::extract::{DefaultBodyLimit, State};
use axum::routing::{any, get, get_service, post};
use axum::{Json, Router};
use axum_server::tls_rustls::RustlsConfig;
use gr_base::{get_current_timestamp, RespMessage};
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;
use tower_http::services::{ServeDir, ServeFile};

use crate::auth::spvr_auth_router::make_auth_router;
use crate::device::spvr_device_router::make_device_router;
use crate::net_client::spvr_client_ws_handler;
use crate::spvr_router::make_spvr_router;
use axum::body::Body;
use axum::{
    http::Request,
    middleware::{self, Next},
    response::Response,
};
use tower_http::cors::CorsLayer;
use crate::net_client::spvr_client_router::make_client_router;
use crate::net_panel::spvr_panel_router::make_panel_router;
use crate::net_panel::spvr_panel_ws_handler;
use crate::net_cm::spvr_cm_ws_handler;
use crate::stream::spvr_stream_router::make_stream_router;
use crate::user::spvr_user_router::make_user_router;
use crate::event::spvr_event_router::make_event_router;
use crate::filter::spvr_appkey_filter;
use crate::relay::{relay_device_handler, relay_room_handler};
use crate::relay::relay_server::RelayServer;
use crate::update::update_router::make_update_router;
use crate::record::spvr_record_router::make_record_router;
use crate::user_device::spvr_user_device_router::make_user_device_router;

pub struct SpvrServer {
    pub host: String,
    pub port: u16,
}

async fn log_mw(req: Request<Body>, next: Next) -> Response {
    //println!("--> hit {}", req.uri().path());
    let mut res = next.run(req).await;
    res.headers_mut()
        .insert("x-logged", "true".parse().unwrap());
    res
}

impl SpvrServer {
    pub fn new(host: String, port: u16) -> Self {
        SpvrServer {
            host,
            port,
        }
    }

    pub async fn start(&self) {
        let current_dir = std::env::current_exe().unwrap();
        let current_dir = current_dir.parent().unwrap();

        let web_spvr_dir = current_dir.join("web");
        tracing::info!("assets_dir: {:?}", &web_spvr_dir);

        // configure certificate and private key used by https
        let cp = current_dir.to_string_lossy().to_string() + "/" + gSpvrSettings.lock().await.ssl_cert.as_str();//.join("certs").join("cert.pem");
        let kp = current_dir.to_string_lossy().to_string() + "/" + gSpvrSettings.lock().await.ssl_key.as_str();//.join("certs").join("key.pem");
        tracing::info!("cp: {:?}", &cp);
        tracing::info!("cp: {:?}", &kp);

        let config = RustlsConfig::from_pem_file(
            current_dir.join("certs").join("cert.pem"),
            current_dir.join("certs").join("key.pem"),
        ).await;

        if let Err(e) = config {
            tracing::error!("==> {}", e);
            return;
        }
        let config = config.unwrap();
        let context = gSpvrContext.clone();

        let cors = CorsLayer::permissive(); // 允许所有跨域请求

        let static_dir = ServeDir::new(web_spvr_dir.clone())
            .not_found_service(ServeFile::new(web_spvr_dir.join("index.html")));

        let router = Router::new()
            .fallback_service(get_service(static_dir).handle_error(|_| async move {
                (
                    axum::http::StatusCode::INTERNAL_SERVER_ERROR,
                    "Static file error"
                )
            }))
            // .nest_service("/web",
            //     get_service(ServeDir::new(web_spvr_dir).append_index_html_on_directories(true))
            // )
            .nest_service("/uploads",
                get_service(ServeDir::new(current_dir.join("uploads")).append_index_html_on_directories(true))
            )
            //.fallback_service(ServeDir::new(web_spvr_dir).append_index_html_on_directories(true))
            // device
            .nest("/api/v1/device/control", make_device_router(context.clone()))

            // authorization
            .nest("/api/v1/auth/control", make_auth_router(context.clone()))

            // spvr
            .nest("/api/v1/spvr/control", make_spvr_router(context.clone()))

            // user
            .nest("/api/v1/user/control", make_user_router(context.clone()))

            // stream
            .nest("/api/v1/stream/control", make_stream_router(context.clone()))

            // connected panel
            .nest("/api/v1/panel/control", make_panel_router(context.clone()))

            // connected client
            .nest("/api/v1/client/control", make_client_router(context.clone()))

            // event
            .nest("/api/v1/event/control", make_event_router(context.clone()))

            // user-device
            .nest("/api/v1/user_device/control", make_user_device_router(context.clone()))

            // update
            .nest("/api/v1/update", make_update_router(context.clone()))

            .nest("/api/v1/record", make_record_router(context.clone()))
            
            .route("/ping", get(SpvrServer::ping))

            // websocket; between servers
            //.route("/inner", any(spvr_relay_ws_handler::inner_handler))

            // websocket; between client and spvr
            .route("/spvr/client",
                   any(spvr_client_ws_handler::client_handler)
                       .layer(middleware::from_fn(spvr_appkey_filter::filter)))

            // websocket; between panel and spvr
            .route("/spvr/panel",
                   any(spvr_panel_ws_handler::panel_handler)
                       .layer(middleware::from_fn(spvr_appkey_filter::filter))
            )

            // websocket; between website and spvr
            .route("/spvr/website",
                   any(spvr_cm_ws_handler::cm_handler)
                       .layer(middleware::from_fn(spvr_appkey_filter::filter))
            )
            
            //
            .layer(middleware::from_fn(spvr_timer_filter))
            .layer(middleware::from_fn(spvr_statistics_filter))

            .layer(DefaultBodyLimit::max(1024 * 1024 * 1024)) // 1GB
            //
            .layer(cors)

            .with_state(context.clone());

        let http_router = router.clone();
        let http_host = self.host.clone();
        let http_port = self.port - 1;
        tracing::info!("http.listening on {}:{}", http_host, http_port);
        tokio::spawn(async move {
            let listener = tokio::net::TcpListener::bind(format!("{}:{}", http_host, http_port)).await.unwrap();
            axum::serve(listener, http_router.into_make_service_with_connect_info::<SocketAddr>()).await.unwrap();
        });

        let addr = SocketAddr::from(([0, 0, 0, 0], self.port));
        tracing::info!("https.listening on {}", addr);
        axum_server::bind_rustls(addr, config)
            .serve(router.into_make_service_with_connect_info::<SocketAddr>())
            .await
            .unwrap();
    }

    pub async fn ping(State(_ctx): State<Arc<Mutex<SpvrContext>>>) -> Json<RespMessage<String>> {
        Json(RespMessage::<String> {
            code: 200,
            message: "ok".to_string(),
            timestamp: get_current_timestamp(),
            data: "Pong".to_string(),
        })
    }

}