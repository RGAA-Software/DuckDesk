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
                "rdp":true,
                "telemetry":{
                    "sampled_at":chrono::Utc::now(),
                    "probe_state":"ready",
                    "logical_processors":16,
                    "cpu_utilization_per_mille":375,
                    "memory_total_bytes":68719476736_u64,
                    "memory_available_bytes":42949672960_u64,
                    "disk_total_bytes":2199023255552_u64,
                    "disk_free_bytes":1099511627776_u64,
                    "gpu_inventory_revision":7,
                    "gpus":[{
                        "stable_key":"pnp-sha256:0123456789abcdef",
                        "name":"Synthetic GPU",
                        "dedicated_memory_bytes":25769803776_u64,
                        "used_memory_bytes":8589934592_u64,
                        "utilization_per_mille":250,
                        "encoder_utilization_per_mille":125
                    }]
                }
            }
        }),
    )
    .await;
    assert_eq!(report["type"], "reported");
    assert_eq!(report["state"], "reconciling");
    let node_id = node["node"]["id"].as_str().unwrap();
    let (history_status, history) = call(
        &router,
        "GET",
        &format!("/api/console/managed/nodes/{node_id}/telemetry?limit=1"),
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(history_status.as_u16(), 200, "{history}");
    assert_eq!(history.as_array().unwrap().len(), 1);
    assert_eq!(history[0]["report_sequence"], 1);
    assert_eq!(history[0]["cpu_utilization_per_mille"], 375);
    assert_eq!(history[0]["gpus"].as_array().unwrap().len(), 1);
    assert_eq!(
        history[0]["gpus"][0]["stable_key"],
        "pnp-sha256:0123456789abcdef"
    );
    assert_eq!(
        call(
            &router,
            "GET",
            &format!("/api/console/managed/nodes/{node_id}/telemetry?limit=1&before_generation=1"),
            "admin_web",
            Some(&admin),
            Value::Null,
        )
        .await
        .0
        .as_u16(),
        400
    );
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
    let (status, descriptor) = resource_call(
        &router,
        "POST",
        &format!("/api/console/resource-sessions/{session_id}/descriptor"),
        "android",
        Some(&user),
        Some("user"),
        json!({"revision":resource_session["revision"]}),
    )
    .await;
    assert_eq!(status.as_u16(), 200, "{descriptor}");
    let expected_frontends =
        exchange(&mut socket, json!({"type":"list_frontends","request_id":9})).await;
    assert_eq!(expected_frontends["type"], "frontends");
    assert!(expected_frontends["frontends"]
        .as_array()
        .unwrap()
        .iter()
        .any(|frontend| frontend["id"] == resource_session["id"]));
    let admitted = exchange(
        &mut socket,
        json!({
            "type":"admit_frontend",
            "request_id":10,
            "session_id":resource_session["id"],
            "revision":descriptor["descriptor"]["session"]["revision"],
            "frontend_token":descriptor["token"]
        }),
    )
    .await;
    assert_eq!(admitted["type"], "frontend_admitted");
    assert_eq!(admitted["grant"]["session_id"], resource_session["id"]);
    assert_eq!(admitted["grant"]["target"]["kind"], "cloud_application");
    assert_eq!(admitted["grant"]["client_type"], "android");
    assert_eq!(admitted["grant"]["access_role"], "controller");
    assert!(admitted["grant"]["valid_for_ms"].as_u64().unwrap() <= 30_000);

    let channel_source_id = Uuid::new_v4();
    let opened_channel = exchange(
        &mut socket,
        json!({
            "type":"open_channel",
            "request_id":11,
            "channel":{
                "source_id":channel_source_id,
                "session_id":resource_session["id"],
                "kind":"media"
            }
        }),
    )
    .await;
    assert_eq!(opened_channel["type"], "channel_opened");
    assert_eq!(opened_channel["state"], "active");
    let closed_channel = exchange(
        &mut socket,
        json!({
            "type":"report_channel",
            "request_id":12,
            "channel_id":opened_channel["channel_id"],
            "progress":{
                "sequence":1,
                "sent_bytes":1200,
                "received_bytes":340,
                "elapsed_ms":2500,
                "outcome":{"kind":"closed","reason":"peer_closed"}
            }
        }),
    )
    .await;
    assert_eq!(closed_channel["type"], "channel_reported");
    assert_eq!(closed_channel["state"], "closed");

    let expected_file_hash = vec![7_u8; 32];
    let started_transfer = exchange(
        &mut socket,
        json!({
            "type":"begin_file_transfer",
            "request_id":13,
            "transfer":{
                "transfer_request_id":Uuid::new_v4(),
                "session_id":resource_session["id"],
                "direction":"to_node",
                "file_name":"cloud-save.bin",
                "total_bytes":4096,
                "expected_sha256":expected_file_hash
            }
        }),
    )
    .await;
    assert_eq!(started_transfer["type"], "file_transfer_started");
    let completed_transfer = exchange(
        &mut socket,
        json!({
            "type":"report_file_transfer",
            "request_id":14,
            "transfer_id":started_transfer["transfer_id"],
            "progress":{
                "sequence":1,
                "transferred_bytes":4096,
                "outcome":{"kind":"completed","received_sha256":expected_file_hash}
            }
        }),
    )
    .await;
    assert_eq!(completed_transfer["type"], "file_transfer_reported");
    assert_eq!(completed_transfer["state"], "completed");

    let recording = exchange(
        &mut socket,
        json!({
            "type":"report_recording",
            "request_id":15,
            "recording":{
                "source_id":Uuid::new_v4(),
                "source_sha256":vec![9_u8; 32],
                "session_id":resource_session["id"],
                "file_name":"cloud-session.mp4",
                "size_bytes":8192,
                "modified_unix_ms":1_700_000_000_000_i64,
                "codec":"h264",
                "sequence":1,
                "present":true
            }
        }),
    )
    .await;
    assert_eq!(recording["type"], "recording_reported");
    assert_eq!(recording["reported_present"], true);
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
    let (_, owned_channels) = resource_call(
        &router,
        "GET",
        &format!("/api/console/activity/channels?session={session_id}&limit=100"),
        "android",
        Some(&user),
        Some("user"),
        Value::Null,
    )
    .await;
    assert_eq!(owned_channels.as_array().unwrap().len(), 1);
    assert_eq!(owned_channels[0]["id"], opened_channel["channel_id"]);
    assert_eq!(owned_channels[0]["state"], "closed");
    let (_, owned_transfers) = resource_call(
        &router,
        "GET",
        "/api/console/file-transfers?limit=100",
        "android",
        Some(&user),
        Some("user"),
        Value::Null,
    )
    .await;
    assert_eq!(owned_transfers.as_array().unwrap().len(), 1);
    assert_eq!(owned_transfers[0]["id"], started_transfer["transfer_id"]);
    assert_eq!(owned_transfers[0]["state"], "completed");
    let (_, managed_recordings) = call(
        &router,
        "GET",
        "/api/console/managed/recordings?limit=100",
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(managed_recordings.as_array().unwrap().len(), 1);
    assert_eq!(managed_recordings[0]["id"], recording["recording_id"]);

    let (status, closing) = resource_call(
        &router,
        "POST",
        &format!("/api/console/resource-sessions/{session_id}/close"),
        "android",
        Some(&user),
        Some("user"),
        json!({"revision":admitted["grant"]["revision"]}),
    )
    .await;
    assert_eq!(status.as_u16(), 200, "{closing}");
    assert_eq!(closing["state"], "closing");
    let retirement = exchange(
        &mut socket,
        json!({
            "type":"begin_frontend_retirement",
            "request_id":16,
            "session_id":resource_session["id"]
        }),
    )
    .await;
    assert_eq!(retirement["type"], "frontend_retirement_started");
    let retired = exchange(
        &mut socket,
        json!({
            "type":"finish_frontend_retirement",
            "request_id":17,
            "session_id":resource_session["id"],
            "challenge_id":retirement["retirement"]["challenge_id"]
        }),
    )
    .await;
    assert_eq!(retired["type"], "frontend_retired");
    assert_eq!(retired["session_id"], resource_session["id"]);
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
    let stop_command = exchange(&mut socket, json!({"type":"poll_command","request_id":18})).await;
    assert_eq!(stop_command["command"]["action"]["kind"], "stop");
    let stopped = exchange(
        &mut socket,
        json!({
            "type":"acknowledge_command",
            "request_id":19,
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
        exchange(&mut socket, json!({"type":"poll_command","request_id":19})).await;
    assert_eq!(
        sequence_error,
        json!({"type":"error","request_id":19,"code":"invalid_sequence"})
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
    assert_eq!(managed["telemetry"]["probe_state"], "ready");
    assert_eq!(managed["telemetry"]["cpu_utilization_per_mille"], 375);
    assert_eq!(
        managed["gpus"][0]["stable_key"],
        "pnp-sha256:0123456789abcdef"
    );

    server_stop.cancel();
    server.await.unwrap().unwrap();
    runtime.shutdown().await;
}
