use crate::author_handler::{
    handle_log_out, handle_me, handle_ping, handle_query_authors, handle_verify_author,
};
use crate::authorization_handler::{
    handle_create_new_authorization, handle_create_new_deploy_authorization,
    handle_gopico_client_report, handle_gopico_create_new_deploy_authorization,
    handle_gopico_query_authorizations,
    handle_gopico_query_deploy_authorization_by_id, handle_gopico_revoke_authorization,
    handle_gopico_update_authorization, handle_gopico_verify_online,
    handle_query_authorization_by_id, handle_query_authorization_by_name,
    handle_query_authorization_like_name, handle_query_authorizations,
    handle_query_deploy_authorization_by_id, handle_update_authorization,
    handle_verify_appkey_secret, handle_verify_license,
};
use crate::filter::{
    author_admin_filter, author_auth_id_filter, author_login_token_filter, author_page_size_filter,
};
use axum::routing::{get, get_service, post};
use axum::{Router, middleware};
use axum_server::tls_rustls::RustlsConfig;
use std::net::SocketAddr;
use std::path::{Path, PathBuf};
use tower_http::services::{ServeDir, ServeFile};
pub struct AuthorServer {
    pub port: u16,
}

impl AuthorServer {
    pub fn new(port: u16) -> AuthorServer {
        AuthorServer { port }
    }

    pub async fn start(&self) {
        let current_dir = std::env::current_exe().unwrap();
        let current_dir = current_dir.parent().unwrap();

        let web_dir = current_dir.join("web_auth");
        tracing::info!("assets_dir: {:?}", &web_dir);

        let (cp, kp) = tls_cert_paths(current_dir);
        tracing::info!("cp: {:?}", &cp);
        tracing::info!("cp: {:?}", &kp);

        let config = match load_tls_config(&cp, &kp).await {
            Ok(config) => config,
            Err(e) => {
                tracing::error!("could not load HTTPS certificate or private key: {}", e);
                return;
            }
        };

        let router = build_router(web_dir);

        tracing::info!("https.listening on {}", self.port);

        let addr = SocketAddr::from(([0, 0, 0, 0], self.port));
        axum_server::bind_rustls(addr, config)
            .serve(router.into_make_service_with_connect_info::<SocketAddr>())
            .await
            .unwrap();
    }
}

fn tls_cert_paths(current_dir: &Path) -> (PathBuf, PathBuf) {
    (
        current_dir.join("certs").join("cert.pem"),
        current_dir.join("certs").join("key.pem"),
    )
}

async fn load_tls_config(
    cert_path: &Path,
    key_path: &Path,
) -> Result<RustlsConfig, std::io::Error> {
    if !cert_path.is_file() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::NotFound,
            format!("certificate file not found: {}", cert_path.display()),
        ));
    }
    if !key_path.is_file() {
        return Err(std::io::Error::new(
            std::io::ErrorKind::NotFound,
            format!("private key file not found: {}", key_path.display()),
        ));
    }
    RustlsConfig::from_pem_file(cert_path, key_path).await
}

