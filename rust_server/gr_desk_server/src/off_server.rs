use crate::consult::off_consult_handle::{create_new_consult, mark_consult_processed, query_consults};
use crate::issue::off_issue_handle::{create_new_issue, mark_issue_processed, query_issues};
use crate::off_context::OffContext;
use crate::version::off_version_handle::{handle_query_product_version, handle_update_product_version};
use axum::routing::{get, get_service, post};
use axum::{Router};
use axum_server::tls_rustls::RustlsConfig;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;
use tower_http::services::{ServeDir, ServeFile};

pub struct OffServer {

}

impl OffServer {

    pub fn new() -> Self {
        Self {
        }
    }

    pub async fn start(&self, context: Arc<Mutex<OffContext>>) {
        let current_dir = std::env::current_exe().unwrap();
        let current_dir = current_dir.parent().unwrap();
        let web_spvr_dir = current_dir.join("static");
        tracing::info!("assets_dir: {:?}", &web_spvr_dir);

        // configure certificate and private key used by https
        let cp = current_dir.join("certs").join("cert.pem");
        let kp = current_dir.join("certs").join("key.pem");
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

        let static_dir = ServeDir::new(web_spvr_dir.clone())
            .not_found_service(ServeFile::new(web_spvr_dir.join("index.html")));

        let router = Router::new()
            //.fallback_service(ServeDir::new(web_spvr_dir).append_index_html_on_directories(true))
            .fallback_service(get_service(static_dir).handle_error(|_| async move {
                (
                    axum::http::StatusCode::INTERNAL_SERVER_ERROR,
                    "Static file error"
                )
            }))
            .route("/api/v1/create/new/issue", post(create_new_issue))
            .route("/api/v1/query/issues", get(query_issues))
            .route("/api/v1/mark/issue/processed", post(mark_issue_processed))

            .route("/api/v1/create/new/consult", post(create_new_consult))
            .route("/api/v1/query/consults", get(query_consults))
            .route("/api/v1/mark/consult/processed", post(mark_consult_processed))
            .route("/api/v1/update/product/version", post(handle_update_product_version))
            .route("/api/v1/query/product/version", get(handle_query_product_version))
            
            .with_state(context.clone());

        let http_router = router.clone();
        let http_host = "0.0.0.0";
        tracing::info!("http.listening on {}:{}", http_host, 5000);
        tokio::spawn(async move {
            let listener = tokio::net::TcpListener::bind(format!("{}:{}", http_host, 5000)).await;
            if let Err(e) = listener {
                tracing::error!("{}", e);
            } else{
                axum::serve(listener.unwrap(), http_router.into_make_service_with_connect_info::<SocketAddr>()).await.unwrap();
            }
        });

        let addr = SocketAddr::from(([0, 0, 0, 0], 5001));
        tracing::info!("https.listening on {}", addr);
        axum_server::bind_rustls(addr, config)
            .serve(router.into_make_service_with_connect_info::<SocketAddr>())
            .await
            .unwrap();
    }

}