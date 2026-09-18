#[path = "support/runtime_fixture.rs"]
mod fixture;

use fixture::{call, login, register, resource_call, start, PASSWORD};
use futures_util::{SinkExt, StreamExt};
use serde_json::{json, Value};
use std::{net::SocketAddr, time::Duration};
use tokio::net::TcpListener;
use tokio_tungstenite::{connect_async, tungstenite::Message};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

const FIXTURE_KIND: &str = "node_control";

async fn exchange<S>(socket: &mut S, request: Value) -> Value
where
    S: futures_util::Sink<Message, Error = tokio_tungstenite::tungstenite::Error>
        + futures_util::Stream<Item = Result<Message, tokio_tungstenite::tungstenite::Error>>
        + Unpin,
{
    socket
        .send(Message::Text(request.to_string().into()))
        .await
        .unwrap();
    let message = tokio::time::timeout(Duration::from_secs(5), socket.next())
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    serde_json::from_str(message.to_text().unwrap()).unwrap()
}

#[tokio::test]
async fn authenticated_node_websocket_fences_generation_and_drives_reconciliation() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let (status, device) = call(
        &router,
        "POST",
        "/api/console/managed/devices",
        "admin_web",
        Some(&admin),
        json!({"name":"node-control-device","platform":"windows"}),
    )
    .await;
    assert_eq!(status.as_u16(), 201, "{device}");
    let (status, node) = call(
        &router,
        "POST",
        "/api/console/managed/nodes",
        "admin_web",
        Some(&admin),
        json!({
            "device_id": device["device"]["id"],
            "product": "cloud_node",
            "max_instances": 4
        }),
    )
    .await;
    assert_eq!(status.as_u16(), 201, "{node}");
    let node_token = node["node_token"].as_str().unwrap();
    let (status, application) = call(
        &router,
        "POST",
        "/api/console/managed/applications",
        "admin_web",
        Some(&admin),
        json!({
            "name":"node-control-application",
            "launch":{
                "kind":"webview",
                "entry_url":"https://example.test/cloud",
                "video":{"codec":"h264","bitrate_kbps":8000}
            },
            "access":"public",
            "allow_observer":false,
            "allow_takeover":false,
            "disabled":false
        }),
    )
    .await;
    assert_eq!(status.as_u16(), 201, "{application}");
    let (status, deployment) = call(
        &router,
        "POST",
        "/api/console/managed/deployments",
        "admin_web",
        Some(&admin),
        json!({
            "application_id":application["id"],
            "node_id":node["node"]["id"],
            "configuration":{
                "target":{"kind":"webview"},
                "gpu_key":null,
                "capacity":4,
                "disabled":false
            }
        }),
    )
    .await;
    assert_eq!(status.as_u16(), 201, "{deployment}");

    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let server_stop = CancellationToken::new();
    let server_router = router.clone();
    let shutdown_signal = server_stop.clone();
    let server = tokio::spawn(async move {
        axum::serve(
            listener,
            server_router.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .with_graceful_shutdown(shutdown_signal.cancelled_owned())
        .await
    });
    let base = format!("ws://{address}/api/console/node-control");
    let query_error = connect_async(format!("{base}?token=forbidden"))
        .await
        .unwrap_err();
    assert_eq!(query_error.to_string(), "HTTP error: 403 Forbidden");

    let (mut socket, _) = connect_async(&base).await.unwrap();
    let authenticated = exchange(
        &mut socket,
        json!({"type":"authenticate","request_id":1,"node_token":node_token}),
    )
    .await;
    assert_eq!(authenticated["type"], "authenticated");
    assert_eq!(authenticated["node_id"], node["node"]["id"]);
    assert!(authenticated.get("node_token").is_none());

    let report = exchange(
        &mut socket,
        json!({
            "type":"report",
            "request_id":2,
            "report":{
                "sequence":1,
                "product_version_code":1,
                "public_host":"node.example.test",
                "desktop_port":4601,
                "application_port_start":4613,
                "application_port_end":4998,
                "game_hook":true,
                "webview":true,
                "rdp":true
            }
        }),
    )
    .await;
    assert_eq!(report["type"], "reported");
    assert_eq!(report["state"], "reconciling");
    let assignments = exchange(
        &mut socket,
        json!({"type":"list_deployments","request_id":3,"after":null,"limit":50}),
    )
    .await;
    assert_eq!(assignments["type"], "deployments");
    assert_eq!(assignments["deployments"].as_array().unwrap().len(), 1);
    assert_eq!(assignments["deployments"][0]["id"], deployment["id"]);
    assert_eq!(
        assignments["deployments"][0]["preparation"]["kind"],
        "webview"
    );
    assert!(assignments["deployments"][0]["preparation"]
        .get("entry_url")
        .is_none());
    let deployment_report = exchange(
        &mut socket,
        json!({
            "type":"report_deployment",
            "request_id":4,
            "deployment_id":deployment["id"],
            "observation":{
                "deployment_revision":deployment["revision"],
                "application_revision":deployment["application_revision"],
                "endpoint_revision":report["endpoint_revision"],
                "sequence":1,
                "status":{"state":"ready"}
            }
        }),
    )
    .await;
    assert_eq!(deployment_report["type"], "deployment_reported");

    let challenge = exchange(
        &mut socket,
        json!({"type":"begin_reconciliation","request_id":5}),
    )
    .await;
    assert_eq!(challenge["type"], "reconciliation_started");
    let reconciled = exchange(
        &mut socket,
        json!({
            "type":"reconcile",
            "request_id":6,
            "inventory":{"challenge_id":challenge["challenge"]["id"],"runtimes":[]}
        }),
    )
    .await;
    assert_eq!(reconciled, json!({"type":"reconciled","request_id":6}));

    let username = register(&router).await;
    let user = login(&router, &username, PASSWORD, "android").await;
    let (status, instance) = resource_call(
        &router,
        "POST",
        "/api/console/instances",
        "android",
        Some(&user),
        Some("user"),
        json!({
            "request_id":Uuid::new_v4(),
            "application_id":application["id"],
            "deployment_id":deployment["id"]
        }),
    )
    .await;
    assert_eq!(status.as_u16(), 201, "{instance}");
    let start_command = exchange(&mut socket, json!({"type":"poll_command","request_id":7})).await;
    assert_eq!(start_command["type"], "command");
    assert_eq!(start_command["command"]["action"]["kind"], "start");
    let running = exchange(
        &mut socket,
        json!({
            "type":"acknowledge_command",
            "request_id":8,
            "receipt":{
                "command_id":start_command["command"]["id"],
                "lease_id":start_command["command"]["lease_id"],
                "instance_id":start_command["command"]["instance_id"],
                "launch_id":start_command["command"]["launch_id"],
                "instance_revision":start_command["command"]["instance_revision"],
                "outcome":{
                    "result":"running",
                    "port":start_command["command"]["action"]["port"]
                }
            }
        }),
    )
    .await;
    assert_eq!(running["type"], "command_acknowledged");
    assert_eq!(running["state"], "running");

    let instance_id = instance["id"].as_str().unwrap();
    let (status, resource_session) = resource_call(
        &router,
        "POST",
        "/api/console/resource-sessions",
        "android",
        Some(&user),
        Some("user"),
        json!({
            "request_id":Uuid::new_v4(),
            "target":{
                "kind":"cloud_application",
                "application_id":application["id"],
                "instance_id":instance["id"]
            },
            "access":"controller"
        }),
    )
    .await;
    assert_eq!(status.as_u16(), 201, "{resource_session}");
    let session_id = resource_session["id"].as_str().unwrap();
    let (status, owned_visits) = resource_call(
        &router,
        "GET",
        "/api/console/activity/visits?limit=100",
        "android",
        Some(&user),
        Some("user"),
        Value::Null,
    )
    .await;
    assert_eq!(status.as_u16(), 200, "{owned_visits}");
    assert_eq!(owned_visits.as_array().unwrap().len(), 1);
    assert_eq!(owned_visits[0]["session"]["id"], resource_session["id"]);
    assert_eq!(
        owned_visits[0]["session"]["target"]["kind"],
        "cloud_application"
    );
    let (status, managed_visits) = call(
        &router,
        "GET",
        "/api/console/managed/activity/visits?limit=100",
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(status.as_u16(), 200, "{managed_visits}");
    assert!(managed_visits
        .as_array()
        .unwrap()
        .iter()
        .any(|visit| visit["session"]["id"] == resource_session["id"]));
    for path in [
        format!("/api/console/activity/channels?session={session_id}&limit=100"),
        "/api/console/file-transfers?limit=100".into(),
    ] {
        assert_eq!(
            resource_call(
                &router,
                "GET",
                &path,
                "android",
                Some(&user),
                Some("user"),
                Value::Null,
            )
            .await,
            (axum::http::StatusCode::OK, json!([]))
        );
    }
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/managed/file-transfers?limit=100",
            "admin_web",
            Some(&admin),
            Value::Null,
        )
        .await,
        (axum::http::StatusCode::OK, json!([]))
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/managed/recordings?limit=100",
            "admin_web",
            Some(&admin),
            Value::Null,
        )
        .await,
        (axum::http::StatusCode::OK, json!([]))
    );
    let (status, stopping) = resource_call(
        &router,
        "POST",
        &format!("/api/console/instances/{instance_id}/stop"),
        "android",
        Some(&user),
        Some("user"),
        json!({"revision":running["revision"]}),
    )
    .await;
    assert_eq!(status.as_u16(), 200, "{stopping}");
    let stop_command = exchange(&mut socket, json!({"type":"poll_command","request_id":9})).await;
    assert_eq!(stop_command["command"]["action"]["kind"], "stop");
    let stopped = exchange(
        &mut socket,
        json!({
            "type":"acknowledge_command",
            "request_id":10,
            "receipt":{
                "command_id":stop_command["command"]["id"],
                "lease_id":stop_command["command"]["lease_id"],
                "instance_id":stop_command["command"]["instance_id"],
                "launch_id":stop_command["command"]["launch_id"],
                "instance_revision":stop_command["command"]["instance_revision"],
                "outcome":{"result":"absent"}
            }
        }),
    )
    .await;
    assert_eq!(stopped["state"], "stopped");

    let sequence_error =
        exchange(&mut socket, json!({"type":"poll_command","request_id":10})).await;
    assert_eq!(
        sequence_error,
        json!({"type":"error","request_id":10,"code":"invalid_sequence"})
    );
    let closed = tokio::time::timeout(Duration::from_secs(5), socket.next())
        .await
        .unwrap();
    assert!(closed.is_none() || closed.is_some_and(|message| message.unwrap().is_close()));

    let (_, nodes) = call(
        &router,
        "GET",
        "/api/console/managed/nodes?limit=100",
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    let managed = nodes
        .as_array()
        .unwrap()
        .iter()
        .find(|candidate| candidate["id"] == node["node"]["id"])
        .unwrap();
    assert_eq!(managed["state"], "offline");

    server_stop.cancel();
    server.await.unwrap().unwrap();
    runtime.shutdown().await;
}
