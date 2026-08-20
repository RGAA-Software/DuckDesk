use crate::cms_context::CmsContext;
use crate::filter::cms_statistics_filter::filter as cms_statistics_filter;
use crate::filter::cms_timer_filter::filter as cms_timer_filter;
use crate::filter::cms_ws_token_filter::{
    client_filter as cms_client_token_filter_fn, panel_filter as cms_panel_token_filter_fn,
    website_filter as cms_website_token_filter_fn,
};
use crate::{gCmsContext, gCmsSettings};
use axum::extract::{DefaultBodyLimit, State};
use axum::response::IntoResponse;
use axum::routing::{any, get};
use axum::{Json, Router};
use axum_server::tls_rustls::RustlsConfig;
use px_base::{get_current_timestamp, RespMessage};
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;
use tower_http::services::{ServeDir, ServeFile};

use crate::app_schedule::router::make_app_schedule_router;
use crate::auth::cms_auth_router::make_auth_router;
use crate::cms_router::make_cms_router;
use crate::device::cms_device_router::make_device_router;
use crate::event::cms_event_router::make_event_router;
use crate::live::cms_live_router::make_live_router;
use crate::net_client::cms_client_router::make_client_router;
use crate::net_client::cms_client_ws_handler;
use crate::net_cm::cms_cm_ws_handler;
use crate::net_panel::cms_panel_router::make_panel_router;
use crate::net_panel::cms_panel_ws_handler;
use crate::net_service::cms_service_router::make_service_router;
use crate::net_service::cms_service_ws_handler;
use crate::record::cms_record_router::make_record_router;
use crate::stream::cms_stream_router::make_stream_router;
use crate::update::update_router::make_update_router;
use crate::user::cms_user_router::make_user_router;
use crate::user_device::cms_user_device_router::make_user_device_router;
use crate::wall::cms_wall_router::make_wall_router;
use axum::middleware::{self};
use tower_http::cors::{AllowOrigin, CorsLayer};

pub struct CmsServer {
    pub host: String,
    pub port: u16,
}

impl CmsServer {
    pub fn new(host: String, port: u16) -> Self {
        CmsServer { host, port }
    }

