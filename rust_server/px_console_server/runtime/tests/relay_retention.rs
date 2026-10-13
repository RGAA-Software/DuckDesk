//! Real loopback Relay and held QUIC connection driven by Console and Service policy.
#[path = "support/runtime_fixture.rs"]
mod fixture;

#[path = "support/relay_distribution.rs"]
mod relay_distribution;

use fixture::{call, login, PASSWORD};
use futures_util::{SinkExt, StreamExt};
use px_relay_server::{
    config::ControlPlaneConfig,
    control,
    iroh::{IrohRelay, IrohRelayConfig},
};
use serde_json::{json, Value};
use std::{
    net::{SocketAddr, UdpSocket},
    path::Path,
    process::Stdio,
    sync::Arc,
    time::Duration,
};
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader, Lines},
    net::TcpListener,
    process::{Child, Command},
};
use tokio_tungstenite::{connect_async, tungstenite::Message};
use tokio_util::sync::CancellationToken;
use zeroize::Zeroizing;

const FIXTURE_KIND: &str = "node_control";
const WAIT: Duration = Duration::from_secs(20);

struct Probe {
    child: Child,
    output: Lines<BufReader<tokio::process::ChildStdout>>,
    errors: std::path::PathBuf,
}

impl Probe {
    fn start(configuration: &Path, errors: &Path) -> Self {
        Self::start_operation(configuration, errors, "retention-self-test")
    }

    fn start_operation(configuration: &Path, errors: &Path, operation: &str) -> Self {
        let mut child = Command::new(
            std::env::var_os("PIXELS_TEST_TRANSPORT_PROBE_BINARY").expect("probe binary missing"),
        )
        .args([operation, "--endpoint-config"])
        .arg(configuration)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(std::fs::File::create(errors).unwrap())
        .kill_on_drop(true)
        .spawn()
        .unwrap();
        let output = BufReader::new(child.stdout.take().unwrap()).lines();
        Self {
            child,
            output,
            errors: errors.to_owned(),
        }
    }

    async fn marker(&mut self, prefix: &str) -> Value {
        tokio::time::timeout(WAIT, async {
            loop {
                let line = self.output.next_line().await.unwrap().unwrap_or_else(|| {
                    panic!(
                        "probe exited before {prefix}: {}",
                        std::fs::read_to_string(&self.errors).unwrap()
                    )
                });
                println!("{line}");
                if let Some(payload) = line.strip_prefix(prefix) {
                    return if payload.is_empty() {
                        Value::Null
                    } else {
                        serde_json::from_str(payload.trim()).unwrap()
                    };
                }
            }
        })
        .await
        .unwrap_or_else(|_| {
            panic!(
                "probe timed out waiting for {prefix}: {}",
                std::fs::read_to_string(&self.errors).unwrap()
            )
        })
    }

    async fn command(&mut self, request: Value, marker: &str) -> Value {
        let bytes = format!("{request}\n");
        self.child
            .stdin
            .as_mut()
            .unwrap()
            .write_all(bytes.as_bytes())
            .await
            .unwrap();
        self.marker(marker).await
    }
}

async fn service_policy(directory: &Path, report: Value, description: &Value) -> Value {
    let input = directory.join("service-policy.json");
    std::fs::write(
        &input,
        json!({"report":report,"description":description}).to_string(),
    )
    .unwrap();
    let errors = directory.join("service-policy-errors.log");
    let mut child = Command::new(
        std::env::var_os("PIXELS_TEST_SERVICE_POLICY_BINARY").expect("Service test binary missing"),
    )
    .args([
        "--ignored",
        "--exact",
        "service_host::tests::console_policy_heartbeat_fixture",
        "--nocapture",
    ])
    .env("PIXELS_TEST_RELAY_POLICY_INPUT", input)
    .stdout(Stdio::piped())
    .stderr(std::fs::File::create(&errors).unwrap())
    .kill_on_drop(true)
    .spawn()
    .unwrap();
    let stdout = child.stdout.take().unwrap();
    let mut lines = BufReader::new(stdout).lines();
    let policy = tokio::time::timeout(WAIT, async {
        while let Some(line) = lines.next_line().await.unwrap() {
            if let Some(payload) = line.strip_prefix("POLICY_HEARTBEAT ") {
                return serde_json::from_str::<Value>(payload).unwrap();
            }
        }
        panic!(
            "Service heartbeat fixture produced no policy: {}",
            std::fs::read_to_string(&errors).unwrap()
        );
    })
    .await
    .unwrap();
    assert!(tokio::time::timeout(WAIT, child.wait())
        .await
        .unwrap()
        .unwrap()
        .success());
    policy
}

