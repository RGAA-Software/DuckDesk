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
use std::{
    env,
    net::SocketAddr,
    process::{Child, Command, Stdio},
    sync::Arc,
    time::Duration,
};
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

async fn create_managed_relay(
    console_router: &axum::Router,
    administrator_secret: &str,
    name: String,
    public_host: &str,
    public_port: u16,
) -> (Uuid, String) {
    let (create_status, created) = call(
        console_router,
        "POST",
        "/api/console/managed/relays",
        "admin_web",
        Some(administrator_secret),
        json!({
            "name": name,
            "public_host": public_host,
            "public_port": public_port
        }),
    )
    .await;
    assert_eq!(create_status.as_u16(), 201, "{created}");
    let relay_secret = created["relay_token"].as_str().unwrap().to_owned();
    assert_eq!(relay_secret.len(), 64);
    let relay_node_id = created["relay"]["id"].as_str().unwrap().parse().unwrap();
    (relay_node_id, relay_secret)
}

struct RunningRelay {
    address: SocketAddr,
    runtime: RelayRuntime,
}

enum RelayRuntime {
    Embedded {
        cancellation: CancellationToken,
        control_task: tokio::task::JoinHandle<()>,
        server_task: tokio::task::JoinHandle<()>,
    },
    Packaged(RelayChild),
}

struct RelayChild(Child);

impl Drop for RelayChild {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

impl RunningRelay {
    async fn start(
        console_address: SocketAddr,
        relay_secret: String,
        max_connections: usize,
        max_rooms: usize,
    ) -> Self {
        let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
        let address = listener.local_addr().unwrap();
        if let Some(relay_binary) = env::var_os("PIXELS_TEST_RELAY_BINARY") {
            drop(listener);
            let relay_process = Command::new(relay_binary)
                .env("PIXELS_RELAY_LISTEN", address.to_string())
                .env("PIXELS_RELAY_APP_KEY", "integration-relay-app-key")
                .env(
                    "PIXELS_RELAY_CONTROL_KEY",
                    "integration-relay-control-key-32",
                )
                .env(
                    "PIXELS_RELAY_CONSOLE_CONTROL_URL",
                    format!("ws://{console_address}/api/console/relay-control"),
                )
                .env("PIXELS_RELAY_NODE_TOKEN", relay_secret)
                .env("PIXELS_RELAY_MAX_CONNECTIONS", max_connections.to_string())
                .env("PIXELS_RELAY_MAX_ROOMS", max_rooms.to_string())
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .unwrap();
            return Self {
                address,
                runtime: RelayRuntime::Packaged(RelayChild(relay_process)),
            };
        }
        let relay_state = RelayServerState::new(RelayConfig {
            listen: address,
            app_key: b"integration-relay-app-key".to_vec(),
            control_key: b"integration-relay-control-key-32".to_vec(),
            max_connections,
            max_rooms,
            outbound_queue: 32,
            max_message_bytes: 1024 * 1024,
            connection_idle_timeout: Duration::from_secs(10),
            control_plane: Some(ControlPlaneConfig {
                url: format!("ws://{console_address}/api/console/relay-control"),
                token: Arc::new(Zeroizing::new(relay_secret)),
                product_version_code: 3_002_001,
                tls_config: None,
            }),
        });
        let cancellation = CancellationToken::new();
        let control_task = tokio::spawn(control::run(relay_state.clone(), cancellation.clone()));
        let shutdown = cancellation.clone();
        let server_task = tokio::spawn(async move {
            axum::serve(listener, router_with_state(relay_state))
                .with_graceful_shutdown(shutdown.cancelled_owned())
                .await
                .unwrap();
        });
        Self {
            address,
            runtime: RelayRuntime::Embedded {
                cancellation,
                control_task,
                server_task,
            },
        }
    }

    async fn accepting(&self) -> bool {
        let health: serde_json::Value = reqwest::get(format!("http://{}/healthz", self.address))
            .await
            .unwrap()
            .json()
            .await
            .unwrap();
        health["accepting_new_connections"] == true
    }

