use crate::console_context::ConsoleContext;
use crate::filter::console_statistics_filter::filter as console_statistics_filter;
use crate::filter::console_timer_filter::filter as console_timer_filter;
use crate::filter::console_ws_token_filter::{
    client_filter as console_client_token_filter_fn, panel_filter as console_panel_token_filter_fn,
    website_filter as console_website_token_filter_fn,
};
use crate::{gConsoleContext, gConsoleSettings};
use axum::extract::{DefaultBodyLimit, State};
use axum::response::IntoResponse;
use axum::routing::{any, get, post};
use axum::{Json, Router};
use axum_server::tls_rustls::RustlsConfig;
use px_base::{get_current_timestamp, RespMessage};
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;
use tower_http::services::{ServeDir, ServeFile};

use crate::app_schedule::router::make_app_schedule_router;
use crate::auth::console_auth_router::make_auth_router;
use crate::console_router::make_console_router;
use crate::device::console_device_router::make_device_router;
use crate::event::console_event_router::make_event_router;
use crate::identity::public_router::make_public_resource_router;
use crate::identity::router::make_admin_identity_router;
use crate::live::console_live_router::make_live_router;
use crate::net_client::console_client_router::make_client_router;
use crate::net_client::console_client_ws_handler;
use crate::net_cm::console_cm_ws_handler;
use crate::net_panel::console_panel_router::make_panel_router;
use crate::net_panel::console_panel_ws_handler;
use crate::net_service::console_service_router::make_service_router;
use crate::net_service::console_service_ws_handler;
use crate::record::console_record_router::make_record_router;
use crate::stream::console_stream_router::make_stream_router;
use crate::update::update_router::make_update_router;
use crate::user::session_router::{make_session_router, make_user_self_router};
use crate::wall::console_wall_router::make_wall_router;
use axum::middleware::{self};
use tower_http::cors::{AllowOrigin, CorsLayer};

// Compatibility routes for clients deployed before the Pixels Console rename.
// Canonical clients use /console/* and /api/v1/console/control.
const LEGACY_CMS_CONTROL_PATH: &str = "/api/v1/cms/control";
const LEGACY_CMS_CLIENT_PATH: &str = "/cms/client";
const LEGACY_CMS_PANEL_PATH: &str = "/cms/panel";
const LEGACY_CMS_SERVICE_PATH: &str = "/cms/service";
const LEGACY_CMS_WEBSITE_PATH: &str = "/cms/website";

pub struct ConsoleServer {
    pub host: String,
    pub port: u16,
}

fn allow_ticket_renewal_origin(origin: &str, path: &str) -> bool {
    path == "/api/v1/connection-tickets/renew"
        && (origin.starts_with("http://") || origin.starts_with("https://"))
}

impl ConsoleServer {
    pub fn new(host: String, port: u16) -> Self {
        ConsoleServer { host, port }
    }

