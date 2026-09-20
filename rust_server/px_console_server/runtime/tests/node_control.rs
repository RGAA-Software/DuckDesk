#[path = "support/runtime_fixture.rs"]
mod fixture;

use fixture::{
    call, login, register, resource_call, start_with_cache_and_relay, start_with_relay, PASSWORD,
};
use futures_util::{SinkExt, StreamExt};
use px_relay_admission::verify;
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{net::SocketAddr, time::Duration};
use tokio::net::TcpListener;
use tokio_tungstenite::{
    connect_async,
    tungstenite::{client::IntoClientRequest, http::HeaderValue, Message},
};
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

const FIXTURE_KIND: &str = "node_control";

async fn exchange<S>(socket: &mut S, request: Value) -> Value
where
    S: futures_util::Sink<Message, Error = tokio_tungstenite::tungstenite::Error>
        + futures_util::Stream<Item = Result<Message, tokio_tungstenite::tungstenite::Error>>
        + Unpin,
{
    let request_for_error = request.clone();
    socket
        .send(Message::Text(request.to_string().into()))
        .await
        .unwrap();
    let message = tokio::time::timeout(Duration::from_secs(5), socket.next())
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    let response_text = message
        .to_text()
        .unwrap_or_else(|error| panic!("non-text node response for {request_for_error}: {error}"));
    serde_json::from_str(response_text).unwrap_or_else(|error| {
        panic!("invalid node response for {request_for_error}: {error}; response={response_text:?}")
    })
}

fn telemetry_report(request_id: u64, sequence: u64, cpu_utilization_per_mille: u16) -> Value {
    json!({
        "type":"report",
        "request_id":request_id,
        "report":{
            "sequence":sequence,
            "product_version_code":1,
            "public_host":"node.example.test",
            "desktop_port":4601,
            "application_port_start":4613,
            "application_port_end":4998,
            "game_hook":true,
            "webview":true,
            "rdp":true,
            "rdp_domain":"RDP-NODE",
            "rdp_proxy_certificate_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
            "telemetry":{
                "sampled_at":chrono::Utc::now(),
                "probe_state":"ready",
                "logical_processors":16,
                "cpu_utilization_per_mille":cpu_utilization_per_mille,
                "memory_total_bytes":68719476736_u64,
                "memory_available_bytes":42949672960_u64,
                "disk_total_bytes":2199023255552_u64,
                "disk_free_bytes":1099511627776_u64,
                "gpu_inventory_revision":7,
                "gpus":[{
                    "stable_key":"pnp-sha256:0123456789abcdef",
                    "name":"Synthetic GPU",
                    "runtime_binding_ready":true,
                    "dedicated_memory_bytes":25769803776_u64,
                    "used_memory_bytes":8589934592_u64,
                    "utilization_per_mille":250,
                    "encoder_utilization_per_mille":125
                }]
            }
        }
    })
}