    async fn wait_for_accepting(&self, expected: bool) {
        for _ in 0..200 {
            if self.accepting().await == expected {
                return;
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
        panic!("Relay did not converge to accepting_new_connections={expected}");
    }

    async fn shutdown(self) {
        match self.runtime {
            RelayRuntime::Embedded {
                cancellation,
                control_task,
                server_task,
            } => {
                cancellation.cancel();
                control_task.await.unwrap();
                server_task.await.unwrap();
            }
            RelayRuntime::Packaged(mut relay_child) => {
                let _ = relay_child.0.kill();
                let _ = relay_child.0.wait();
            }
        }
    }
}

#[tokio::test]
async fn two_relays_converge_independently_and_fail_closed_when_console_stops() {
    let runtime = start().await;
    let console_router = runtime.router();
    let admin_secret = login(&console_router, "initial-admin", PASSWORD, "admin_web").await;
    let admin = digest(&admin_secret);
    let (first_relay_node_id, first_relay_secret) = create_managed_relay(
        &console_router,
        &admin_secret,
        format!("first-active-relay-{}", Uuid::new_v4()),
        "first-relay.example.test",
        4605,
    )
    .await;
    let (second_relay_node_id, second_relay_secret) = create_managed_relay(
        &console_router,
        &admin_secret,
        format!("second-active-relay-{}", Uuid::new_v4()),
        "second-relay.example.test",
        4606,
    )
    .await;
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
    assert_eq!(listed.as_array().unwrap().len(), 2);
    assert!(listed
        .as_array()
        .unwrap()
        .iter()
        .all(|profile| profile.get("relay_token").is_none()));

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

    let first_relay = RunningRelay::start(console_address, first_relay_secret, 4_096, 2_048).await;
    let second_relay =
        RunningRelay::start(console_address, second_relay_secret, 2_048, 1_024).await;

    let first_reported_draining =
        wait_for_profile(&relay_nodes, &admin, first_relay_node_id, |profile| {
            profile.state == "ready" && profile.report_sequence >= 1
        })
        .await;
    let second_reported_draining =
        wait_for_profile(&relay_nodes, &admin, second_relay_node_id, |profile| {
            profile.state == "ready" && profile.report_sequence >= 1
        })
        .await;
    assert!(first_reported_draining.fresh);
    assert_eq!(first_reported_draining.reported_draining, Some(true));
    assert_eq!(first_reported_draining.max_connections, Some(4_096));
    assert_eq!(first_reported_draining.max_rooms, Some(2_048));
    assert!(second_reported_draining.fresh);
    assert_eq!(second_reported_draining.reported_draining, Some(true));
    assert_eq!(second_reported_draining.max_connections, Some(2_048));
    assert_eq!(second_reported_draining.max_rooms, Some(1_024));

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
    let first_relay_path = format!("/api/console/managed/relays/{first_relay_node_id}");
    let (viewer_change_status, _) = call(
        &console_router,
        "PATCH",
        &first_relay_path,
        "admin_web",
        Some(&viewer_secret),
        json!({
            "revision": first_reported_draining.revision,
            "configuration": {"draining": false, "disabled": false}
        }),
    )
    .await;
    assert_eq!(viewer_change_status.as_u16(), 403);

    for (relay_node_id, revision) in [
        (first_relay_node_id, first_reported_draining.revision),
        (second_relay_node_id, second_reported_draining.revision),
    ] {
        let relay_path = format!("/api/console/managed/relays/{relay_node_id}");
        let (change_status, changed) = call(
            &console_router,
            "PATCH",
            &relay_path,
            "admin_web",
            Some(&admin_secret),
            json!({
                "revision": revision,
                "configuration": {"draining": false, "disabled": false}
            }),
        )
        .await;
        assert_eq!(change_status.as_u16(), 200, "{changed}");
    }
    let first_accepting = wait_for_profile(&relay_nodes, &admin, first_relay_node_id, |profile| {
        profile.reported_draining == Some(false) && profile.report_sequence >= 2
    })
    .await;
    let second_accepting =
        wait_for_profile(&relay_nodes, &admin, second_relay_node_id, |profile| {
            profile.reported_draining == Some(false) && profile.report_sequence >= 2
        })
        .await;
    assert!(!first_accepting.desired_draining);
    assert!(!second_accepting.desired_draining);
    first_relay.wait_for_accepting(true).await;
    second_relay.wait_for_accepting(true).await;

    let (drain_status, drained) = call(
        &console_router,
        "PATCH",
        &first_relay_path,
        "admin_web",
        Some(&admin_secret),
        json!({
            "revision": first_accepting.revision,
            "configuration": {"draining": true, "disabled": false}
        }),
    )
    .await;
    assert_eq!(drain_status.as_u16(), 200, "{drained}");
    first_relay.wait_for_accepting(false).await;
    second_relay.wait_for_accepting(true).await;
    let draining_profile = wait_for_profile(&relay_nodes, &admin, first_relay_node_id, |profile| {
        profile.desired_draining
    })
    .await;
    assert!(draining_profile.desired_draining);

    runtime.cancellation_token().cancel();
    console_stop.cancel();
    console_server.await.unwrap();
    second_relay.wait_for_accepting(false).await;

    first_relay.shutdown().await;
    second_relay.shutdown().await;
    relay_nodes.close().await;
    runtime.shutdown().await;
}
