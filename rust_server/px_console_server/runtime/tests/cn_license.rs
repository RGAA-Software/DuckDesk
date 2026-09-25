#[path = "support/runtime_fixture.rs"]
mod fixture;

use axum::http::StatusCode;
use fixture::{call, login, register, resource_call, PASSWORD};
use futures_util::{SinkExt, StreamExt};
use px_console_runtime::{ConsoleRuntime, LicenseLaunchConfig, ReleaseIdentity, RuntimeResources};
use px_console_store::{initialize_administrator, PasswordDigest, Username};
use serde_json::{json, Value};
use std::{env, net::SocketAddr, path::PathBuf, time::Duration};
use tokio::net::TcpListener;
use tokio_tungstenite::{connect_async, tungstenite::Message};
use uuid::Uuid;

const FIXTURE_KIND: &str = "cn_license";

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
    let response = tokio::time::timeout(Duration::from_secs(10), socket.next())
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    serde_json::from_str(response.to_text().unwrap()).unwrap()
}

fn node_report() -> Value {
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
                "cpu_utilization_per_mille":250,
                "memory_total_bytes":68719476736_u64,
                "memory_available_bytes":42949672960_u64,
                "disk_total_bytes":2199023255552_u64,
                "disk_free_bytes":1099511627776_u64,
                "gpu_inventory_revision":1,
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
async fn cn_signed_customer_license_controls_live_console_api() {
    let deployment_id = fixture::deployment();
    let license_trust_path = PathBuf::from(env::var("PIXELS_TEST_CN_LICENSE_TRUST_STORE").unwrap());
    let license_path = PathBuf::from(env::var("PIXELS_TEST_CN_LICENSE_FILE").unwrap());
    let license = LicenseLaunchConfig::new(license_trust_path, license_path)
        .unwrap()
        .admit(deployment_id)
        .await
        .unwrap();
    let license_id = license.payload.license_id;
    assert_eq!(license.payload.max_streams, 1);
    assert_eq!(
        license.payload.services,
        vec![px_license::LicensedService::CloudApplications]
    );

    let password_digest = px_credentials::hash(PASSWORD).unwrap();
    initialize_administrator(
        &fixture::config("OWNER"),
        deployment_id,
        &Username::parse("initial-admin").unwrap(),
        &PasswordDigest::parse(password_digest.to_string()).unwrap(),
    )
    .await
    .unwrap();
    let runtime = ConsoleRuntime::activate_product_with_cache(
        &fixture::config("RUNTIME"),
        deployment_id,
        fixture::vault(),
        fixture::policy(),
        fixture::guests(),
        RuntimeResources {
            recording_cache: None,
            relay_admission: None,
            release: ReleaseIdentity::integration(),
        },
        license,
    )
    .await
    .unwrap();
    let router = runtime.router();
    let administrator_token = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let (license_status_code, license_status) = call(
        &router,
        "GET",
        "/api/console/managed/license",
        "admin_web",
        Some(&administrator_token),
        Value::Null,
    )
    .await;
    assert_eq!(license_status_code, StatusCode::OK, "{license_status}");
    assert_eq!(license_status["license_id"], license_id.to_string());
    assert_eq!(license_status["max_streams"], 1);
    assert_eq!(license_status["services"], json!(["cloud_applications"]));

    let username = register(&router).await;
    let user_token = login(&router, &username, PASSWORD, "android").await;
    for (application_kind, expected_status) in [
        ("rdp", StatusCode::FORBIDDEN),
        ("webview", StatusCode::SERVICE_UNAVAILABLE),
    ] {
        let application_specification = match application_kind {
            "rdp" => {
                json!({"name":Uuid::new_v4().to_string(),"launch":{"kind":"rdp"},"access":"public","allow_observer":false,"allow_takeover":false,"disabled":false})
            }
            _ => {
                json!({"name":Uuid::new_v4().to_string(),"launch":{"kind":"webview","entry_url":"https://example.test/app","video":{"codec":"h264","bitrate_kbps":8000}},"access":"public","allow_observer":false,"allow_takeover":false,"disabled":false})
            }
        };
        let (create_status, application) = call(
            &router,
            "POST",
            "/api/console/managed/applications",
            "admin_web",
            Some(&administrator_token),
            application_specification,
        )
        .await;
        assert_eq!(create_status, StatusCode::CREATED, "{application}");
        let (start_status, start_response) = resource_call(
            &router,
            "POST",
            "/api/console/instances",
            "android",
            Some(&user_token),
            Some("user"),
            json!({"request_id":Uuid::new_v4(),"application_id":application["id"],"deployment_id":null}),
        )
        .await;
        assert_eq!(
            start_status, expected_status,
            "{application_kind}: {start_response}"
        );
    }
    let (instances_status, instances) = resource_call(
        &router,
        "GET",
        "/api/console/instances?limit=100",
        "android",
        Some(&user_token),
        Some("user"),
        Value::Null,
    )
    .await;
    assert_eq!(instances_status, StatusCode::OK, "{instances}");
    assert_eq!(instances, json!([]));

    let (device_status, device) = call(
        &router,
        "POST",
        "/api/console/managed/devices",
        "admin_web",
        Some(&administrator_token),
        json!({"name":Uuid::new_v4().to_string(),"platform":"windows"}),
    )
    .await;
    assert_eq!(device_status, StatusCode::CREATED, "{device}");
    let (node_status, node) = call(
        &router,
        "POST",
        "/api/console/managed/nodes",
        "admin_web",
        Some(&administrator_token),
        json!({"device_id":device["device"]["id"],"product":"cloud_node","max_instances":4}),
    )
    .await;
    assert_eq!(node_status, StatusCode::CREATED, "{node}");
    let (application_status, application) = call(
        &router,
        "POST",
        "/api/console/managed/applications",
        "admin_web",
        Some(&administrator_token),
        json!({"name":Uuid::new_v4().to_string(),"launch":{"kind":"webview","entry_url":"https://example.test/quota","video":{"codec":"h264","bitrate_kbps":8000}},"access":"public","allow_observer":false,"allow_takeover":false,"disabled":false}),
    )
    .await;
    assert_eq!(application_status, StatusCode::CREATED, "{application}");
    let (deployment_status, deployment) = call(
        &router,
        "POST",
        "/api/console/managed/deployments",
        "admin_web",
        Some(&administrator_token),
        json!({"application_id":application["id"],"node_id":node["node"]["id"],"configuration":{"target":{"kind":"webview"},"gpu_key":null,"gpu_profile":{"memory_bytes":1073741824_u64,"compute_per_mille":100,"encoder_per_mille":100,"memory_reserve_bytes":1073741824_u64,"compute_limit_per_mille":900,"encoder_limit_per_mille":900},"capacity":4,"disabled":false}}),
    )
    .await;
    assert_eq!(deployment_status, StatusCode::CREATED, "{deployment}");

    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let server_router = router.clone();
    let server = tokio::spawn(async move {
        axum::serve(
            listener,
            server_router.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .await
    });
    let (mut node_socket, _) = connect_async(format!("ws://{address}/api/console/node-control"))
        .await
        .unwrap();
    let authenticated = exchange(
        &mut node_socket,
        json!({"type":"authenticate","request_id":1,"node_token":node["node_token"]}),
    )
    .await;
    assert_eq!(authenticated["type"], "authenticated");
    let report = exchange(&mut node_socket, node_report()).await;
    assert_eq!(report["type"], "reported");
    let prepared = exchange(
        &mut node_socket,
        json!({"type":"report_deployment","request_id":3,"deployment_id":deployment["id"],"observation":{"deployment_revision":deployment["revision"],"application_revision":deployment["application_revision"],"endpoint_revision":report["endpoint_revision"],"sequence":1,"status":{"state":"ready"}}}),
    )
    .await;
    assert_eq!(prepared["type"], "deployment_reported");
    let challenge = exchange(
        &mut node_socket,
        json!({"type":"begin_reconciliation","request_id":4}),
    )
    .await;
    assert_eq!(challenge["type"], "reconciliation_started");
    let reconciled = exchange(
        &mut node_socket,
        json!({"type":"reconcile","request_id":5,"inventory":{"challenge_id":challenge["challenge"]["id"],"runtimes":[]}}),
    )
    .await;
    assert_eq!(reconciled["type"], "reconciled");

    let mut running_instances = Vec::new();
    for request_number in 0..2 {
        let (start_status, instance) = resource_call(
            &router,
            "POST",
            "/api/console/instances",
            "android",
            Some(&user_token),
            Some("user"),
            json!({"request_id":Uuid::new_v4(),"application_id":application["id"],"deployment_id":deployment["id"]}),
        )
        .await;
        assert_eq!(start_status, StatusCode::CREATED, "{instance}");
        let command = exchange(
            &mut node_socket,
            json!({"type":"poll_command","request_id":6 + request_number * 2}),
        )
        .await;
        assert_eq!(command["command"]["action"]["kind"], "start");
        let running = exchange(
            &mut node_socket,
            json!({"type":"acknowledge_command","request_id":7 + request_number * 2,"receipt":{"command_id":command["command"]["id"],"lease_id":command["command"]["lease_id"],"instance_id":command["command"]["instance_id"],"launch_id":command["command"]["launch_id"],"instance_revision":command["command"]["instance_revision"],"outcome":{"result":"running","port":command["command"]["action"]["port"]}}}),
        )
        .await;
        assert_eq!(running["state"], "running");
        running_instances.push((instance, running));
    }
    let mut first_session = Value::Null;
    for (instance_index, (instance, _)) in running_instances.iter().enumerate() {
        let (open_status, session) = resource_call(
            &router,
            "POST",
            "/api/console/resource-sessions",
            "android",
            Some(&user_token),
            Some("user"),
            json!({"request_id":Uuid::new_v4(),"target":{"kind":"cloud_application","application_id":application["id"],"instance_id":instance["id"]},"access":"controller"}),
        )
        .await;
        if instance_index == 0 {
            assert_eq!(open_status, StatusCode::CREATED, "{session}");
            first_session = session;
        } else {
            assert_eq!(open_status, StatusCode::FORBIDDEN, "{session}");
        }
    }
    let (stop_status, stopping) = resource_call(
        &router,
        "POST",
        &format!(
            "/api/console/instances/{}/stop",
            running_instances[0].0["id"].as_str().unwrap()
        ),
        "android",
        Some(&user_token),
        Some("user"),
        json!({"revision":running_instances[0].1["revision"]}),
    )
    .await;
    assert_eq!(stop_status, StatusCode::OK, "{stopping}");
    let stop_command = exchange(
        &mut node_socket,
        json!({"type":"poll_command","request_id":10}),
    )
    .await;
    assert_eq!(stop_command["command"]["action"]["kind"], "stop");
    let stopped = exchange(
        &mut node_socket,
        json!({"type":"acknowledge_command","request_id":11,"receipt":{"command_id":stop_command["command"]["id"],"lease_id":stop_command["command"]["lease_id"],"instance_id":stop_command["command"]["instance_id"],"launch_id":stop_command["command"]["launch_id"],"instance_revision":stop_command["command"]["instance_revision"],"outcome":{"result":"absent"}}}),
    )
    .await;
    assert_eq!(stopped["state"], "stopped");
    let (closed_status, closed_session) = resource_call(
        &router,
        "GET",
        &format!(
            "/api/console/resource-sessions/{}",
            first_session["id"].as_str().unwrap()
        ),
        "android",
        Some(&user_token),
        Some("user"),
        Value::Null,
    )
    .await;
    assert_eq!(closed_status, StatusCode::OK, "{closed_session}");
    assert_eq!(closed_session["state"], "closed");
    let (reopened_status, reopened_session) = resource_call(
        &router,
        "POST",
        "/api/console/resource-sessions",
        "android",
        Some(&user_token),
        Some("user"),
        json!({"request_id":Uuid::new_v4(),"target":{"kind":"cloud_application","application_id":application["id"],"instance_id":running_instances[1].0["id"]},"access":"controller"}),
    )
    .await;
    assert_eq!(reopened_status, StatusCode::CREATED, "{reopened_session}");
    node_socket.close(None).await.unwrap();
    server.abort();
    let _ = server.await;
    runtime.shutdown().await;
}