#[tokio::test]
async fn authenticated_node_websocket_fences_generation_and_drives_reconciliation() {
    let (runtime, _cache_directory) = start_with_cache_and_relay().await;
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
    let node_id = node["node"]["id"].as_str().unwrap();
    let (policy_status, alert_policy) = call(
        &router,
        "GET",
        &format!("/api/console/managed/nodes/{node_id}/telemetry-alert-policy"),
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(policy_status.as_u16(), 200, "{alert_policy}");
    assert_eq!(alert_policy["trigger_samples"], 3);
    let (policy_update_status, updated_alert_policy) = call(
        &router,
        "PATCH",
        &format!("/api/console/managed/nodes/{node_id}/telemetry-alert-policy"),
        "admin_web",
        Some(&admin),
        json!({
            "revision":alert_policy["revision"],
            "cpu_warning_per_mille":850,
            "cpu_critical_per_mille":950,
            "memory_warning_per_mille":850,
            "memory_critical_per_mille":950,
            "disk_warning_per_mille":850,
            "disk_critical_per_mille":950,
            "gpu_warning_per_mille":900,
            "gpu_critical_per_mille":980,
            "trigger_samples":3,
            "recovery_samples":3,
            "recovery_hysteresis_per_mille":50
        }),
    )
    .await;
    assert_eq!(policy_update_status.as_u16(), 200, "{updated_alert_policy}");
    assert_eq!(updated_alert_policy["revision"], 2);
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
                "gpu_profile":{
                    "memory_bytes":1073741824_u64,
                    "compute_per_mille":100,
                    "encoder_per_mille":100,
                    "memory_reserve_bytes":1073741824_u64,
                    "compute_limit_per_mille":900,
                    "encoder_limit_per_mille":900
                },
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

    let management_base = format!("ws://{address}/api/console/managed/events");
    let mut management_request = management_base.into_client_request().unwrap();
    management_request
        .headers_mut()
        .insert("origin", HeaderValue::from_static(fixture::ORIGIN));
    let (mut management_socket, _) = connect_async(management_request).await.unwrap();
    let management_ready = exchange(
        &mut management_socket,
        json!({"type":"authenticate","token":admin,"stream_id":null,"after":null}),
    )
    .await;
    assert_eq!(management_ready["type"], "ready");
    assert_eq!(management_ready["snapshot_required"], true);
    let initial_management_sequence = management_ready["latest_sequence"].as_u64().unwrap();

    let (realtime_device_status, realtime_device) = call(
        &router,
        "POST",
        "/api/console/managed/devices",
        "admin_web",
        Some(&admin),
        json!({"name":"realtime-device","platform":"windows"}),
    )
    .await;
    assert_eq!(realtime_device_status.as_u16(), 201, "{realtime_device}");
    let http_management_event =
        tokio::time::timeout(Duration::from_secs(5), management_socket.next())
            .await
            .unwrap()
            .unwrap()
            .unwrap();
    let http_management_event: Value =
        serde_json::from_str(http_management_event.to_text().unwrap()).unwrap();
    assert_eq!(http_management_event["type"], "event");
    assert_eq!(http_management_event["category"], "devices");
    assert_eq!(
        http_management_event["sequence"],
        initial_management_sequence + 1
    );

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
                "rdp_domain":"RDP-NODE",
                "rdp_proxy_certificate_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
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
                        "runtime_binding_ready":true,
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
    let management_event = tokio::time::timeout(Duration::from_secs(5), management_socket.next())
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    let management_event: Value =
        serde_json::from_str(management_event.to_text().unwrap()).unwrap();
    assert_eq!(management_event["type"], "event");
    assert_eq!(management_event["category"], "nodes");
    assert_eq!(management_event["resource_id"], node["node"]["id"]);
    assert_eq!(
        management_event["sequence"],
        initial_management_sequence + 2
    );

    let viewer_name = format!("viewer-{}", Uuid::new_v4());
    let (viewer_status, viewer_profile) = call(
        &router,
        "POST",
        "/api/console/users",
        "admin_web",
        Some(&admin),
        json!({"username":viewer_name,"password":PASSWORD,"role":"viewer"}),
    )
    .await;
    assert_eq!(viewer_status.as_u16(), 201, "{viewer_profile}");
    let viewer_token = login(&router, &viewer_name, PASSWORD, "admin_web").await;
    let mut viewer_request = format!("ws://{address}/api/console/managed/events")
        .into_client_request()
        .unwrap();
    viewer_request
        .headers_mut()
        .insert("origin", HeaderValue::from_static(fixture::ORIGIN));
    let (mut viewer_socket, _) = connect_async(viewer_request).await.unwrap();
    let viewer_ready = exchange(
        &mut viewer_socket,
        json!({"type":"authenticate","token":viewer_token,"stream_id":null,"after":null}),
    )
    .await;
    assert_eq!(viewer_ready["type"], "ready");
    let (disable_status, disabled_viewer) = call(
        &router,
        "PATCH",
        &format!(
            "/api/console/users/{}",
            viewer_profile["id"].as_str().unwrap()
        ),
        "admin_web",
        Some(&admin),
        json!({"revision":viewer_profile["revision"],"role":"viewer","disabled":true}),
    )
    .await;
    assert_eq!(disable_status.as_u16(), 200, "{disabled_viewer}");
    let revoked = tokio::time::timeout(Duration::from_secs(5), viewer_socket.next())
        .await
        .unwrap();
    assert!(revoked.is_none() || revoked.is_some_and(|message| message.unwrap().is_close()));
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
    assert_eq!(
        start_command["command"]["action"]["relay"],
        json!({
            "host":"relay.example.test",
            "port":4605,
            "app_key":"isolated-relay-app-key"
        })
    );
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
    assert_eq!(descriptor["relay"]["host"], "relay.example.test");
    assert_eq!(descriptor["relay"]["port"], 4605);
    let relay_admission = descriptor["relay"]["admission_ticket"]
        .as_str()
        .expect("Relay admission ticket");
    assert_ne!(relay_admission, "isolated-relay-app-key");
    let session_id = descriptor["descriptor"]["session"]["id"]
        .as_str()
        .and_then(|value| Uuid::parse_str(value).ok())
        .expect("resource session id");
    let remote_resource_id = descriptor["descriptor"]["session"]["target"]["instance_id"]
        .as_str()
        .and_then(|value| Uuid::parse_str(value).ok())
        .expect("resource instance id");
    assert!(verify(
        b"isolated-relay-app-key",
        relay_admission,
        session_id,
        remote_resource_id,
        chrono::Utc::now().timestamp().try_into().unwrap()
    ));
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

    let opened_audio_channel = exchange(
        &mut socket,
        json!({
            "type":"open_channel",
            "request_id":13,
            "channel":{
                "source_id":Uuid::new_v4(),
                "session_id":resource_session["id"],
                "kind":"audio"
            }
        }),
    )
    .await;
    assert_eq!(opened_audio_channel["type"], "channel_opened");
    let closed_audio_channel = exchange(
        &mut socket,
        json!({
            "type":"report_channel",
            "request_id":14,
            "channel_id":opened_audio_channel["channel_id"],
            "progress":{
                "sequence":1,
                "sent_bytes":480,
                "received_bytes":160,
                "elapsed_ms":500,
                "outcome":{"kind":"failed","reason":"policy_revoked"}
            }
        }),
    )
    .await;
    assert_eq!(closed_audio_channel["state"], "failed");

    let opened_file_channel = exchange(
        &mut socket,
        json!({
            "type":"open_channel",
            "request_id":15,
            "channel":{
                "source_id":Uuid::new_v4(),
                "session_id":resource_session["id"],
                "kind":"file"
            }
        }),
    )
    .await;
    assert_eq!(opened_file_channel["type"], "channel_opened");
    let closed_file_channel = exchange(
        &mut socket,
        json!({
            "type":"report_channel",
            "request_id":16,
            "channel_id":opened_file_channel["channel_id"],
            "progress":{
                "sequence":1,
                "sent_bytes":4096,
                "received_bytes":128,
                "elapsed_ms":900,
                "outcome":{"kind":"failed","reason":"transport_lost"}
            }
        }),
    )
    .await;
    assert_eq!(closed_file_channel["state"], "failed");

    let expected_file_hash = vec![7_u8; 32];
    let started_transfer = exchange(
        &mut socket,
        json!({
            "type":"begin_file_transfer",
            "request_id":17,
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
            "request_id":18,
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

    let recording_bytes = b"synthetic authenticated recording bytes";
    let recording_source_id = Uuid::new_v4();
    let recording_sha256: [u8; 32] = Sha256::digest(recording_bytes).into();
    let recording = exchange(
        &mut socket,
        json!({
            "type":"report_recording",
            "request_id":19,
            "recording":{
                "source_id":recording_source_id,
                "source_sha256":recording_sha256,
                "session_id":resource_session["id"],
                "file_name":"cloud-session.mp4",
                "size_bytes":recording_bytes.len(),
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
    let owned_channels = owned_channels.as_array().unwrap();
    assert_eq!(owned_channels.len(), 3);
    for (opened, kind, state, sent_bytes, received_bytes, close_reason) in [
        (&opened_channel, "media", "closed", 1200, 340, "peer_closed"),
        (
            &opened_audio_channel,
            "audio",
            "failed",
            480,
            160,
            "policy_revoked",
        ),
        (
            &opened_file_channel,
            "file",
            "failed",
            4096,
            128,
            "transport_lost",
        ),
    ] {
        let channel = owned_channels
            .iter()
            .find(|channel| channel["id"] == opened["channel_id"])
            .expect("owned channel");
        assert_eq!(channel["kind"], kind);
        assert_eq!(channel["state"], state);
        assert_eq!(channel["sent_bytes"], sent_bytes);
        assert_eq!(channel["received_bytes"], received_bytes);
        assert_eq!(channel["reason"], close_reason);
    }
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

    let recording_id = recording["recording_id"].as_str().unwrap();
    let (cache_status, cache_profile) = call(
        &router,
        "POST",
        &format!("/api/console/managed/recordings/{recording_id}/cache"),
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(cache_status.as_u16(), 200, "{cache_profile}");
    assert_eq!(cache_profile["state"], "fetching");
    let upload_list = exchange(
        &mut socket,
        json!({"type":"poll_recording_cache","request_id":20,"after":null,"limit":4}),
    )
    .await;
    assert_eq!(upload_list["type"], "recording_cache_uploads");
    assert_eq!(upload_list["uploads"].as_array().unwrap().len(), 1);
    let upload = &upload_list["uploads"][0];
    assert_eq!(upload["recording_id"], recording["recording_id"]);
    assert_eq!(upload["source_id"], recording_source_id.to_string());
    assert_eq!(upload["size_bytes"], recording_bytes.len());
    assert_eq!(upload["source_sha256"], json!(recording_sha256));
    assert!(upload["valid_for_ms"].as_u64().unwrap() <= 30_000);
    let upload_token = upload["upload_token"].as_str().unwrap();
    let upload_url = format!(
        "http://{address}{}",
        upload["upload_path"].as_str().unwrap()
    );
    let http = reqwest::Client::new();
    let upload_response = http
        .put(&upload_url)
        .header("authorization", format!("Bearer {upload_token}"))
        .header("content-type", "application/octet-stream")
        .body(recording_bytes.to_vec())
        .send()
        .await
        .unwrap();
    assert_eq!(upload_response.status().as_u16(), 201);
    let published: Value = serde_json::from_slice(&upload_response.bytes().await.unwrap()).unwrap();
    assert_eq!(published["state"], "ready");
    let replay = http
        .put(&upload_url)
        .header("authorization", format!("Bearer {upload_token}"))
        .header("content-type", "application/octet-stream")
        .body(recording_bytes.to_vec())
        .send()
        .await
        .unwrap();
    assert_eq!(replay.status().as_u16(), 401);
    let download = http
        .get(format!(
            "http://{address}/api/console/managed/recordings/{recording_id}/download"
        ))
        .header("authorization", format!("Bearer {admin}"))
        .header("x-pixels-client-type", "admin_web")
        .header("origin", fixture::ORIGIN)
        .header("range", "bytes=10-19")
        .send()
        .await
        .unwrap();
    assert_eq!(download.status().as_u16(), 206);
    assert_eq!(download.headers()["content-range"], "bytes 10-19/39");
    assert_eq!(
        download.bytes().await.unwrap().as_ref(),
        &recording_bytes[10..20]
    );
    let (cache_list_status, cache_list) = call(
        &router,
        "GET",
        "/api/console/managed/recording-cache?limit=100",
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(cache_list_status.as_u16(), 200, "{cache_list}");
    assert_eq!(cache_list.as_array().unwrap().len(), 1);
    assert_eq!(cache_list[0]["recording_id"], recording_id);
    assert_eq!(cache_list[0]["state"], "ready");
    let ready_revision = cache_list[0]["revision"].as_i64().unwrap();
    let (retain_status, retained) = call(
        &router,
        "PATCH",
        &format!("/api/console/managed/recordings/{recording_id}/cache"),
        "admin_web",
        Some(&admin),
        json!({"revision":ready_revision,"retained":true}),
    )
    .await;
    assert_eq!(retain_status.as_u16(), 200, "{retained}");
    assert_eq!(retained["pinned"], true);
    let retained_revision = retained["revision"].as_i64().unwrap();
    let (pinned_evict_status, _) = call(
        &router,
        "DELETE",
        &format!(
            "/api/console/managed/recordings/{recording_id}/cache?revision={retained_revision}"
        ),
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(pinned_evict_status.as_u16(), 403);
    let (release_status, released) = call(
        &router,
        "PATCH",
        &format!("/api/console/managed/recordings/{recording_id}/cache"),
        "admin_web",
        Some(&admin),
        json!({"revision":retained_revision,"retained":false}),
    )
    .await;
    assert_eq!(release_status.as_u16(), 200, "{released}");
    assert_eq!(released["pinned"], false);
    let released_revision = released["revision"].as_i64().unwrap();
    let (evict_status, evict_body) = call(
        &router,
        "DELETE",
        &format!(
            "/api/console/managed/recordings/{recording_id}/cache?revision={released_revision}"
        ),
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(evict_status.as_u16(), 204, "{evict_body}");
    let evicted_download = http
        .get(format!(
            "http://{address}/api/console/managed/recordings/{recording_id}/download"
        ))
        .header("authorization", format!("Bearer {admin}"))
        .header("x-pixels-client-type", "admin_web")
        .header("origin", fixture::ORIGIN)
        .send()
        .await
        .unwrap();
    assert_eq!(evicted_download.status().as_u16(), 403);

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
            "request_id":21,
            "session_id":resource_session["id"]
        }),
    )
    .await;
    assert_eq!(retirement["type"], "frontend_retirement_started");
    let retired = exchange(
        &mut socket,
        json!({
            "type":"finish_frontend_retirement",
            "request_id":22,
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
    let backfill_sample_id = Uuid::new_v4();
    let backfilled = exchange(
        &mut socket,
        json!({
            "type":"report_telemetry_backfill",
            "request_id":23,
            "samples":[{
                "sample_id":backfill_sample_id,
                "telemetry":{
                    "sampled_at":chrono::Utc::now()-chrono::TimeDelta::minutes(2),
                    "probe_state":"unavailable",
                    "logical_processors":null,
                    "cpu_utilization_per_mille":null,
                    "memory_total_bytes":null,
                    "memory_available_bytes":null,
                    "disk_total_bytes":null,
                    "disk_free_bytes":null,
                    "gpu_inventory_revision":null,
                    "gpus":[]
                }
            }]
        }),
    )
    .await;
    assert_eq!(backfilled["type"], "telemetry_backfilled");
    assert_eq!(backfilled["sample_ids"], json!([backfill_sample_id]));
    let stop_command = exchange(&mut socket, json!({"type":"poll_command","request_id":24})).await;
    assert_eq!(stop_command["command"]["action"]["kind"], "stop");
    let stopped = exchange(
        &mut socket,
        json!({
            "type":"acknowledge_command",
            "request_id":25,
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
        exchange(&mut socket, json!({"type":"poll_command","request_id":25})).await;
    assert_eq!(
        sequence_error,
        json!({"type":"error","request_id":25,"code":"invalid_sequence"})
    );
    let closed = tokio::time::timeout(Duration::from_secs(5), socket.next())
        .await
        .unwrap();
    assert!(closed.is_none() || closed.is_some_and(|message| message.unwrap().is_close()));

    let (mut alert_socket, _) = connect_async(&base).await.unwrap();
    let alert_authenticated = exchange(
        &mut alert_socket,
        json!({"type":"authenticate","request_id":1,"node_token":node_token}),
    )
    .await;
    assert_eq!(alert_authenticated["type"], "authenticated");
    for sequence in 1..=3 {
        let reported = exchange(
            &mut alert_socket,
            telemetry_report(sequence + 1, sequence, 960),
        )
        .await;
        assert_eq!(reported["type"], "reported");
    }
    let (alert_status, alerts) = call(
        &router,
        "GET",
        &format!("/api/console/managed/telemetry-alerts?node_id={node_id}&metric=cpu&limit=10"),
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(alert_status.as_u16(), 200, "{alerts}");
    assert_eq!(alerts.as_array().unwrap().len(), 1);
    assert_eq!(alerts[0]["severity"], "critical");
    assert_eq!(alerts[0]["state"], "active");
    let alert_id = alerts[0]["id"].as_str().unwrap();
    let (detail_status, detail) = call(
        &router,
        "GET",
        &format!("/api/console/managed/telemetry-alerts/{alert_id}"),
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(detail_status.as_u16(), 200, "{detail}");
    let (acknowledgement_status, acknowledgement) = call(
        &router,
        "PATCH",
        &format!("/api/console/managed/telemetry-alerts/{alert_id}/acknowledgement"),
        "admin_web",
        Some(&admin),
        json!({"revision":detail["revision"]}),
    )
    .await;
    assert_eq!(acknowledgement_status.as_u16(), 200, "{acknowledgement}");
    assert_eq!(acknowledgement["state"], "acknowledged");
    alert_socket.close(None).await.unwrap();

    let managed = tokio::time::timeout(Duration::from_secs(5), async {
        loop {
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
                .unwrap()
                .clone();
            if managed["state"] == "offline" {
                break managed;
            }
            tokio::time::sleep(Duration::from_millis(25)).await;
        }
    })
    .await
    .unwrap();
    assert_eq!(managed["state"], "offline");
    assert_eq!(managed["telemetry"]["probe_state"], "ready");
    assert_eq!(managed["telemetry"]["cpu_utilization_per_mille"], 960);
    assert_eq!(
        managed["gpus"][0]["stable_key"],
        "pnp-sha256:0123456789abcdef"
    );

    server_stop.cancel();
    server.await.unwrap().unwrap();
    runtime.shutdown().await;
}

#[tokio::test]
async fn rdp_start_fetches_one_leased_workspace_confirms_sid_and_issues_no_relay() {
    let runtime = start_with_relay().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let (device_status, device) = call(
        &router,
        "POST",
        "/api/console/managed/devices",
        "admin_web",
        Some(&admin),
        json!({"name":format!("rdp-node-{}", Uuid::new_v4()),"platform":"windows"}),
    )
    .await;
    assert_eq!(device_status.as_u16(), 201, "{device}");
    let (node_status, node) = call(
        &router,
        "POST",
        "/api/console/managed/nodes",
        "admin_web",
        Some(&admin),
        json!({"device_id":device["device"]["id"],"product":"cloud_node","max_instances":1}),
    )
    .await;
    assert_eq!(node_status.as_u16(), 201, "{node}");
    let (application_status, application) = call(
        &router,
        "POST",
        "/api/console/managed/applications",
        "admin_web",
        Some(&admin),
        json!({
            "name":format!("rdp-application-{}", Uuid::new_v4()),
            "launch":{"kind":"rdp"},
            "access":"public",
            "allow_observer":false,
            "allow_takeover":false,
            "disabled":false
        }),
    )
    .await;
    assert_eq!(application_status.as_u16(), 201, "{application}");
    let (deployment_status, deployment) = call(
        &router,
        "POST",
        "/api/console/managed/deployments",
        "admin_web",
        Some(&admin),
        json!({
            "application_id":application["id"],
            "node_id":node["node"]["id"],
            "configuration":{
                "target":{"kind":"rdp"},
                "gpu_key":null,
                "gpu_profile":null,
                "capacity":1,
                "disabled":false
            }
        }),
    )
    .await;
    assert_eq!(deployment_status.as_u16(), 201, "{deployment}");

    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let server_stop = CancellationToken::new();
    let shutdown_signal = server_stop.clone();
    let server_router = router.clone();
    let server = tokio::spawn(async move {
        axum::serve(
            listener,
            server_router.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .with_graceful_shutdown(shutdown_signal.cancelled_owned())
        .await
    });
    let base = format!("ws://{address}/api/console/node-control");
    let (mut socket, _) = connect_async(&base).await.unwrap();
    let authenticated = exchange(
        &mut socket,
        json!({"type":"authenticate","request_id":1,"node_token":node["node_token"]}),
    )
    .await;
    assert_eq!(authenticated["type"], "authenticated");
    let report = exchange(&mut socket, telemetry_report(2, 1, 100)).await;
    assert_eq!(report["type"], "reported");
    let assignments = exchange(
        &mut socket,
        json!({"type":"list_deployments","request_id":3,"after":null,"limit":50}),
    )
    .await;
    assert_eq!(assignments["deployments"].as_array().unwrap().len(), 1);
    assert_eq!(assignments["deployments"][0]["preparation"]["kind"], "rdp");
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
    let reconciled = exchange(
        &mut socket,
        json!({
            "type":"reconcile",
            "request_id":6,
            "inventory":{"challenge_id":challenge["challenge"]["id"],"runtimes":[]}
        }),
    )
    .await;
    assert_eq!(reconciled["type"], "reconciled");

    let username = register(&router).await;
    let user = login(&router, &username, PASSWORD, "panel").await;
    let (instance_status, instance) = resource_call(
        &router,
        "POST",
        "/api/console/instances",
        "panel",
        Some(&user),
        Some("user"),
        json!({
            "request_id":Uuid::new_v4(),
            "application_id":application["id"],
            "deployment_id":deployment["id"]
        }),
    )
    .await;
    assert_eq!(instance_status.as_u16(), 201, "{instance}");
    let start_command = exchange(&mut socket, json!({"type":"poll_command","request_id":7})).await;
    assert_eq!(start_command["command"]["action"]["launch"]["kind"], "rdp");
    assert!(start_command["command"]["action"]["gpu_reservation"].is_null());
    assert!(start_command["command"]["action"]["relay"].is_null());
    assert!(!start_command.to_string().contains("password"));

    let workspace = exchange(
        &mut socket,
        json!({
            "type":"fetch_rdp_workspace",
            "request_id":8,
            "command_id":start_command["command"]["id"],
            "lease_id":start_command["command"]["lease_id"]
        }),
    )
    .await;
    assert_eq!(workspace["type"], "rdp_workspace");
    assert_eq!(
        workspace["workspace"]["account_name"]
            .as_str()
            .unwrap()
            .len(),
        20
    );
    assert!(workspace["workspace"]["account_name"]
        .as_str()
        .unwrap()
        .starts_with("pxrdp_"));
    assert!(workspace["workspace"]["password"].as_str().unwrap().len() >= 32);
    let workspace_password = workspace["workspace"]["password"]
        .as_str()
        .unwrap()
        .to_string();
    let windows_sid = "S-1-5-21-1-2-3-1001";
    let confirmed = exchange(
        &mut socket,
        json!({
            "type":"confirm_rdp_workspace",
            "request_id":9,
            "command_id":start_command["command"]["id"],
            "lease_id":start_command["command"]["lease_id"],
            "workspace_id":workspace["workspace"]["workspace_id"],
            "windows_sid":windows_sid
        }),
    )
    .await;
    assert_eq!(confirmed["type"], "rdp_workspace_confirmed");
    let running = exchange(
        &mut socket,
        json!({
            "type":"acknowledge_command",
            "request_id":10,
            "receipt":{
                "command_id":start_command["command"]["id"],
                "lease_id":start_command["command"]["lease_id"],
                "instance_id":start_command["command"]["instance_id"],
                "launch_id":start_command["command"]["launch_id"],
                "instance_revision":start_command["command"]["instance_revision"],
                "outcome":{"result":"running","port":start_command["command"]["action"]["port"]}
            }
        }),
    )
    .await;
    assert_eq!(running["state"], "running");

    let (session_status, resource_session) = resource_call(
        &router,
        "POST",
        "/api/console/resource-sessions",
        "panel",
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
    assert_eq!(session_status.as_u16(), 201, "{resource_session}");
    let (descriptor_status, descriptor) = resource_call(
        &router,
        "POST",
        &format!(
            "/api/console/resource-sessions/{}/descriptor",
            resource_session["id"].as_str().unwrap()
        ),
        "panel",
        Some(&user),
        Some("user"),
        json!({"revision":resource_session["revision"]}),
    )
    .await;
    assert_eq!(descriptor_status.as_u16(), 200, "{descriptor}");
    assert_eq!(descriptor["descriptor"]["transport"], "rdp");
    assert!(descriptor["relay"].is_null());
    assert_eq!(descriptor["rdp"]["schema"], 1);
    assert_eq!(
        descriptor["rdp"]["account_name"],
        workspace["workspace"]["account_name"]
    );
    assert_eq!(descriptor["rdp"]["domain"], "RDP-NODE");
    assert_eq!(descriptor["rdp"]["password"], workspace_password);
    assert_eq!(
        descriptor["rdp"]["proxy_certificate_sha256"],
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    );

    let admitted = exchange(
        &mut socket,
        json!({
            "type":"admit_frontend",
            "request_id":11,
            "session_id":resource_session["id"],
            "revision":descriptor["descriptor"]["session"]["revision"],
            "frontend_token":descriptor["token"]
        }),
    )
    .await;
    assert_eq!(admitted["type"], "frontend_admitted", "{admitted}");
    assert_eq!(admitted["grant"]["access_role"], "controller");

    let opened_channel = exchange(
        &mut socket,
        json!({
            "type":"open_channel",
            "request_id":12,
            "channel":{
                "source_id":Uuid::new_v4(),
                "session_id":resource_session["id"],
                "kind":"rdp"
            }
        }),
    )
    .await;
    assert_eq!(opened_channel["type"], "channel_opened", "{opened_channel}");
    assert_eq!(opened_channel["state"], "active");
    let closed_channel = exchange(
        &mut socket,
        json!({
            "type":"report_channel",
            "request_id":13,
            "channel_id":opened_channel["channel_id"],
            "progress":{
                "sequence":1,
                "sent_bytes":4096,
                "received_bytes":2048,
                "elapsed_ms":750,
                "outcome":{"kind":"closed","reason":"peer_closed"}
            }
        }),
    )
    .await;
    assert_eq!(closed_channel["type"], "channel_reported");
    assert_eq!(closed_channel["state"], "closed");
    let (channels_status, channels) = resource_call(
        &router,
        "GET",
        &format!(
            "/api/console/activity/channels?session={}&limit=100",
            resource_session["id"].as_str().unwrap()
        ),
        "panel",
        Some(&user),
        Some("user"),
        Value::Null,
    )
    .await;
    assert_eq!(channels_status.as_u16(), 200, "{channels}");
    assert_eq!(channels.as_array().unwrap().len(), 1);
    assert_eq!(channels[0]["id"], opened_channel["channel_id"]);
    assert_eq!(channels[0]["kind"], "rdp");
    assert_eq!(channels[0]["state"], "closed");
    assert_eq!(channels[0]["sent_bytes"], 4096);
    assert_eq!(channels[0]["received_bytes"], 2048);

    socket.close(None).await.unwrap();
    server_stop.cancel();
    server.await.unwrap().unwrap();
    runtime.shutdown().await;
}
