use std::net::SocketAddr;
use std::sync::Arc;
use axum::{middleware, Router};
use axum::routing::{get, get_service, post};
use axum_server::tls_rustls::RustlsConfig;
use tokio::sync::Mutex;
use tower_http::services::{ServeDir, ServeFile};
use crate::author_context::AuthorContext;
use crate::author_handler::{handle_ping, handle_query_authors, handle_verify_author, handle_log_out, handle_me};
use crate::authorization_handler::{handle_create_new_authorization, handle_create_new_deploy_authorization, handle_query_authorization_by_id, handle_query_authorization_by_name, handle_query_authorization_like_name, handle_query_authorizations, handle_query_deploy_authorization_by_id, handle_update_authorization, handle_verify_appkey_secret};
use crate::filter::{author_admin_filter, author_auth_id_filter, author_page_size_filter, author_login_token_filter};
pub struct AuthorServer {
    pub port: u16,
    pub context: Arc<Mutex<AuthorContext>>,
}

impl AuthorServer {
    pub fn new(port: u16, context: Arc<Mutex<AuthorContext>>) -> AuthorServer {
        AuthorServer {
            port,
            context,
        }
    }

    pub async fn start(&self) {
        let current_dir = std::env::current_exe().unwrap();
        let current_dir = current_dir.parent().unwrap();

        let web_dir = current_dir.join("web_auth");
        tracing::info!("assets_dir: {:?}", &web_dir);

        // certs config
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

        let static_dir = ServeDir::new(web_dir.clone())
            .not_found_service(ServeFile::new(web_dir.join("index.html")));

        let router = Router::new()
            .fallback_service(get_service(static_dir).handle_error(|_| async move {
                (
                    axum::http::StatusCode::INTERNAL_SERVER_ERROR,
                    "Static file error"
                )
            }))
            .route("/api/v1/ping", get(handle_ping))
            .route("/api/v1/verify/author",
                   post(handle_verify_author)
            )

            .route("/api/v1/log_out",
                   post(handle_log_out)
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/me",
                   get(handle_me)
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/query/authors",
                   get(handle_query_authors)
                       .layer(middleware::from_fn(author_admin_filter::filter))
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/create/new/authorization",
                   post(handle_create_new_authorization)
                       .layer(middleware::from_fn(author_admin_filter::filter))
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/create/new/deploy/authorization",
                   post(handle_create_new_deploy_authorization)
                       .layer(middleware::from_fn(author_admin_filter::filter))
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/query/authorization/by/id",
                get(handle_query_authorization_by_id)
                    .layer(middleware::from_fn(author_auth_id_filter::filter))
                    .layer(middleware::from_fn(author_admin_filter::filter))
                    .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/query/deploy/authorization/by/id",
                   get(handle_query_deploy_authorization_by_id)
                       .layer(middleware::from_fn(author_auth_id_filter::filter))
                       .layer(middleware::from_fn(author_admin_filter::filter))
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/query/authorization/by/name",
                   get(handle_query_authorization_by_name)
                       .layer(middleware::from_fn(author_admin_filter::filter))
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/query/authorization/like/name",
                   get(handle_query_authorization_like_name)
                       .layer(middleware::from_fn(author_page_size_filter::filter))
                       .layer(middleware::from_fn(author_admin_filter::filter))
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/query/authorizations",
                   get(handle_query_authorizations)
                       .layer(middleware::from_fn(author_page_size_filter::filter))
                       .layer(middleware::from_fn(author_admin_filter::filter))
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/verify/appkey/secret",
                   post(handle_verify_appkey_secret)
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .route("/api/v1/update/authorization",
                   post(handle_update_authorization)
                       .layer(middleware::from_fn(author_admin_filter::filter))
                       .layer(middleware::from_fn(author_login_token_filter::filter))
            )

            .with_state(self.context.clone());

        tracing::info!("https.listening on {}", self.port);

        let addr = SocketAddr::from(([0, 0, 0, 0], self.port));
        axum_server::bind_rustls(addr, config)
            .serve(router.into_make_service_with_connect_info::<SocketAddr>())
            .await
            .unwrap();
    }
}
