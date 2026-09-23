#[path = "support/runtime_fixture.rs"]
mod fixture;

use fixture::{call, login, start, PASSWORD};
use px_console_store::{RelayNodeStore, TokenDigest};
use px_relay_server::{
    config::{ControlPlaneConfig, RelayConfig},
    control,
    server::{router_with_state, RelayServerState},
};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{net::SocketAddr, sync::Arc, time::Duration};
use tokio::net::TcpListener;
use tokio_util::sync::CancellationToken;
use uuid::Uuid;
use zeroize::Zeroizing;

const FIXTURE_KIND: &str = "node_control";

fn digest(secret: &str) -> TokenDigest {
    TokenDigest::from_sha256(Sha256::digest(secret.as_bytes()).into())
}

async fn wait_for_profile<F>(
    relay_nodes: &RelayNodeStore,
    admin: &TokenDigest,
    relay_node_id: Uuid,
    predicate: F,
) -> px_console_store::RelayNodeProfile
where
    F: Fn(&px_console_store::RelayNodeProfile) -> bool,
{
    let mut last_profile = None;
    for _ in 0..80 {
        let relay_node = relay_nodes
            .list_managed(admin, None, 100)
            .await
            .unwrap()
            .into_iter()
            .find(|relay_node| relay_node.id == relay_node_id)
            .unwrap();
        if predicate(&relay_node) {
            return relay_node;
        }
        last_profile = Some(relay_node);
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
    panic!("Relay profile did not reach the expected state: {last_profile:?}");
}

#[tokio::test]
async fn relay_active_control_reports_capacity_converges_draining_and_fails_closed() {
    let runtime = start().await;
    let console_router = runtime.router();
    let admin_secret = login(&console_router, "initial-admin", PASSWORD, "admin_web").await;
    let admin = digest(&admin_secret);
    let relay_name = format!("active-relay-{}", Uuid::new_v4());
    let (create_status, created) = call(
        &console_router,
        "POST",
        "/api/console/managed/relays",
        "admin_web",
        Some(&admin_secret),
        json!({
            "name": relay_name,
            "public_host": "relay-active.example.test",
            "public_port": 4605
        }),
    )
    .await;
    assert_eq!(create_status.as_u16(), 201, "{created}");
    let relay_secret = created["relay_token"].as_str().unwrap().to_owned();
    assert_eq!(relay_secret.len(), 64);
    let relay_node_id = created["relay"]["id"].as_str().unwrap().parse().unwrap();
    let relay_nodes = RelayNodeStore::connect(&fixture::config("RUNTIME"), fixture::deployment())
        .await
        .unwrap();
    let (list_status, listed) = call(
        &console_router,
        "GET",
        "/api/console/managed/relays?limit=100",
        "admin_web",
        Some(&admin_secret),
        Value::Null,
    )
    .await;
    assert_eq!(list_status.as_u16(), 200, "{listed}");
    assert_eq!(listed.as_array().unwrap().len(), 1);
    assert!(listed[0].get("relay_token").is_none());

    let console_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let console_address = console_listener.local_addr().unwrap();
    let console_stop = CancellationToken::new();
    let console_shutdown = console_stop.clone();
    let server_router = console_router.clone();
    let console_server = tokio::spawn(async move {
        axum::serve(
            console_listener,
            server_router.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .with_graceful_shutdown(console_shutdown.cancelled_owned())
        .await
        .unwrap();
    });

    let relay_listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let relay_address = relay_listener.local_addr().unwrap();
    let relay_state = RelayServerState::new(RelayConfig {
        listen: relay_address,
        app_key: b"integration-relay-app-key".to_vec(),
        control_key: b"integration-relay-control-key-32".to_vec(),
        max_connections: 4_096,
        max_rooms: 2_048,
        outbound_queue: 32,
        max_message_bytes: 1024 * 1024,
        connection_idle_timeout: Duration::from_secs(10),
        control_plane: Some(ControlPlaneConfig {
            url: format!("ws://{console_address}/api/console/relay-control"),
            token: Arc::new(Zeroizing::new(relay_secret)),
            product_version_code: 3_002_001,
        }),
    });
    let relay_stop = CancellationToken::new();
    let relay_control = tokio::spawn(control::run(relay_state.clone(), relay_stop.clone()));
    let relay_shutdown = relay_stop.clone();
    let relay_server = tokio::spawn(async move {
        axum::serve(relay_listener, router_with_state(relay_state))
            .with_graceful_shutdown(relay_shutdown.cancelled_owned())
            .await
            .unwrap();
    });

    let reported_draining = wait_for_profile(&relay_nodes, &admin, relay_node_id, |profile| {
        profile.state == "ready" && profile.report_sequence >= 1
    })
    .await;
    assert!(reported_draining.fresh);
    assert_eq!(reported_draining.reported_draining, Some(true));
    assert_eq!(reported_draining.max_connections, Some(4_096));
    assert_eq!(reported_draining.max_rooms, Some(2_048));

    let viewer_name = format!("relay-viewer-{}", Uuid::new_v4());
    let (viewer_status, viewer_profile) = call(
        &console_router,
        "POST",
        "/api/console/users",
        "admin_web",
        Some(&admin_secret),
        json!({"username":viewer_name,"password":PASSWORD,"role":"viewer"}),
    )
    .await;
    assert_eq!(viewer_status.as_u16(), 201, "{viewer_profile}");
    let viewer_secret = login(&console_router, &viewer_name, PASSWORD, "admin_web").await;
    let relay_path = format!("/api/console/managed/relays/{relay_node_id}");
    let (viewer_change_status, _) = call(
        &console_router,
        "PATCH",
        &relay_path,
        "admin_web",
        Some(&viewer_secret),
        json!({
            "revision": reported_draining.revision,
            "configuration": {"draining": false, "disabled": false}
        }),
    )
    .await;
    assert_eq!(viewer_change_status.as_u16(), 403);
    let (change_status, changed) = call(
        &console_router,
        "PATCH",
        &relay_path,
        "admin_web",
        Some(&admin_secret),
        json!({
            "revision": reported_draining.revision,
            "configuration": {"draining": false, "disabled": false}
        }),
    )
    .await;
    assert_eq!(change_status.as_u16(), 200, "{changed}");
    let accepting = wait_for_profile(&relay_nodes, &admin, relay_node_id, |profile| {
        profile.reported_draining == Some(false) && profile.report_sequence >= 2
    })
    .await;
    assert!(!accepting.desired_draining);
    let health: serde_json::Value = reqwest::get(format!("http://{relay_address}/healthz"))
        .await
        .unwrap()
        .json()
        .await
        .unwrap();
    assert_eq!(health["accepting_new_connections"], true);

    runtime.cancellation_token().cancel();
    console_stop.cancel();
    console_server.await.unwrap();
    for _ in 0..200 {
        let health: serde_json::Value = reqwest::get(format!("http://{relay_address}/healthz"))
            .await
            .unwrap()
            .json()
            .await
            .unwrap();
        if health["accepting_new_connections"] == false {
            break;
        }
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
    let drained_health: serde_json::Value = reqwest::get(format!("http://{relay_address}/healthz"))
        .await
        .unwrap()
        .json()
        .await
        .unwrap();
    assert_eq!(drained_health["accepting_new_connections"], false);

    relay_stop.cancel();
    relay_control.await.unwrap();
    relay_server.await.unwrap();
    relay_nodes.close().await;
    runtime.shutdown().await;
}