    pub async fn start(&self) {
        let current_dir = std::env::current_exe().unwrap();
        let current_dir = current_dir.parent().unwrap();

        let web_console_dir = current_dir.join("web");
        tracing::info!("assets_dir: {:?}", &web_console_dir);

        let ssl_enable = gConsoleSettings.lock().await.ssl_enable;
        tracing::info!("ssl_enable: {}", ssl_enable);

        // configure certificate and private key used by https (ssl_enable=false 时跳过,
        // 局域网纯 HTTP 部署允许不部署证书)
        let tls_config = if ssl_enable {
            let cp = current_dir.to_string_lossy().to_string()
                + "/"
                + gConsoleSettings.lock().await.ssl_cert.as_str(); //.join("certs").join("cert.pem");
            let kp = current_dir.to_string_lossy().to_string()
                + "/"
                + gConsoleSettings.lock().await.ssl_key.as_str(); //.join("certs").join("key.pem");
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
        let context = gConsoleContext.clone();

        // Only the rotating bearer-capability renewal endpoint is callable by
        // a Web Client hosted on a Render/device origin. It accepts no Console
        // cookies; every other API remains same-origin only.
        let cors = CorsLayer::new()
            .allow_origin(AllowOrigin::predicate(|origin, request| {
                origin
                    .to_str()
                    .is_ok_and(|value| allow_ticket_renewal_origin(value, request.uri.path()))
            }))
            .allow_methods([axum::http::Method::POST])
            .allow_headers([axum::http::header::CONTENT_TYPE]);

        let index_html_path = web_console_dir.join("index.html");

        let router = Router::new()
            // Static files served from web/ directory.
            .nest_service("/assets", ServeDir::new(web_console_dir.join("assets")))
            .route_service(
                "/favicon.ico",
                ServeFile::new(web_console_dir.join("favicon.ico")),
            )
            // .nest_service("/web",
            //     ServeDir::new(web_console_dir).append_index_html_on_directories(true),
            // )
            .nest_service(
                "/uploads",
                ServeDir::new(current_dir.join("uploads")).append_index_html_on_directories(true),
            )
            //.fallback_service(ServeDir::new(web_console_dir).append_index_html_on_directories(true))
            // device
            .nest(
                "/api/v1/device/control",
                make_device_router(context.clone()),
            )
            // authorization
            .nest("/api/v1/auth/control", make_auth_router(context.clone()))
            // console
            .nest("/api/v1/console/control", make_console_router(context.clone()))
            .nest(LEGACY_CMS_CONTROL_PATH, make_console_router(context.clone()))
            // user
            .nest("/api/v1/session", make_session_router(context.clone()))
            .route(
                "/api/v1/connection-tickets/renew",
                post(crate::connection_ticket::handler::renew_connection_ticket),
            )
            .nest("/api/v1/user", make_user_self_router(context.clone()))
            .nest("/api/v1/admin", make_admin_identity_router(context.clone()))
            .nest(
                "/api/v1/public",
                make_public_resource_router(context.clone()),
            )
            // stream
            .nest(
                "/api/v1/stream/control",
                make_stream_router(context.clone()),
            )
            // ZLMediaKit live discovery + Console-ticketed HLS playback.
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
            // Console app schedule (game-hook multi-machine)
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
            // update
            .nest("/api/v1/update", make_update_router(context.clone()))
            .nest("/api/v1/record", make_record_router(context.clone()))
            .route("/ping", get(ConsoleServer::ping))
            // websocket; between servers
            //.route("/inner", any(console_relay_ws_handler::inner_handler))
            // websocket; between client and console
            .route(
                "/console/client",
                any(console_client_ws_handler::client_handler)
                    .layer(middleware::from_fn(console_client_token_filter_fn)),
            )
            // websocket; between panel and console
            .route(
                "/console/panel",
                any(console_panel_ws_handler::panel_handler)
                    .layer(middleware::from_fn(console_panel_token_filter_fn)),
            )
            // websocket; between Pixels windows service and console
            .route(
                "/console/service",
                any(console_service_ws_handler::service_handler)
                    .layer(middleware::from_fn(console_panel_token_filter_fn)),
            )
            // websocket; between website and console
            .route(
                "/console/website",
                any(console_cm_ws_handler::cm_handler)
                    .layer(middleware::from_fn(console_website_token_filter_fn)),
            )
            // One-release compatibility aliases. They execute the same token
            // filters and handlers as the canonical Console routes.
            .route(
                LEGACY_CMS_CLIENT_PATH,
                any(console_client_ws_handler::client_handler)
                    .layer(middleware::from_fn(console_client_token_filter_fn)),
            )
            .route(
                LEGACY_CMS_PANEL_PATH,
                any(console_panel_ws_handler::panel_handler)
                    .layer(middleware::from_fn(console_panel_token_filter_fn)),
            )
            .route(
                LEGACY_CMS_SERVICE_PATH,
                any(console_service_ws_handler::service_handler)
                    .layer(middleware::from_fn(console_panel_token_filter_fn)),
            )
            .route(
                LEGACY_CMS_WEBSITE_PATH,
                any(console_cm_ws_handler::cm_handler)
                    .layer(middleware::from_fn(console_website_token_filter_fn)),
            )
            //
            .layer(middleware::from_fn(console_timer_filter))
            .layer(middleware::from_fn(console_statistics_filter))
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

    pub async fn ping(State(_ctx): State<Arc<Mutex<ConsoleContext>>>) -> Json<RespMessage<String>> {
        Json(RespMessage::<String> {
            code: 200,
            message: "ok".to_string(),
            timestamp: get_current_timestamp(),
            data: "Pong".to_string(),
        })
    }
}

#[cfg(test)]
mod cors_tests {
    use super::allow_ticket_renewal_origin;

    #[test]
    fn cross_origin_is_limited_to_rotating_ticket_renewal() {
        assert!(allow_ticket_renewal_origin(
            "http://device.local:32004",
            "/api/v1/connection-tickets/renew"
        ));
        assert!(!allow_ticket_renewal_origin(
            "http://device.local:32004",
            "/api/v1/user/apps"
        ));
        assert!(!allow_ticket_renewal_origin(
            "null",
            "/api/v1/connection-tickets/renew"
        ));
    }
}