pub fn build_router(web_dir: PathBuf) -> Router {
    let static_dir = ServeDir::new(web_dir.clone())
        .not_found_service(ServeFile::new(web_dir.join("index.html")));

    Router::new()
        .fallback_service(get_service(static_dir).handle_error(|_| async move {
            (
                axum::http::StatusCode::INTERNAL_SERVER_ERROR,
                "Static file error",
            )
        }))
        .route("/api/v1/ping", get(handle_ping))
        .route("/api/v1/verify/author", post(handle_verify_author))
        .route(
            "/api/v1/log_out",
            post(handle_log_out).layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/me",
            get(handle_me).layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/query/authors",
            get(handle_query_authors)
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/create/new/authorization",
            post(handle_create_new_authorization)
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/create/new/deploy/authorization",
            post(handle_create_new_deploy_authorization)
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/query/authorization/by/id",
            get(handle_query_authorization_by_id)
                .layer(middleware::from_fn(author_auth_id_filter::filter))
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/query/deploy/authorization/by/id",
            get(handle_query_deploy_authorization_by_id)
                .layer(middleware::from_fn(author_auth_id_filter::filter))
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/query/authorization/by/name",
            get(handle_query_authorization_by_name)
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/query/authorization/like/name",
            get(handle_query_authorization_like_name)
                .layer(middleware::from_fn(author_page_size_filter::filter))
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/query/authorizations",
            get(handle_query_authorizations)
                .layer(middleware::from_fn(author_page_size_filter::filter))
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/verify/appkey/secret",
            post(handle_verify_appkey_secret)
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route("/api/v1/verify/license", post(handle_verify_license))
        .route(
            "/api/v1/update/authorization",
            post(handle_update_authorization)
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        // GoPico-specific routes
        .route(
            "/api/v1/gopico/create/new/deploy/authorization",
            post(handle_gopico_create_new_deploy_authorization)
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/gopico/update/authorization",
            post(handle_gopico_update_authorization)
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/gopico/query/authorizations",
            get(handle_gopico_query_authorizations)
                .layer(middleware::from_fn(author_page_size_filter::filter))
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/gopico/query/deploy/authorization/by/id",
            get(handle_gopico_query_deploy_authorization_by_id)
                .layer(middleware::from_fn(author_auth_id_filter::filter))
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/gopico/revoke/authorization",
            post(handle_gopico_revoke_authorization)
                .layer(middleware::from_fn(author_admin_filter::filter))
                .layer(middleware::from_fn(author_login_token_filter::filter)),
        )
        .route(
            "/api/v1/gopico/verify/online",
            post(handle_gopico_verify_online),
        )
        .route("/api/v1/gopico/report", post(handle_gopico_client_report))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::author::AuthorRole;
    use crate::author_claims::{
        AuthorClaims, blacklist_test_guard, clear_blacklist_for_test, init_jwt_secret,
    };
    use crate::gLicenseSigner;
    use axum::body::{Body, to_bytes};
    use axum::http::{Request, StatusCode};
    use gr_auth_mgr::auth_license::{AuthLicense, LicenseSigner};
    use serde_json::Value;
    use tower::ServiceExt;

    fn test_router() -> Router {
        build_router(std::env::temp_dir().join("gr_auth_server_router_tests"))
    }

    fn init_test_secret() {
        assert!(init_jwt_secret(
            "test-secret-must-be-at-least-32-bytes".to_string()
        ));
    }

    fn token_for(role: AuthorRole) -> String {
        AuthorClaims::new("TestUser".to_string(), role, 3600)
            .generate_token()
            .expect("token should encode")
    }

    #[test]
    fn builds_tls_cert_paths_from_executable_directory() {
        let base = PathBuf::from("D:/gr_auth_server");
        let (cert, key) = tls_cert_paths(&base);

        assert_eq!(cert, base.join("certs").join("cert.pem"));
        assert_eq!(key, base.join("certs").join("key.pem"));
    }

    #[tokio::test]
    async fn load_tls_config_reports_missing_certificate_before_binding() {
        let base = std::env::temp_dir().join("gr_auth_missing_tls_test");
        let cert = base.join("cert.pem");
        let key = base.join("key.pem");

        let err = load_tls_config(&cert, &key).await.unwrap_err();

        assert_eq!(err.kind(), std::io::ErrorKind::NotFound);
        assert!(err.to_string().contains("certificate file not found"));
    }

    async fn request_status(router: Router, request: Request<Body>) -> StatusCode {
        router
            .oneshot(request)
            .await
            .expect("request should complete")
            .status()
    }

    #[tokio::test]
    async fn ping_route_returns_ok() {
        let response = test_router()
            .oneshot(
                Request::builder()
                    .uri("/api/v1/ping")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .expect("request should complete");

        assert_eq!(response.status(), StatusCode::OK);

        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let value: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(value["code"], 200);
        assert_eq!(value["data"], "Pong");
    }

    #[tokio::test]
    async fn me_rejects_missing_token() {
        let status = request_status(
            test_router(),
            Request::builder()
                .uri("/api/v1/me")
                .body(Body::empty())
                .unwrap(),
        )
        .await;

        assert_eq!(status, StatusCode::UNAUTHORIZED);
    }

    #[tokio::test]
    async fn login_rejects_empty_body_before_database_access() {
        let status = request_status(
            test_router(),
            Request::builder()
                .method("POST")
                .uri("/api/v1/verify/author")
                .body(Body::empty())
                .unwrap(),
        )
        .await;

        assert_eq!(status, StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn login_rejects_invalid_json_before_database_access() {
        let status = request_status(
            test_router(),
            Request::builder()
                .method("POST")
                .uri("/api/v1/verify/author")
                .header("content-type", "application/json")
                .body(Body::from("{invalid-json"))
                .unwrap(),
        )
        .await;

        assert_eq!(status, StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn login_rejects_missing_fields_before_database_access() {
        let status = request_status(
            test_router(),
            Request::builder()
                .method("POST")
                .uri("/api/v1/verify/author")
                .header("content-type", "application/json")
                .body(Body::from(r#"{"author_name":"Admin"}"#))
                .unwrap(),
        )
        .await;

        assert_eq!(status, StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn me_rejects_invalid_token() {
        let status = request_status(
            test_router(),
            Request::builder()
                .uri("/api/v1/me")
                .header("Authorization", "invalid-token")
                .body(Body::empty())
                .unwrap(),
        )
        .await;

        assert_eq!(status, StatusCode::UNAUTHORIZED);
    }

    #[tokio::test]
    async fn me_accepts_valid_token_and_returns_role() {
        init_test_secret();
        let response = test_router()
            .oneshot(
                Request::builder()
                    .uri("/api/v1/me")
                    .header("Authorization", token_for(AuthorRole::Admin))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .expect("request should complete");

        assert_eq!(response.status(), StatusCode::OK);

        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let value: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(value["code"], 200);
        assert_eq!(value["data"]["name"], "TestUser");
        assert_eq!(value["data"]["role"], "admin");
    }

    #[tokio::test]
    async fn visitor_token_cannot_access_admin_route() {
        init_test_secret();
        let status = request_status(
            test_router(),
            Request::builder()
                .uri("/api/v1/query/authors")
                .header("Authorization", token_for(AuthorRole::Visitor))
                .body(Body::empty())
                .unwrap(),
        )
        .await;

        assert_eq!(status, StatusCode::FORBIDDEN);
    }

    #[tokio::test]
    async fn logout_invalidates_current_token_only() {
        // Serialize with other blacklist-mutating tests and start from a clean state.
        let _guard = blacklist_test_guard();
        init_test_secret();
        clear_blacklist_for_test();

        let first = token_for(AuthorRole::Admin);
        let second = token_for(AuthorRole::Admin);

        let logout_status = request_status(
            test_router(),
            Request::builder()
                .method("POST")
                .uri("/api/v1/log_out")
                .header("Authorization", &first)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
        assert_eq!(logout_status, StatusCode::OK);

        let first_status = request_status(
            test_router(),
            Request::builder()
                .uri("/api/v1/me")
                .header("Authorization", first)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
        assert_eq!(first_status, StatusCode::UNAUTHORIZED);

        let second_status = request_status(
            test_router(),
            Request::builder()
                .uri("/api/v1/me")
                .header("Authorization", second)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
        assert_eq!(second_status, StatusCode::OK);
    }

    #[tokio::test]
    async fn create_authorization_rejects_invalid_body_before_database_access() {
        init_test_secret();
        let invalid_body = r#"{
            "name": "customer-a",
            "machine_code": "machine-a",
            "days": 0,
            "max_streams": 4,
            "role": 1
        }"#;

        let status = request_status(
            test_router(),
            Request::builder()
                .method("POST")
                .uri("/api/v1/create/new/authorization")
                .header("Authorization", token_for(AuthorRole::Admin))
                .header("content-type", "application/json")
                .body(Body::from(invalid_body))
                .unwrap(),
        )
        .await;

        assert_eq!(status, StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn update_authorization_rejects_invalid_body_before_database_access() {
        init_test_secret();
        let invalid_body = r#"{
            "auth_id": "auth-id",
            "days": 30,
            "max_streams": 4,
            "role": 4
        }"#;

        let status = request_status(
            test_router(),
            Request::builder()
                .method("POST")
                .uri("/api/v1/update/authorization")
                .header("Authorization", token_for(AuthorRole::Admin))
                .header("content-type", "application/json")
                .body(Body::from(invalid_body))
                .unwrap(),
        )
        .await;

        assert_eq!(status, StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn gopico_create_verify_online_and_revoke_flow() {
        use crate::authorization_manager::clear_authorization_memory_store;
        use std::sync::Mutex;

        // Serialize against other tests that mutate signer / memory store.
        static LOCK: Mutex<()> = Mutex::new(());
        let _guard = LOCK.lock().unwrap();
        clear_authorization_memory_store();
        init_test_secret();

        let (priv_key, _pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        *gLicenseSigner.lock().await = Some(signer);

        let router = test_router();
        let create_body = r#"{
            "name": "gopico-e2e-customer",
            "machine_code": "mc-gopico-e2e",
            "days": 30,
            "max_devices": 40,
            "role": 1
        }"#;

        let create_resp = router
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/gopico/create/new/deploy/authorization")
                    .header("Authorization", token_for(AuthorRole::Admin))
                    .header("content-type", "application/json")
                    .body(Body::from(create_body))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(create_resp.status(), StatusCode::OK);
        let create_bytes = to_bytes(create_resp.into_body(), usize::MAX).await.unwrap();
        let create_json: Value = serde_json::from_slice(&create_bytes).unwrap();
        assert_eq!(create_json["code"], 200);
        let deploy = create_json["data"].as_str().expect("deploy string");
        assert!(deploy.contains('.'));

        // Online verify — valid.
        let verify_body = format!(
            r#"{{"data":"{}","machine_code":"mc-gopico-e2e"}}"#,
            deploy
        );
        let verify_resp = router
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/gopico/verify/online")
                    .header("content-type", "application/json")
                    .body(Body::from(verify_body.clone()))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(verify_resp.status(), StatusCode::OK);
        let verify_bytes = to_bytes(verify_resp.into_body(), usize::MAX).await.unwrap();
        let verify_json: Value = serde_json::from_slice(&verify_bytes).unwrap();
        assert_eq!(verify_json["code"], 200);
        assert_eq!(verify_json["data"]["valid"], true);
        assert_eq!(verify_json["data"]["max_devices"], 40);
        assert_eq!(verify_json["data"]["product"], "gopico");
        assert_eq!(verify_json["data"]["revoked"], false);
        let auth_id = verify_json["data"]["auth_id"].as_str().unwrap().to_string();

        // List gopico authorizations.
        let list_resp = router
            .clone()
            .oneshot(
                Request::builder()
                    .uri("/api/v1/gopico/query/authorizations?page=1&page_size=20")
                    .header("Authorization", token_for(AuthorRole::Admin))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(list_resp.status(), StatusCode::OK);
        let list_bytes = to_bytes(list_resp.into_body(), usize::MAX).await.unwrap();
        let list_json: Value = serde_json::from_slice(&list_bytes).unwrap();
        let items = list_json["data"].as_array().unwrap();
        assert!(items.iter().any(|i| i["auth_id"] == auth_id));
        assert!(items.iter().all(|i| i["product"] == "gopico"));

        // Revoke.
        let revoke_body = format!(r#"{{"auth_id":"{auth_id}"}}"#);
        let revoke_resp = router
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/gopico/revoke/authorization")
                    .header("Authorization", token_for(AuthorRole::Admin))
                    .header("content-type", "application/json")
                    .body(Body::from(revoke_body))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(revoke_resp.status(), StatusCode::OK);

        // Online verify — revoked.
        let verify2 = router
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/gopico/verify/online")
                    .header("content-type", "application/json")
                    .body(Body::from(verify_body))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(verify2.status(), StatusCode::OK);
        let verify2_bytes = to_bytes(verify2.into_body(), usize::MAX).await.unwrap();
        let verify2_json: Value = serde_json::from_slice(&verify2_bytes).unwrap();
        assert_eq!(verify2_json["data"]["valid"], false);
        assert_eq!(verify2_json["data"]["revoked"], true);
        assert_eq!(verify2_json["data"]["reason"], "revoked");

        clear_authorization_memory_store();
    }

    #[tokio::test]
    async fn gopico_client_report_records_status() {
        use crate::authorization_manager::clear_authorization_memory_store;
        use std::sync::Mutex;

        // Serialize against other tests that mutate signer / memory store.
        static LOCK: Mutex<()> = Mutex::new(());
        let _guard = LOCK.lock().unwrap();
        clear_authorization_memory_store();
        init_test_secret();

        let (priv_key, _pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        *gLicenseSigner.lock().await = Some(signer);

        let router = test_router();
        let create_body = r#"{
            "name": "gopico-report-customer",
            "machine_code": "mc-report-e2e",
            "days": 30,
            "max_devices": 10,
            "role": 1
        }"#;
        let create_resp = router
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/gopico/create/new/deploy/authorization")
                    .header("Authorization", token_for(AuthorRole::Admin))
                    .header("content-type", "application/json")
                    .body(Body::from(create_body))
                    .unwrap(),
            )
            .await
            .unwrap();
        let create_bytes = to_bytes(create_resp.into_body(), usize::MAX).await.unwrap();
        let create_json: Value = serde_json::from_slice(&create_bytes).unwrap();
        let deploy = create_json["data"].as_str().expect("deploy string");

        // Client reports its status — accepted.
        let report_body = format!(
            r#"{{"data":"{deploy}","machine_code":"mc-report-e2e",
                "client_version":"2.9.0","client_status":"valid",
                "os":"windows","device_count":3}}"#
        );
        let report_resp = router
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/gopico/report")
                    .header("content-type", "application/json")
                    .body(Body::from(report_body))
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(report_resp.status(), StatusCode::OK);
        let report_bytes = to_bytes(report_resp.into_body(), usize::MAX).await.unwrap();
        let report_json: Value = serde_json::from_slice(&report_bytes).unwrap();
        assert_eq!(report_json["code"], 200);
        assert_eq!(report_json["data"]["accepted"], true);

        // Report is visible in the authorization list.
        let list_resp = router
            .clone()
            .oneshot(
                Request::builder()
                    .uri("/api/v1/gopico/query/authorizations?page=1&page_size=20")
                    .header("Authorization", token_for(AuthorRole::Admin))
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        let list_bytes = to_bytes(list_resp.into_body(), usize::MAX).await.unwrap();
        let list_json: Value = serde_json::from_slice(&list_bytes).unwrap();
        let items = list_json["data"].as_array().unwrap();
        let item = items
            .iter()
            .find(|i| i["auth_name"] == "gopico-report-customer")
            .expect("reported authorization in list");
        assert_eq!(item["client_version"], "2.9.0");
        assert_eq!(item["client_status"], "valid");
        assert_eq!(item["client_os"], "windows");
        assert_eq!(item["client_device_count"], 3);
        assert!(item["client_reported_at_ms"].as_i64().unwrap() > 0);

        // Wrong machine code — rejected, previous report kept.
        let bad_body = format!(
            r#"{{"data":"{deploy}","machine_code":"mc-other-machine",
                "client_version":"9.9.9","client_status":"valid"}}"#
        );
        let bad_resp = router
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/gopico/report")
                    .header("content-type", "application/json")
                    .body(Body::from(bad_body))
                    .unwrap(),
            )
            .await
            .unwrap();
        let bad_bytes = to_bytes(bad_resp.into_body(), usize::MAX).await.unwrap();
        let bad_json: Value = serde_json::from_slice(&bad_bytes).unwrap();
        assert_eq!(bad_json["data"]["accepted"], false);
        assert_eq!(bad_json["data"]["reason"], "machine_mismatch");

        // Unknown auth_id (validly signed, but not in DB) — not_found.
        let ghost = AuthLicense {
            auth_id: "ghost-auth".to_string(),
            auth_name: "ghost".to_string(),
            machine_code: "mc-ghost".to_string(),
            max_streams: 1,
            days: 1,
            role: 1,
            created_at_ms: 0,
            expires_at_ms: i64::MAX,
            appkey: "k".to_string(),
            app_secret: "s".to_string(),
            username: "u".to_string(),
            password: "p".to_string(),
            product: "gopico".to_string(),
        };
        let ghost_deploy = gLicenseSigner
            .lock()
            .await
            .as_ref()
            .unwrap()
            .sign(&ghost)
            .unwrap()
            .to_deploy_string()
            .unwrap();
        let ghost_body = format!(
            r#"{{"data":"{ghost_deploy}","machine_code":"mc-ghost",
                "client_version":"1.0.0","client_status":"valid"}}"#
        );
        let ghost_resp = router
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/gopico/report")
                    .header("content-type", "application/json")
                    .body(Body::from(ghost_body))
                    .unwrap(),
            )
            .await
            .unwrap();
        let ghost_bytes = to_bytes(ghost_resp.into_body(), usize::MAX).await.unwrap();
        let ghost_json: Value = serde_json::from_slice(&ghost_bytes).unwrap();
        assert_eq!(ghost_json["data"]["accepted"], false);
        assert_eq!(ghost_json["data"]["reason"], "not_found");

        clear_authorization_memory_store();
    }

    #[tokio::test]
    async fn verify_license_accepts_valid_signed_license() {
        let (priv_key, _pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        *gLicenseSigner.lock().await = Some(signer);

        let license = AuthLicense {
            auth_id: "auth-1".to_string(),
            auth_name: "name".to_string(),
            machine_code: "mc-1".to_string(),
            max_streams: 4,
            days: 30,
            role: 1,
            created_at_ms: 0,
            expires_at_ms: i64::MAX,
            appkey: "key".to_string(),
            app_secret: "secret".to_string(),
            username: "user".to_string(),
            password: "pass".to_string(),
            product: "cms".to_string(),
        };
        let signed = gLicenseSigner
            .lock()
            .await
            .as_ref()
            .unwrap()
            .sign(&license)
            .unwrap();
        let deploy = signed.to_deploy_string().unwrap();

        let response = test_router()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/api/v1/verify/license")
                    .header("content-type", "application/json")
                    .body(Body::from(format!(r#"{{"data":"{}"}}"#, deploy)))
                    .unwrap(),
            )
            .await
            .expect("request should complete");

        assert_eq!(response.status(), StatusCode::OK);
        let body = to_bytes(response.into_body(), usize::MAX).await.unwrap();
        let value: Value = serde_json::from_slice(&body).unwrap();
        assert_eq!(value["code"], 200);
        assert_eq!(value["data"], true);
    }
}
