#[path = "support/runtime_fixture.rs"]
mod fixture;

use axum::{
    body::{to_bytes, Body},
    extract::ConnectInfo,
    http::{Request, StatusCode},
    Router,
};
use serde_json::{json, Value};
use std::{env, net::SocketAddr};
use tower::ServiceExt;
use uuid::Uuid;

const FIXTURE_KIND: &str = "distribution_isolation";
const OFFICIAL_ORIGIN: &str = "https://official-console.example.test";
const CUSTOMER_ORIGIN: &str = "https://customer-console.example.test";

fn database_name(distribution: &str) -> String {
    let platform = if cfg!(windows) { "windows" } else { "linux" };
    format!("pixels_console_distribution_{distribution}_{platform}")
}

async fn call(
    router: &Router,
    method: &str,
    path: &str,
    origin: &str,
    token: Option<&str>,
    body: Value,
) -> (StatusCode, Value) {
    let mut request = Request::builder()
        .method(method)
        .uri(path)
        .header("content-type", "application/json")
        .header("origin", origin)
        .header("x-pixels-client-type", "android");
    if let Some(token) = token {
        request = request.header("authorization", format!("Bearer {token}"));
    }
    let mut request = request.body(Body::from(body.to_string())).unwrap();
    request.extensions_mut().insert(ConnectInfo(
        "127.0.0.7:14000".parse::<SocketAddr>().unwrap(),
    ));
    let response = router.clone().oneshot(request).await.unwrap();
    let status = response.status();
    let response_bytes = to_bytes(response.into_body(), 65_536).await.unwrap();
    let response_body = if response_bytes.is_empty() {
        Value::Null
    } else {
        serde_json::from_slice(&response_bytes).unwrap()
    };
    (status, response_body)
}

async fn register_and_login(router: &Router, origin: &str, username: &str) -> String {
    let (registration_status, registration) = call(
        router,
        "POST",
        "/api/console/accounts",
        origin,
        None,
        json!({"username":username,"password":fixture::PASSWORD}),
    )
    .await;
    assert_eq!(registration_status, StatusCode::CREATED, "{registration}");
    let (login_status, login) = call(
        router,
        "POST",
        "/api/console/sessions",
        origin,
        None,
        json!({"username":username,"password":fixture::PASSWORD}),
    )
    .await;
    assert_eq!(login_status, StatusCode::OK, "{login}");
    login["token"].as_str().unwrap().to_owned()
}

#[tokio::test]
async fn official_and_customer_deployments_isolate_origins_accounts_and_tokens() {
    let official_deployment_id = env::var("PIXELS_DEPLOYMENT_ID")
        .unwrap()
        .parse::<Uuid>()
        .unwrap();
    let customer_deployment_id = env::var("PIXELS_TEST_CUSTOMER_DEPLOYMENT_ID")
        .unwrap()
        .parse::<Uuid>()
        .unwrap();
    assert_ne!(official_deployment_id, customer_deployment_id);

    let official_runtime = fixture::start_isolated_deployment(
        &database_name("official"),
        official_deployment_id,
        OFFICIAL_ORIGIN,
    )
    .await;
    let customer_runtime = fixture::start_isolated_deployment(
        &database_name("customer"),
        customer_deployment_id,
        CUSTOMER_ORIGIN,
    )
    .await;
    let official_router = official_runtime.router();
    let customer_router = customer_runtime.router();
    let shared_username = format!("same-account-{}", Uuid::new_v4());
    let official_token =
        register_and_login(&official_router, OFFICIAL_ORIGIN, &shared_username).await;
    let customer_token =
        register_and_login(&customer_router, CUSTOMER_ORIGIN, &shared_username).await;
    assert_ne!(official_token, customer_token);

    for (router, origin, own_token, foreign_token) in [
        (
            &official_router,
            OFFICIAL_ORIGIN,
            official_token.as_str(),
            customer_token.as_str(),
        ),
        (
            &customer_router,
            CUSTOMER_ORIGIN,
            customer_token.as_str(),
            official_token.as_str(),
        ),
    ] {
        assert_eq!(
            call(
                router,
                "GET",
                "/api/console/applications?limit=100",
                origin,
                Some(own_token),
                Value::Null,
            )
            .await
            .0,
            StatusCode::OK
        );
        assert_eq!(
            call(
                router,
                "GET",
                "/api/console/applications?limit=100",
                origin,
                Some(foreign_token),
                Value::Null,
            )
            .await
            .0,
            StatusCode::FORBIDDEN
        );
    }

    assert_eq!(
        call(
            &customer_router,
            "GET",
            "/api/console/applications?limit=100",
            OFFICIAL_ORIGIN,
            Some(&customer_token),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
}
