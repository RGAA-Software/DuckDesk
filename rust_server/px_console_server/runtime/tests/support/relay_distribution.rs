//! Mixed Windows/Linux product Relay allocation through actual Console policy.
use super::{
    configure_relay, exchange, fixture, relay_profile, service_policy, telemetry_report, Probe,
    WAIT,
};
use fixture::{call, login, PASSWORD};
use serde_json::{json, Value};
use std::{
    net::{SocketAddr, UdpSocket},
    path::{Path, PathBuf},
    process::Stdio,
    time::{Duration, Instant},
};
use tokio::{net::TcpListener, process::Command};
use tokio_tungstenite::connect_async;
use tokio_util::sync::CancellationToken;
use uuid::Uuid;

struct LinuxRelay {
    docker: PathBuf,
    compose_file: PathBuf,
    project: String,
    active: bool,
}

impl LinuxRelay {
    fn new(directory: &Path, host_port: u16, qad_port: u16, image: &str) -> Self {
        let compose_file = directory.join("compose.json");
        std::fs::write(&compose_file, json!({"services":{"relay":{
            "image":image,"pull_policy":"never","read_only":true,"user":"10001:10001",
            "cap_drop":["ALL"],"security_opt":["no-new-privileges:true"],"stop_grace_period":"10s",
            "env_file":directory.join("relay-control.env"),
            "ports":[format!("127.0.0.1:{host_port}:4605/tcp"),format!("127.0.0.1:{qad_port}:4605/udp")],
            "volumes":[{"type":"bind","source":directory,"target":"/etc/pixels/relay","read_only":true,"bind":{"create_host_path":false}}],
            "tmpfs":["/tmp:size=16m,mode=1777"]
        }}}).to_string()).unwrap();
        Self {
            docker: PathBuf::from(
                std::env::var_os("PIXELS_TEST_DOCKER_BINARY").unwrap_or_else(|| "docker".into()),
            ),
            compose_file,
            project: format!("pixels-relay-distribution-{}", Uuid::new_v4()),
            active: true,
        }
    }

    async fn compose(&self, arguments: &[&str]) -> String {
        let child = Command::new(&self.docker)
            .args(["compose", "--project-name", &self.project, "--file"])
            .arg(&self.compose_file)
            .args(arguments)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true)
            .spawn()
            .unwrap();
        let output = tokio::time::timeout(WAIT, child.wait_with_output())
            .await
            .expect("Compose timed out")
            .unwrap();
        assert!(
            output.status.success(),
            "Compose {:?}: {}",
            arguments,
            String::from_utf8_lossy(&output.stderr)
        );
        String::from_utf8(output.stdout).unwrap()
    }

    async fn shutdown(&mut self) {
        self.compose(&["down", "--timeout", "5"]).await;
        assert!(self
            .compose(&["ps", "--all", "--quiet"])
            .await
            .trim()
            .is_empty());
        self.active = false;
    }
}

impl Drop for LinuxRelay {
    fn drop(&mut self) {
        if !self.active {
            return;
        }
        // Only this randomly named fixture project is eligible for cleanup.
        for arguments in [
            vec!["logs", "--no-color", "--tail", "40"],
            vec!["down", "--timeout", "5"],
        ] {
            if let Ok(mut cleanup) = std::process::Command::new(&self.docker)
                .args(["compose", "--project-name", &self.project, "--file"])
                .arg(&self.compose_file)
                .args(arguments)
                .stdout(Stdio::inherit())
                .stderr(Stdio::inherit())
                .spawn()
            {
                let deadline = Instant::now() + Duration::from_secs(10);
                loop {
                    if cleanup.try_wait().ok().flatten().is_some() {
                        break;
                    }
                    if Instant::now() >= deadline {
                        let _ = cleanup.kill();
                        let _ = cleanup.wait();
                        break;
                    }
                    std::thread::sleep(Duration::from_millis(20));
                }
            }
        }
    }
}

async fn register_relay(router: &axum::Router, admin: &str, name: &str, port: u16) -> Value {
    let (status, registration) = call(
        router,
        "POST",
        "/api/console/managed/relays",
        "admin_web",
        Some(admin),
        json!({"name":name,"public_host":"127.0.0.1","public_port":port}),
    )
    .await;
    assert_eq!(status.as_u16(), 201, "{registration}");
    configure_relay(router, admin, &registration["relay"], false).await;
    registration
}

fn available_port() -> u16 {
    std::net::TcpListener::bind("127.0.0.1:0")
        .unwrap()
        .local_addr()
        .unwrap()
        .port()
}

fn available_qad_port() -> u16 {
    UdpSocket::bind("127.0.0.1:0")
        .unwrap()
        .local_addr()
        .unwrap()
        .port()
}

