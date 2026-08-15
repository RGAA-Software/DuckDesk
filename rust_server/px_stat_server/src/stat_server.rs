use crate::auth::auth_stat_handle::handle_insert_or_update_auth_stat;
use crate::filter;
use crate::filter::stat_visit_filter;
use crate::stat_api_error::StatApiError;
use crate::stat_context::StatContext;
use crate::using::stat_using_handler::handle_open_up;
use axum::routing::{get, get_service, post};
use axum::{middleware, Json, Router};
use axum_server::tls_rustls::RustlsConfig;
use px_base::{ok_resp, RespMessage};
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;
use tower_http::services::{ServeDir, ServeFile};

pub struct StatServer {}

impl StatServer {
    pub fn new() -> Self {
        Self {}
    }

    pub async fn start(&self, context: Arc<Mutex<StatContext>>, port: u16) {
        let current_dir = std::env::current_exe().unwrap();
        let current_dir = current_dir.parent().unwrap();
        let web_cms_dir = current_dir.join("static");
        tracing::info!("assets_dir: {:?}", &web_cms_dir);

        // configure certificate and private key used by https
        let cp = current_dir.join("certs").join("cert.pem");
        let kp = current_dir.join("certs").join("key.pem");
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
        let config = config.unwrap();

        let static_dir = ServeDir::new(web_cms_dir.clone())
            .not_found_service(ServeFile::new(web_cms_dir.join("index.html")));

        let router = Router::new()
            //.fallback_service(ServeDir::new(web_cms_dir).append_index_html_on_directories(true))
            .fallback_service(get_service(static_dir).handle_error(|_| async move {
                (
                    axum::http::StatusCode::INTERNAL_SERVER_ERROR,
                    "Static file error",
                )
            }))
            .route("/api/v1/ping", get(StatServer::handle_ping))
            // auth
            .route(
                "/api/v1/insert/update/auth/stat",
                post(handle_insert_or_update_auth_stat),
            )
            // normal stat
            .route("/api/v1/open/up", post(handle_open_up))
            .layer(middleware::from_fn(stat_visit_filter::filter))
            .layer(middleware::from_fn(filter::stat_statistics_filter::filter))
            .with_state(context.clone());

        let http_host = "0.0.0.0";
        tracing::info!("http.listening on {}:{}", http_host, port);
        let addr = SocketAddr::from(([0, 0, 0, 0], port));
        tracing::info!("https.listening on {}", addr);
        axum_server::bind_rustls(addr, config)
            .serve(router.into_make_service_with_connect_info::<SocketAddr>())
            .await
            .unwrap();
    }

    pub async fn handle_ping() -> Result<Json<RespMessage<String>>, StatApiError> {
        Ok(Json(ok_resp("pong".to_string())))
    }
}