async fn relay_profile(
    router: &axum::Router,
    admin: &str,
    relay_id: &str,
    connections: u32,
    draining: bool,
) -> Value {
    tokio::time::timeout(WAIT, async {
        loop {
            let (status, inventory) = call(
                router,
                "GET",
                "/api/console/managed/relays?limit=100",
                "admin_web",
                Some(admin),
                Value::Null,
            )
            .await;
            assert_eq!(status.as_u16(), 200, "{inventory}");
            let profile = inventory
                .as_array()
                .unwrap()
                .iter()
                .find(|profile| profile["id"] == relay_id)
                .unwrap();
            if profile["current_connections"] == connections
                && profile["reported_draining"] == draining
                && profile["state"] == "ready"
            {
                return profile.clone();
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    })
    .await
    .expect("Relay reports did not converge")
}

async fn configure_relay(router: &axum::Router, admin: &str, profile: &Value, draining: bool) {
    configure_relay_bandwidth(
        router,
        admin,
        profile,
        draining,
        profile["bandwidth_capacity_mbps"].clone(),
    )
    .await;
}

async fn configure_relay_bandwidth(
    router: &axum::Router,
    admin: &str,
    profile: &Value,
    draining: bool,
    capacity: Value,
) {
    let (status, response) = call(
        router,
        "PATCH",
        &format!(
            "/api/console/managed/relays/{}",
            profile["id"].as_str().unwrap()
        ),
        "admin_web",
        Some(admin),
        json!({"revision":profile["revision"],"configuration":{"draining":draining,"disabled":false,
            "bandwidth_capacity_mbps":capacity}}),
    )
    .await;
    assert_eq!(status.as_u16(), 200, "{response}");
}

#[tokio::test]
async fn actual_relay_preserves_existing_streams_and_datagrams_when_full_or_draining() {
    run_retention(false).await;
}

#[tokio::test]
async fn actual_relay_bandwidth_filters_candidates_without_disrupting_existing_connections() {
    run_retention(true).await;
}

async fn run_retention(check_bandwidth: bool) {
    let fixture_name = if check_bandwidth {
        "bandwidth"
    } else {
        "retention"
    };
    let runtime = fixture::start_with_iroh().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let directory = tempfile::tempdir().unwrap();
    let certificate = rcgen::generate_simple_self_signed(vec!["127.0.0.1".into()]).unwrap();
    let relay_configuration = IrohRelayConfig {
        https_bind: std::net::TcpListener::bind("127.0.0.1:0")
            .unwrap()
            .local_addr()
            .unwrap(),
        qad_bind: UdpSocket::bind("127.0.0.1:0")
            .unwrap()
            .local_addr()
            .unwrap(),
        qad_public_port: None,
        console_managed: true,
        certificate_file: directory.path().join("relay.pem"),
        private_key_file: directory.path().join("relay-key.pem"),
        max_connections: if check_bandwidth { 16 } else { 2 },
    };
    std::fs::write(
        &relay_configuration.certificate_file,
        certificate.cert.pem(),
    )
    .unwrap();
    std::fs::write(
        &relay_configuration.private_key_file,
        certificate.signing_key.serialize_pem(),
    )
    .unwrap();
    let (status, registration) = call(&router, "POST", "/api/console/managed/relays", "admin_web", Some(&admin),
        json!({"name":format!("real-{fixture_name}-home"),"public_host":"127.0.0.1","public_port":relay_configuration.https_bind.port()})).await;
    assert_eq!(status.as_u16(), 201, "{registration}");
    configure_relay(&router, &admin, &registration["relay"], false).await;
    let relay_id = registration["relay"]["id"].as_str().unwrap();
    let listener = TcpListener::bind("127.0.0.1:0").await.unwrap();
    let address = listener.local_addr().unwrap();
    let stop = CancellationToken::new();
    let server_stop = stop.clone();
    let server_router = router.clone();
    let server = tokio::spawn(async move {
        axum::serve(
            listener,
            server_router.into_make_service_with_connect_info::<SocketAddr>(),
        )
        .with_graceful_shutdown(server_stop.cancelled_owned())
        .await
        .unwrap();
    });
    let relay = IrohRelay::start(&relay_configuration).await.unwrap();
    let control = tokio::spawn(control::run_iroh(
        relay.management(),
        ControlPlaneConfig {
            url: format!("ws://{address}/api/console/relay-control"),
            token: Arc::new(Zeroizing::new(
                registration["relay_token"].as_str().unwrap().to_owned(),
            )),
            product_version_code: 3_002_001,
            tls_config: None,
        },
        stop.clone(),
    ));
    let relay_task = tokio::spawn(relay.run(stop.clone()));
    relay_profile(&router, &admin, relay_id, 0, false).await;
    let (status, device) = call(
        &router,
        "POST",
        "/api/console/managed/devices",
        "admin_web",
        Some(&admin),
        json!({"name":format!("real-{fixture_name}-node"),"platform":"windows"}),
    )
    .await;
    assert_eq!(status.as_u16(), 201);
    let (status, node) = call(
        &router,
        "POST",
        "/api/console/managed/nodes",
        "admin_web",
        Some(&admin),
        json!({"device_id":device["device"]["id"],"product":"cloud_node","max_instances":4}),
    )
    .await;
    assert_eq!(status.as_u16(), 201);
    let (mut socket, _) = connect_async(format!("ws://{address}/api/console/node-control"))
        .await
        .unwrap();
    let authenticated = exchange(
        &mut socket,
        json!({"type":"authenticate","request_id":1,"node_token":node["node_token"]}),
    )
    .await;
    assert_eq!(authenticated["type"], "authenticated");
    let home = json!([{"url":format!("https://{}/", relay_configuration.https_bind),"qad_port":relay_configuration.qad_bind.port()}]);
    let configuration_path = directory.path().join("endpoint.json");
    std::fs::write(
        &configuration_path,
        json!({"relay_only":true,"relays":home,"ca_certificates_pem":[certificate.cert.pem()]})
            .to_string(),
    )
    .unwrap();
    let errors_path = directory.path().join("probe-errors.log");
    let mut probe = Probe::start(&configuration_path, &errors_path);
    let description = probe.marker("RETENTION_READY ").await;
    let mut observations = Vec::new();
    let phases: &[&str] = if check_bandwidth {
        &[]
    } else {
        &["full", "draining"]
    };
    for (phase_index, phase) in phases.iter().enumerate() {
        let full = relay_profile(&router, &admin, relay_id, 2, phase_index > 0).await;
        let mut request = telemetry_report(phase_index as u64 + 2, phase_index as u64 + 1, 100);
        request["report"]["render_iroh_endpoints"] =
            json!([{"port":4601,"instance_id":null,"launch_id":null,"description":description}]);
        let reported = exchange(&mut socket, request).await;
        assert_eq!(reported["type"], "reported", "{reported}");
        assert_eq!(reported["iroh_relays"], json!([]));
        assert_eq!(reported["iroh_retained_relays"], home);
        let policy = service_policy(directory.path(), reported, &description).await;
        assert_eq!(policy["new_endpoint_candidates"], json!([]));
        assert_eq!(policy["relays"], home);
        let observed = probe
            .command(
                json!({"phase":phase,"relays":policy["relays"]}),
                "RETENTION_PHASE ",
            )
            .await;
        assert_eq!(observed["new_endpoint_denied"], true);
        observations.push(observed);
        if phase_index == 0 {
            configure_relay(&router, &admin, &full, true).await;
        } else {
            configure_relay(&router, &admin, &full, false).await;
        }
    }
    if check_bandwidth {
        let profile = relay_profile(&router, &admin, relay_id, 2, false).await;
        configure_relay_bandwidth(&router, &admin, &profile, false, json!(1)).await;
        let load = probe
            .command(json!({"phase":"bandwidth-load"}), "RETENTION_LOAD ")
            .await;
        assert_eq!(load["reliable_bytes_verified"], 2 * 1024 * 1024);
        let busy = wait_bandwidth(&router, &admin, relay_id, true).await;
        assert_eq!(busy["max_connections"], 16);
        assert_eq!(busy["current_connections"], 2);
        let mut request = telemetry_report(2, 1, 100);
        request["report"]["render_iroh_endpoints"] =
            json!([{"port":4601,"instance_id":null,"launch_id":null,"description":description}]);
        let reported = exchange(&mut socket, request).await;
        assert_eq!(reported["type"], "reported", "{reported}");
        assert_eq!(reported["iroh_relays"], json!([]));
        assert_eq!(reported["iroh_retained_relays"], home);
        let policy = service_policy(directory.path(), reported, &description).await;
        assert_eq!(policy["new_endpoint_candidates"], json!([]));
        assert_eq!(policy["relays"], home);
        let held = probe
            .command(
                json!({"phase":"bandwidth","relays":policy["relays"]}),
                "RETENTION_PHASE ",
            )
            .await;
        let idle = wait_bandwidth(&router, &admin, relay_id, false).await;
        let available = exchange(&mut socket, telemetry_report(3, 2, 100)).await;
        assert_eq!(available["iroh_relays"], home);
        observations.push(
            json!({"busy_profile":busy,"held_connection":held,"idle_profile":idle,
            "new_candidates_restored":true,"load":load}),
        );
    }
    relay_profile(&router, &admin, relay_id, 2, false).await;
    probe
        .command(json!({"phase":"release"}), "RETENTION_RELEASED")
        .await;
    relay_profile(&router, &admin, relay_id, 0, false).await;
    let restored = exchange(&mut socket, telemetry_report(4, 3, 100)).await;
    assert_eq!(restored["iroh_relays"], home);
    let restored_policy = service_policy(directory.path(), restored, &description).await;
    assert_eq!(restored_policy["new_endpoint_candidates"], home);
    probe
        .command(json!({"phase":"restore"}), "RETENTION_RESTORED")
        .await;
    assert!(
        tokio::time::timeout(WAIT, probe.child.wait())
            .await
            .unwrap()
            .unwrap()
            .success(),
        "{}",
        std::fs::read_to_string(&errors_path).unwrap()
    );
    relay_profile(&router, &admin, relay_id, 0, false).await;
    socket.close(None).await.unwrap();
    stop.cancel();
    tokio::time::timeout(WAIT, control).await.unwrap().unwrap();
    tokio::time::timeout(WAIT, relay_task)
        .await
        .unwrap()
        .unwrap()
        .unwrap();
    tokio::time::timeout(WAIT, server).await.unwrap().unwrap();
    runtime.shutdown().await;
    let result = json!({"scope":"isolated-loopback-real-relay-console-service-heartbeat","phases":observations,
        "capacity_released":true,"new_endpoint_restored":true,"cleanup_complete":true,"remote_deployment":false});
    if let Some(result_path) = std::env::var_os("PIXELS_TEST_RELAY_RETENTION_RESULT") {
        let result_path = std::path::PathBuf::from(result_path);
        let result_path = if check_bandwidth {
            result_path.with_file_name("bandwidth-result.json")
        } else {
            result_path
        };
        std::fs::write(result_path, serde_json::to_vec_pretty(&result).unwrap()).unwrap();
    }
    println!("RETENTION_RESULT {result}");
}

async fn wait_bandwidth(router: &axum::Router, admin: &str, relay_id: &str, busy: bool) -> Value {
    tokio::time::timeout(WAIT, async {
        loop {
            let profile = relay_profile(router, admin, relay_id, 2, false).await;
            if profile["forwarded_rate_bps"]
                .as_i64()
                .is_some_and(|rate| (rate >= 900_000) == busy)
            {
                return profile;
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    })
    .await
    .expect("actual Relay bandwidth report did not converge")
}

async fn exchange<S>(socket: &mut S, request: Value) -> Value
where
    S: futures_util::Sink<Message, Error = tokio_tungstenite::tungstenite::Error>
        + futures_util::Stream<Item = Result<Message, tokio_tungstenite::tungstenite::Error>>
        + Unpin,
{
    let request_for_error = json!({"type":request["type"], "request_id":request["request_id"]});
    socket
        .send(Message::Text(request.to_string().into()))
        .await
        .unwrap();
    let message = tokio::time::timeout(Duration::from_secs(15), socket.next())
        .await
        .unwrap_or_else(|_| panic!("timed out waiting for node response to {request_for_error}"))
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
            "render_iroh_endpoints":[],
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