    pub async fn start(&self) {
        let current_dir = std::env::current_exe().unwrap();
        let current_dir = current_dir.parent().unwrap();

        let web_cms_dir = current_dir.join("web");
        tracing::info!("assets_dir: {:?}", &web_cms_dir);

        let ssl_enable = gCmsSettings.lock().await.ssl_enable;
        tracing::info!("ssl_enable: {}", ssl_enable);

        // configure certificate and private key used by https (ssl_enable=false 时跳过,
        // 局域网纯 HTTP 部署允许不部署证书)
        let tls_config = if ssl_enable {
            let cp = current_dir.to_string_lossy().to_string()
                + "/"
                + gCmsSettings.lock().await.ssl_cert.as_str(); //.join("certs").join("cert.pem");
            let kp = current_dir.to_string_lossy().to_string()
                + "/"
                + gCmsSettings.lock().await.ssl_key.as_str(); //.join("certs").join("key.pem");
            tracing::info!("cp: {:?}", &cp);
            tracing::info!("cp: {:?}", &kp);

            let config = RustlsConfig::from_pem_file(
                current_dir.join("certs").join("cert.pem"),
                current_dir.join("certs").join("key.pem"),
            )
            .await;

            if let Err(e) = config {
                tracing::error!("==> {}", e);
                return;
            }
            Some(config.unwrap())
        } else {
            None
        };
        let context = gCmsContext.clone();

        // Default CORS policy: deny cross-origin requests. Browser clients must be served
        // from the same origin; native clients do not use CORS.
        let cors = CorsLayer::new().allow_origin(AllowOrigin::predicate(|_, _| false));

        let index_html_path = web_cms_dir.join("index.html");

        let router = Router::new()
            // Static files served from web/ directory.
            .nest_service("/assets", ServeDir::new(web_cms_dir.join("assets")))
            .route_service(
                "/favicon.ico",
                ServeFile::new(web_cms_dir.join("favicon.ico")),
            )
            // .nest_service("/web",
            //     ServeDir::new(web_cms_dir).append_index_html_on_directories(true),
            // )
            .nest_service(
                "/uploads",
                ServeDir::new(current_dir.join("uploads")).append_index_html_on_directories(true),
            )
            //.fallback_service(ServeDir::new(web_cms_dir).append_index_html_on_directories(true))
            // device
            .nest(
                "/api/v1/device/control",
                make_device_router(context.clone()),
            )
            // authorization
            .nest("/api/v1/auth/control", make_auth_router(context.clone()))
            // cms
            .nest("/api/v1/cms/control", make_cms_router(context.clone()))
            // user
            .nest("/api/v1/user/control", make_user_router(context.clone()))
            // stream
            .nest(
                "/api/v1/stream/control",
                make_stream_router(context.clone()),
            )
            // ZLMediaKit live discovery + CMS-ticketed HLS playback.
            .nest("/api/v1/live/control", make_live_router(context.clone()))
            // Trusted, same-origin signaling for the read-only 3x3 wall.
            .nest("/api/v1/wall/control", make_wall_router(context.clone()))
            // connected panel
            .nest("/api/v1/panel/control", make_panel_router(context.clone()))
            // connected service
            .nest(
                "/api/v1/service/control",
                make_service_router(context.clone()),
            )
            // CMS app schedule (game-hook multi-machine)
            .nest(
                "/api/v1/app/control",
                make_app_schedule_router(context.clone()),
            )
            // connected client
            .nest(
                "/api/v1/client/control",
                make_client_router(context.clone()),
            )
            // event
            .nest("/api/v1/event/control", make_event_router(context.clone()))
            // user-device
            .nest(
                "/api/v1/user_device/control",
                make_user_device_router(context.clone()),
            )
            // update
            .nest("/api/v1/update", make_update_router(context.clone()))
            .nest("/api/v1/record", make_record_router(context.clone()))
            .route("/ping", get(CmsServer::ping))
            // websocket; between servers
            //.route("/inner", any(cms_relay_ws_handler::inner_handler))
            // websocket; between client and cms
            .route(
                "/cms/client",
                any(cms_client_ws_handler::client_handler)
                    .layer(middleware::from_fn(cms_client_token_filter_fn)),
            )
            // websocket; between panel and cms
            .route(
                "/cms/panel",
                any(cms_panel_ws_handler::panel_handler)
                    .layer(middleware::from_fn(cms_panel_token_filter_fn)),
            )
            // websocket; between Pixels windows service and cms
            .route(
                "/cms/service",
                any(cms_service_ws_handler::service_handler)
                    .layer(middleware::from_fn(cms_panel_token_filter_fn)),
            )
            // websocket; between website and cms
            .route(
                "/cms/website",
                any(cms_cm_ws_handler::cm_handler)
                    .layer(middleware::from_fn(cms_website_token_filter_fn)),
            )
            //
            .layer(middleware::from_fn(cms_timer_filter))
            .layer(middleware::from_fn(cms_statistics_filter))
            .layer(DefaultBodyLimit::max(1024 * 1024 * 1024)) // 1GB
            //
            .layer(cors)
            // SPA fallback: any path not matched by API routes or static
            // files returns index.html, so Vue Router (history mode) routes
            // like /devices-list work on direct access or page refresh.
            .fallback(move || {
                let path = index_html_path.clone();
                async move {
                    match tokio::fs::read_to_string(&path).await {
                        Ok(content) => axum::response::Html(content).into_response(),
                        Err(_) => (
                            axum::http::StatusCode::INTERNAL_SERVER_ERROR,
                            "index.html not found",
                        )
                            .into_response(),
                    }
                }
            })
            .with_state(context.clone());

        let addr = SocketAddr::from(([0, 0, 0, 0], self.port));
        match tls_config {
            Some(config) => {
                tracing::info!("https.listening on {}", addr);
                axum_server::bind_rustls(addr, config)
                    .serve(router.into_make_service_with_connect_info::<SocketAddr>())
                    .await
                    .unwrap();
            }
            None => {
                // 局域网纯 HTTP 部署(ssl_enable=false):页面内嵌 http:// 设备内容
                // (panel 录像、render 托管的 web client)不被混合内容拦截
                tracing::info!("http.listening on {} (ssl_enable=false)", addr);
                let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
                axum::serve(
                    listener,
                    router.into_make_service_with_connect_info::<SocketAddr>(),
                )
                .await
                .unwrap();
            }
        }
    }

    pub async fn ping(State(_ctx): State<Arc<Mutex<CmsContext>>>) -> Json<RespMessage<String>> {
        Json(RespMessage::<String> {
            code: 200,
            message: "ok".to_string(),
            timestamp: get_current_timestamp(),
            data: "Pong".to_string(),
        })
    }
}
