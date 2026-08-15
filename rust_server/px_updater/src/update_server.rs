use crate::update_context::UpdateContext;
use crate::update_handle::{
    handle_download_install_package, handle_hello_world, handle_query_update_info,
    handle_upload_update_info,
};
use axum::Router;
use axum::extract::DefaultBodyLimit;
use axum::routing::{get, post};
use axum_server::tls_rustls::RustlsConfig;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;
use tower_http::services::ServeDir;

pub struct UpdateServer {}

impl UpdateServer {
    pub fn new() -> Self {
        Self {}
    }

    pub async fn start(&self, context: Arc<Mutex<UpdateContext>>) {
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

        let router = Router::new()
            .fallback_service(ServeDir::new(web_cms_dir).append_index_html_on_directories(true))
            .route("/hello", get(handle_hello_world))
            .route("/upload_update_info", post(handle_upload_update_info))
            .route("/query_update_info", get(handle_query_update_info))
            .route("/download", get(handle_download_install_package))
            .layer(DefaultBodyLimit::max(1024 * 1024 * 1024)) // 1GB
            .with_state(context.clone());

        let http_router = router.clone();
        let http_host = "0.0.0.0";
        let port = 30699;
        tracing::info!("http.listening on {}:{}", http_host, port);
        tokio::spawn(async move {
            let listener = tokio::net::TcpListener::bind(format!("{}:{}", http_host, port))
                .await
                .unwrap();
            axum::serve(
                listener,
                http_router.into_make_service_with_connect_info::<SocketAddr>(),
            )
            .await
            .unwrap();
        });

        let addr = SocketAddr::from(([0, 0, 0, 0], port + 1));
        tracing::info!("https.listening on {}", addr);
        axum_server::bind_rustls(addr, config)
            .serve(router.into_make_service_with_connect_info::<SocketAddr>())
            .await
            .unwrap();
    }
}
