#![allow(dead_code)] // Shared isolated API fixture; suites exercise different capabilities.
use axum::{
    body::{to_bytes, Body},
    extract::ConnectInfo,
    http::{Request, StatusCode},
    Router,
};
use px_console_runtime::{ConsoleRuntime, GuestAdmission, IngressPolicy};
use px_console_store::{
    initialize_administrator, PasswordDigest, Username, WorkspaceKey, WorkspaceVault,
};
use px_pg::{DatabaseConfig, Transport};
use serde_json::{json, Value};
use std::{env, net::SocketAddr, sync::Arc, time::Duration};
use tower::ServiceExt;
use uuid::Uuid;
use zeroize::Zeroizing;
pub const PASSWORD: &str = "synthetic-console-password";
pub const ORIGIN: &str = "https://console.example.test";
pub fn config(role: &str) -> DatabaseConfig {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let mut url =
        url::Url::parse(&env::var(format!("PIXELS_TEST_CONSOLE_{role}_URL")).unwrap()).unwrap();
    assert!(matches!(
        crate::FIXTURE_KIND,
        "api" | "directory" | "node_control"
    ));
    let platform = if cfg!(windows) { "windows" } else { "linux" };
    url.set_path(&format!(
        "/pixels_console_{}_{platform}",
        crate::FIXTURE_KIND
    ));
    DatabaseConfig::parse(url.as_str(), Transport::LocalDevelopment).unwrap()
}
pub fn deployment() -> Uuid {
    env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap()
}
pub fn vault() -> Arc<WorkspaceVault> {
    let id = Uuid::new_v4();
    Arc::new(
        WorkspaceVault::new(
            id,
            vec![WorkspaceKey {
                id,
                bytes: Zeroizing::new([75; 32]),
            }],
        )
        .unwrap(),
    )
}
pub fn policy() -> IngressPolicy {
    IngressPolicy::new(ORIGIN, true, Duration::from_secs(3600), false).unwrap()
}
pub fn guests() -> GuestAdmission {
    GuestAdmission::for_isolated_test(
        deployment(),
        Zeroizing::new([42; 32]),
        true,
        Duration::from_secs(3600),
    )
    .unwrap()
}
pub async fn start() -> ConsoleRuntime {
    static INIT: tokio::sync::OnceCell<()> = tokio::sync::OnceCell::const_new();
    INIT.get_or_init(|| async {
        let hash = px_credentials::hash(PASSWORD).unwrap();
        initialize_administrator(
            &config("OWNER"),
            deployment(),
            &Username::parse("initial-admin").unwrap(),
            &PasswordDigest::parse(hash.to_string()).unwrap(),
        )
        .await
        .unwrap();
    })
    .await;
    ConsoleRuntime::activate(
        &config("RUNTIME"),
        deployment(),
        vault(),
        policy(),
        guests(),
    )
    .await
    .unwrap()
}
pub async fn call(
    router: &Router,
    method: &str,
    path: &str,
    client: &str,
    token: Option<&str>,
    body: Value,
) -> (StatusCode, Value) {
    request(
        router,
        method,
        path,
        client,
        token,
        body,
        Some(ORIGIN),
        false,
    )
    .await
}
#[allow(clippy::too_many_arguments)]
pub async fn request(
    router: &Router,
    method: &str,
    path: &str,
    client: &str,
    token: Option<&str>,
    body: Value,
    origin: Option<&str>,
    forwarded: bool,
) -> (StatusCode, Value) {
    request_internal(
        router,
        method,
        path,
        client,
        token,
        body,
        origin,
        forwarded,
        None,
        "127.0.0.1:14000".parse().unwrap(),
    )
    .await
}

pub async fn resource_call(
    router: &Router,
    method: &str,
    path: &str,
    client: &str,
    token: Option<&str>,
    subject: Option<&str>,
    body: Value,
) -> (StatusCode, Value) {
    request_internal(
        router,
        method,
        path,
        client,
        token,
        body,
        Some(ORIGIN),
        false,
        subject,
        "127.0.0.2:14000".parse().unwrap(),
    )
    .await
}

#[allow(clippy::too_many_arguments)]
async fn request_internal(
    router: &Router,
    method: &str,
    path: &str,
    client: &str,
    token: Option<&str>,
    body: Value,
    origin: Option<&str>,
    forwarded: bool,
    subject: Option<&str>,
    peer: SocketAddr,
) -> (StatusCode, Value) {
    let mut req = Request::builder()
        .method(method)
        .uri(path)
        .header("content-type", "application/json")
        .header("x-pixels-client-type", client);
    if let Some(origin) = origin {
        req = req.header("origin", origin)
    }
    if let Some(token) = token {
        req = req.header("authorization", format!("Bearer {token}"))
    }
    if let Some(subject) = subject {
        req = req.header("x-pixels-subject-kind", subject)
    }
    if forwarded {
        req = req.header("x-forwarded-for", "127.0.0.1")
    }
    let mut req = req.body(Body::from(body.to_string())).unwrap();
    req.extensions_mut().insert(ConnectInfo(peer));
    let response = router.clone().oneshot(req).await.unwrap();
    let status = response.status();
    let bytes = to_bytes(response.into_body(), 65536).await.unwrap();
    let value = if bytes.is_empty() {
        Value::Null
    } else {
        serde_json::from_slice(&bytes)
            .unwrap_or_else(|_| json!({"text":String::from_utf8_lossy(&bytes)}))
    };
    (status, value)
}
pub async fn login(router: &Router, name: &str, password: &str, client: &str) -> String {
    let (status, value) = call(
        router,
        "POST",
        "/api/console/sessions",
        client,
        None,
        json!({"username":name,"password":password}),
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{value}");
    value["token"].as_str().unwrap().into()
}
pub async fn register(router: &Router) -> String {
    let name = Uuid::new_v4().to_string();
    let (status, value) = call(
        router,
        "POST",
        "/api/console/accounts",
        "android",
        None,
        json!({"username":name,"password":PASSWORD}),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED, "{value}");
    name
}