fn write_relay_configuration(
    directory: &Path,
    https_bind: String,
    qad_bind: String,
    public_qad: u16,
) -> PathBuf {
    let path = directory.join("relay.json");
    std::fs::write(&path, json!({"https_bind":https_bind,"qad_bind":qad_bind,"qad_public_port":public_qad,
        "console_managed":true,"certificate_file":"relay.pem","private_key_file":"relay-key.pem","max_connections":2}).to_string()).unwrap();
    path
}

#[tokio::test]
async fn windows_linux_relays_allocate_new_endpoints_without_disrupting_existing_connections() {
    let runtime = fixture::start_with_iroh().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let directory = tempfile::tempdir().unwrap();
    let linux_directory = directory.path().join("linux");
    let windows_directory = directory.path().join("windows");
    std::fs::create_dir(&linux_directory).unwrap();
    std::fs::create_dir(&windows_directory).unwrap();
    let certificate = rcgen::generate_simple_self_signed(vec!["127.0.0.1".into()]).unwrap();
    for relay_directory in [&linux_directory, &windows_directory] {
        std::fs::write(relay_directory.join("relay.pem"), certificate.cert.pem()).unwrap();
        std::fs::write(
            relay_directory.join("relay-key.pem"),
            certificate.signing_key.serialize_pem(),
        )
        .unwrap();
    }
    let windows_port = available_port();
    let windows_qad = available_qad_port();
    let linux_port = available_port();
    let linux_qad = available_qad_port();
    let windows_registration =
        register_relay(&router, &admin, "isolated-windows", windows_port).await;
    let linux_registration = register_relay(&router, &admin, "isolated-linux", linux_port).await;
    let windows_id = windows_registration["relay"]["id"].as_str().unwrap();
    let linux_id = linux_registration["relay"]["id"].as_str().unwrap();
    let windows_home =
        json!({"url":format!("https://127.0.0.1:{windows_port}/"),"qad_port":windows_qad});
    let linux_home = json!({"url":format!("https://127.0.0.1:{linux_port}/"),"qad_port":linux_qad});
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
    let windows_configuration = write_relay_configuration(
        &windows_directory,
        format!("127.0.0.1:{windows_port}"),
        format!("127.0.0.1:{windows_qad}"),
        windows_qad,
    );
    let windows_log = std::fs::File::create(directory.path().join("windows-relay.log")).unwrap();
    let mut windows_relay = Command::new(
        std::env::var_os("PIXELS_TEST_WINDOWS_RELAY_BINARY").expect("Windows Relay binary missing"),
    )
    .arg("--iroh-config")
    .arg(windows_configuration)
    .env(
        "PIXELS_RELAY_CONSOLE_CONTROL_URL",
        format!("ws://{address}/api/console/relay-control"),
    )
    .env(
        "PIXELS_RELAY_NODE_TOKEN",
        windows_registration["relay_token"].as_str().unwrap(),
    )
    .stdout(windows_log.try_clone().unwrap())
    .stderr(windows_log)
    .kill_on_drop(true)
    .spawn()
    .unwrap();
    write_relay_configuration(
        &linux_directory,
        "0.0.0.0:4605".into(),
        "0.0.0.0:4605".into(),
        linux_qad,
    );
    std::fs::write(linux_directory.join("relay-control.env"), format!(
        "PIXELS_RELAY_CONSOLE_CONTROL_URL=ws://host.docker.internal:{}/api/console/relay-control\nPIXELS_RELAY_NODE_TOKEN={}\n",
        address.port(), linux_registration["relay_token"].as_str().unwrap())).unwrap();
    let linux_image =
        std::env::var("PIXELS_TEST_LINUX_RELAY_IMAGE").expect("Linux Relay image missing");
    let mut linux_relay = LinuxRelay::new(&linux_directory, linux_port, linux_qad, &linux_image);
    linux_relay
        .compose(&["up", "--detach", "--no-build", "--pull", "never"])
        .await;
    let linux_runtime_sha = linux_relay
        .compose(&[
            "exec",
            "-T",
            "relay",
            "sha256sum",
            "/opt/pixels/bin/px_relay",
        ])
        .await;
    assert!(linux_runtime_sha.starts_with(
        &std::env::var("PIXELS_TEST_LINUX_RELAY_SHA256").expect("Linux runtime hash missing")
    ));
    relay_profile(&router, &admin, windows_id, 0, false).await;
    relay_profile(&router, &admin, linux_id, 0, false).await;
    let (status, device) = call(
        &router,
        "POST",
        "/api/console/managed/devices",
        "admin_web",
        Some(&admin),
        json!({"name":"windows-linux-policy-node","platform":"windows"}),
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
    assert_eq!(
        exchange(
            &mut socket,
            json!({"type":"authenticate","request_id":1,"node_token":node["node_token"]})
        )
        .await["type"],
        "authenticated"
    );
    let configuration = directory.path().join("endpoint.json");
    std::fs::write(&configuration, json!({"relay_only":true,"relays":[windows_home],"ca_certificates_pem":[certificate.cert.pem()]}).to_string()).unwrap();
    let errors = directory.path().join("probe-errors.log");
    let mut probe = Probe::start_operation(&configuration, &errors, "distribution-self-test");
    let description = probe.marker("RETENTION_READY ").await;
    let mut observations = Vec::new();
    for (phase_index, phase) in ["full", "draining"].into_iter().enumerate() {
        let windows_profile = relay_profile(&router, &admin, windows_id, 2, phase_index > 0).await;
        relay_profile(&router, &admin, linux_id, 0, false).await;
        let mut report_request =
            telemetry_report(phase_index as u64 + 2, phase_index as u64 + 1, 100);
        report_request["report"]["render_iroh_endpoints"] =
            json!([{"port":4601,"instance_id":null,"launch_id":null,"description":description}]);
        let report = exchange(&mut socket, report_request).await;
        assert_eq!(report["type"], "reported", "{report}");
        assert_eq!(report["iroh_relays"], json!([linux_home]));
        let policy = service_policy(directory.path(), report, &description).await;
        assert_eq!(policy["new_endpoint_candidates"], json!([linux_home]));
        assert!(policy["relays"].as_array().unwrap().contains(&windows_home));
        assert!(policy["relays"].as_array().unwrap().contains(&linux_home));
        let retained = probe
            .command(
                json!({"phase":phase,"relays":policy["relays"]}),
                "RETENTION_PHASE ",
            )
            .await;
        let allocated = probe
            .command(
                json!({"phase":"new-endpoints","relays":policy["new_endpoint_candidates"]}),
                "DISTRIBUTION_READY ",
            )
            .await;
        assert_eq!(
            allocated["homes"],
            json!([linux_home["url"], linux_home["url"]])
        );
        let observed_windows = relay_profile(&router, &admin, windows_id, 2, phase_index > 0).await;
        let observed_linux = relay_profile(&router, &admin, linux_id, 2, false).await;
        observations.push(json!({"phase":phase,"retained":retained,"allocated":allocated,
            "console_windows_connections":observed_windows["current_connections"],"console_linux_connections":observed_linux["current_connections"]}));
        probe
            .command(json!({"phase":"release-new"}), "DISTRIBUTION_RELEASED")
            .await;
        relay_profile(&router, &admin, linux_id, 0, false).await;
        configure_relay(&router, &admin, &windows_profile, phase_index == 0).await;
    }
    relay_profile(&router, &admin, windows_id, 2, false).await;
    probe
        .command(json!({"phase":"release"}), "RETENTION_RELEASED")
        .await;
    relay_profile(&router, &admin, windows_id, 0, false).await;
    let restored = exchange(&mut socket, telemetry_report(4, 3, 100)).await;
    let restored_candidates = restored["iroh_relays"].as_array().unwrap();
    assert_eq!(restored_candidates.len(), 2);
    assert!(
        restored_candidates.contains(&windows_home) && restored_candidates.contains(&linux_home)
    );
    let restored_allocation = probe
        .command(
            json!({"phase":"allocate-restored","relays":restored_candidates}),
            "DISTRIBUTION_RESTORED ",
        )
        .await;
    relay_profile(&router, &admin, windows_id, 2, false).await;
    relay_profile(&router, &admin, linux_id, 2, false).await;
    let full_report = exchange(&mut socket, telemetry_report(5, 4, 100)).await;
    assert_eq!(full_report["iroh_relays"], json!([]));
    probe
        .command(json!({"phase":"finish"}), "DISTRIBUTION_FINISHED")
        .await;
    assert!(
        tokio::time::timeout(WAIT, probe.child.wait())
            .await
            .unwrap()
            .unwrap()
            .success(),
        "{}",
        std::fs::read_to_string(&errors).unwrap()
    );
    relay_profile(&router, &admin, windows_id, 0, false).await;
    relay_profile(&router, &admin, linux_id, 0, false).await;
    socket.close(None).await.unwrap();
    linux_relay.shutdown().await;
    windows_relay.kill().await.unwrap();
    windows_relay.wait().await.unwrap();
    stop.cancel();
    tokio::time::timeout(WAIT, server).await.unwrap().unwrap();
    runtime.shutdown().await;
    let result = json!({"scope":"actual-windows-executable-and-linux-compose-relay-with-console-service-policy",
        "linux_image":linux_image,"linux_runtime_sha256":linux_runtime_sha.split_whitespace().next().unwrap(),
        "phases":observations,"restored_allocation":restored_allocation,"final_connection_counts":[0,0],
        "cleanup_complete":true,"remote_deployment":false});
    std::fs::write(
        std::env::var_os("PIXELS_TEST_RELAY_RETENTION_RESULT").expect("result path missing"),
        serde_json::to_vec_pretty(&result).unwrap(),
    )
    .unwrap();
    println!("DISTRIBUTION_RESULT {result}");
}
