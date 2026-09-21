use px_console_runtime::LicenseLaunchConfig;
use px_console_store::{initialize_administrator, PasswordDigest, Username};
use px_deployment_identity::{
    sign_certificate, DeploymentCertificate, DeploymentKind, DeploymentTrustStore,
};
use px_license::{
    Distribution, Feature, LicensePayload, LicenseSigner, LicenseTrustStore, Mode, Product,
};
use px_pg::{DatabaseConfig, Transport};
use ring::{
    rand::SystemRandom,
    signature::{Ed25519KeyPair, KeyPair},
};
use sha2::{Digest, Sha256};
use std::{
    env,
    io::{Read, Write},
    net::{SocketAddr, TcpListener, TcpStream},
    path::Path,
    process::{Child, Command, Stdio},
    thread,
    time::{Duration, Instant},
};
use uuid::Uuid;
use zeroize::Zeroizing;

struct ChildProcess {
    process: Option<Child>,
}

impl ChildProcess {
    fn process(&mut self) -> &mut Child {
        self.process.as_mut().unwrap()
    }

    fn take(&mut self) -> Child {
        self.process.take().unwrap()
    }
}

impl Drop for ChildProcess {
    fn drop(&mut self) {
        if let Some(process) = &mut self.process {
            let _ = process.kill();
            let _ = process.wait();
        }
    }
}

fn database_url(role: &str) -> String {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let mut url =
        url::Url::parse(&env::var(format!("PIXELS_TEST_CONSOLE_{role}_URL")).unwrap()).unwrap();
    let platform = if cfg!(windows) { "windows" } else { "linux" };
    url.set_path(&format!("/pixels_console_process_{platform}"));
    url.to_string()
}

fn unused_loopback_address() -> SocketAddr {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    listener.local_addr().unwrap()
}

fn restrict_private_directory(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o700)).unwrap();
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        let current_identity = Command::new("whoami")
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(current_identity.status.success());
        let identity_access = format!(
            "{}:(OI)(CI)F",
            String::from_utf8(current_identity.stdout).unwrap().trim()
        );
        let access_result = Command::new("icacls")
            .arg(path)
            .args([
                "/inheritance:r",
                "/grant:r",
                &identity_access,
                "*S-1-5-18:(OI)(CI)F",
            ])
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(access_result.status.success());
    }
}

fn deployment_identity_files(
    private_directory: &Path,
    deployment: Uuid,
) -> (std::path::PathBuf, std::path::PathBuf, std::path::PathBuf) {
    let random = SystemRandom::new();
    let vendor_pkcs8 = Ed25519KeyPair::generate_pkcs8(&random).unwrap();
    let vendor_signer = Ed25519KeyPair::from_pkcs8(vendor_pkcs8.as_ref()).unwrap();
    let deployment_pkcs8 = Ed25519KeyPair::generate_pkcs8(&random).unwrap();
    let deployment_signer = Ed25519KeyPair::from_pkcs8(deployment_pkcs8.as_ref()).unwrap();
    let vendor_public_key: [u8; 32] = vendor_signer.public_key().as_ref().try_into().unwrap();
    let deployment_public_key: [u8; 32] =
        deployment_signer.public_key().as_ref().try_into().unwrap();
    let now = chrono::Utc::now().timestamp();
    let certificate = DeploymentCertificate {
        schema_version: 1,
        deployment_id: deployment,
        deployment_kind: DeploymentKind::Private,
        deployment_public_key_hex: hex::encode(deployment_public_key),
        certificate_version: 1,
        not_before: now - 60,
        expires_at: now + 3600,
        issuer_key_id: hex::encode(Sha256::digest(vendor_public_key)),
    };
    let certificate_path = private_directory.join("deployment.cert");
    let signing_key_path = private_directory.join("deployment.pk8");
    let trust_store_path = private_directory.join("deployment-trust.json");
    px_private_files::private::create_private(
        &certificate_path,
        sign_certificate(vendor_pkcs8.as_ref(), &certificate)
            .unwrap()
            .as_bytes(),
    )
    .unwrap();
    px_private_files::private::create_private(&signing_key_path, deployment_pkcs8.as_ref())
        .unwrap();
    px_private_files::private::create_private(
        &trust_store_path,
        &DeploymentTrustStore::new(1, [vendor_public_key])
            .unwrap()
            .canonical_bytes()
            .unwrap(),
    )
    .unwrap();
    (certificate_path, signing_key_path, trust_store_path)
}

fn http_response(address: SocketAddr, path: &str) -> Option<String> {
    let Ok(mut stream) = TcpStream::connect_timeout(&address, Duration::from_millis(250)) else {
        return None;
    };
    stream
        .set_read_timeout(Some(Duration::from_secs(2)))
        .unwrap();
    write!(
        stream,
        "GET {path} HTTP/1.1\r\nHost: {address}\r\nOrigin: http://{address}\r\nX-Pixels-Client-Type: admin_web\r\nConnection: close\r\n\r\n"
    )
    .unwrap();
    let mut response = String::new();
    stream.read_to_string(&mut response).unwrap();
    Some(response)
}

fn ready(address: SocketAddr) -> bool {
    http_response(address, "/health/ready")
        .is_some_and(|response| response.starts_with("HTTP/1.1 204"))
}

fn wait_until_ready(process: &mut ChildProcess, address: SocketAddr) {
    let deadline = Instant::now() + Duration::from_secs(20);
    while Instant::now() < deadline {
        if let Some(status) = process.process().try_wait().unwrap() {
            let mut stderr = String::new();
            process
                .process()
                .stderr
                .as_mut()
                .unwrap()
                .read_to_string(&mut stderr)
                .unwrap();
            panic!("Console exited before readiness ({status}): {stderr}");
        }
        if ready(address) {
            return;
        }
        thread::sleep(Duration::from_millis(50));
    }
    panic!("Console process did not become ready");
}

fn wait_for_exit(process: &mut ChildProcess) {
    let deadline = Instant::now() + Duration::from_secs(15);
    while Instant::now() < deadline {
        if process.process().try_wait().unwrap().is_some() {
            return;
        }
        thread::sleep(Duration::from_millis(50));
    }
    panic!("Console process did not exit after losing database authority");
}

#[tokio::test]
async fn native_process_starts_serves_and_exits_after_database_authority_loss() {
    let deployment = env::var("PIXELS_DEPLOYMENT_ID")
        .unwrap()
        .parse::<Uuid>()
        .unwrap();
    let owner_url = database_url("OWNER");
    let owner_config = DatabaseConfig::parse(&owner_url, Transport::LocalDevelopment).unwrap();
    let password = Zeroizing::new("synthetic-console-process-password".to_string());
    let password_hash = px_credentials::hash(&password).unwrap();
    initialize_administrator(
        &owner_config,
        deployment,
        &Username::parse("process-admin").unwrap(),
        &PasswordDigest::parse(password_hash.to_string()).unwrap(),
    )
    .await
    .unwrap();

    let private_directory = tempfile::tempdir().unwrap();
    restrict_private_directory(private_directory.path());
    let guest_key_path = private_directory.path().join("guest-source.key");
    let workspace_key_path = private_directory.path().join("workspace.key");
    let static_directory = private_directory.path().join("web");
    let recording_cache_directory = private_directory.path().join("recording-cache");
    let license_state_directory = private_directory.path().join("license-state");
    let (deployment_certificate_path, deployment_signing_key_path, deployment_trust_path) =
        deployment_identity_files(private_directory.path(), deployment);
    std::fs::create_dir(&static_directory).unwrap();
    std::fs::create_dir(&recording_cache_directory).unwrap();
    std::fs::create_dir(&license_state_directory).unwrap();
    std::fs::write(
        static_directory.join("index.html"),
        "pixels-console-process",
    )
    .unwrap();
    std::fs::write(static_directory.join("app.js"), "pixels-console-script").unwrap();
    px_private_files::private::create_private(&guest_key_path, &[41; 32]).unwrap();
    px_private_files::private::create_private(&workspace_key_path, &[42; 32]).unwrap();
    let signer = LicenseSigner::from_pkcs8(
        &hex::decode("3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a").unwrap(),
    )
    .unwrap();
    let authority_deployment = Uuid::new_v4();
    let trust_store = LicenseTrustStore::new(
        authority_deployment,
        Uuid::new_v4(),
        signer.public_key().try_into().unwrap(),
        [],
    )
    .unwrap();
    let license_trust_path = private_directory.path().join("license-trust.json");
    let license_path = private_directory.path().join("console.license");
    px_private_files::private::create_private(
        &license_trust_path,
        &trust_store.canonical_bytes().unwrap(),
    )
    .unwrap();
    let current_time = chrono::Utc::now().timestamp();
    let license = LicensePayload {
        schema: 1,
        license_id: Uuid::new_v4(),
        deployment_id: deployment,
        product: Product::PixelsConsole,
        distribution: Distribution::Customer,
        machine_sha256: "a".repeat(64),
        revision: 1,
        mode: Mode::Licensed,
        issued_at: current_time - 10,
        not_before: current_time - 10,
        expires_at: current_time + 3600,
        max_devices: 4,
        max_sessions: 8,
        features: vec![Feature::CloudApplications, Feature::Desktop, Feature::Rdp],
        key_id: signer.key_id(),
    };
    px_private_files::private::create_private(
        &license_path,
        signer.sign(&license).unwrap().as_bytes(),
    )
    .unwrap();
    px_private_files::private::verify_private_directory(&license_state_directory).unwrap();
    let trust_bytes =
        px_private_files::private::read_private_bounded(&license_trust_path, 65536).unwrap();
    let parsed_trust = LicenseTrustStore::from_canonical_bytes(&trust_bytes).unwrap();
    let wire_bytes = px_private_files::private::read_private_bounded(&license_path, 8192).unwrap();
    let wire = std::str::from_utf8(&wire_bytes).unwrap();
    parsed_trust
        .verifier_set()
        .unwrap()
        .verify(
            wire,
            &px_license::VerifyContext {
                deployment_id: deployment,
                product: Product::PixelsConsole,
                distribution: Distribution::Customer,
                machine_sha256: &"a".repeat(64),
                now: chrono::Utc::now().timestamp(),
                minimum_revision: 1,
                last_trusted_time: 0,
            },
        )
        .unwrap();
    LicenseLaunchConfig::new(
        "customer",
        "a".repeat(64),
        authority_deployment,
        license_trust_path.clone(),
        license_path.clone(),
        license_state_directory.clone(),
        None,
        None,
        true,
    )
    .unwrap()
    .admit(deployment)
    .await
    .unwrap();
    drop(px_private_files::CacheRoot::initialize(&recording_cache_directory, deployment).unwrap());
    let workspace_key_id = Uuid::new_v4();
    let address = unused_loopback_address();
    let workspace_keys = serde_json::json!([{
        "id": workspace_key_id,
        "path": workspace_key_path,
    }]);

    let mut command = Command::new(env!("CARGO_BIN_EXE_px_console"));
    command
        .env("PIXELS_CONSOLE_LOCAL_DEVELOPMENT", "1")
        .env("PIXELS_DEPLOYMENT_ID", deployment.to_string())
        .env("PIXELS_CONSOLE_DATABASE_URL", database_url("RUNTIME"))
        .env("PIXELS_CONSOLE_LISTEN", address.to_string())
        .env("PIXELS_CONSOLE_STATIC_DIRECTORY", &static_directory)
        .env("PIXELS_CONSOLE_PUBLIC_ORIGIN", format!("http://{address}"))
        .env("PIXELS_CONSOLE_REGISTRATION", "1")
        .env("PIXELS_CONSOLE_GUESTS", "1")
        .env("PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS", "3600")
        .env("PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS", "3600")
        .env("PIXELS_CONSOLE_GUEST_SOURCE_KEY", &guest_key_path)
        .env(
            "PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY",
            workspace_key_id.to_string(),
        )
        .env("PIXELS_CONSOLE_WORKSPACE_KEYS", workspace_keys.to_string())
        .env(
            "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY",
            &recording_cache_directory,
        )
        .env("PIXELS_CONSOLE_RECORDING_CACHE_BYTES", "1073741824")
        .env("PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS", "4")
        .env("PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS", "86400")
        .env("PIXELS_CONSOLE_DISTRIBUTION", "customer")
        .env("PIXELS_CONSOLE_MACHINE_SHA256", "a".repeat(64))
        .env(
            "PIXELS_CONSOLE_LICENSE_AUTHORITY_DEPLOYMENT_ID",
            authority_deployment.to_string(),
        )
        .env("PIXELS_CONSOLE_LICENSE_TRUST_STORE", &license_trust_path)
        .env("PIXELS_CONSOLE_LICENSE_FILE", &license_path)
        .env(
            "PIXELS_CONSOLE_LICENSE_STATE_DIRECTORY",
            &license_state_directory,
        )
        .env(
            "PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE",
            &deployment_certificate_path,
        )
        .env(
            "PIXELS_CONSOLE_DEPLOYMENT_SIGNING_KEY",
            &deployment_signing_key_path,
        )
        .env(
            "PIXELS_CONSOLE_DEPLOYMENT_TRUST_STORE",
            &deployment_trust_path,
        )
        .env("PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE_VERSION", "1")
        .env("PIXELS_CONSOLE_DESCRIPTOR_REVISION", "1")
        .env("PIXELS_CONSOLE_DEPLOYMENT_TRUST_EPOCH", "1")
        .env("PIXELS_CONSOLE_MINIMUM_CLIENT_BUILD", "1")
        .env_remove("PIXELS_CONSOLE_AUTH_VERIFY_URL")
        .env_remove("PIXELS_CONSOLE_TLS_CERT")
        .env_remove("PIXELS_CONSOLE_TLS_KEY")
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::piped());
    let mut process = ChildProcess {
        process: Some(command.spawn().unwrap()),
    };
    wait_until_ready(&mut process, address);
    let index_response = http_response(address, "/settings/profile").unwrap();
    assert!(index_response.starts_with("HTTP/1.1 200"));
    assert!(index_response.contains("content-type: text/html; charset=utf-8"));
    assert!(index_response.ends_with("pixels-console-process"));
    let asset_response = http_response(address, "/app.js").unwrap();
    assert!(asset_response.starts_with("HTTP/1.1 200"));
    assert!(asset_response.contains("content-type: text/javascript; charset=utf-8"));
    assert!(asset_response.ends_with("pixels-console-script"));
    assert!(http_response(address, "/api/retired")
        .unwrap()
        .starts_with("HTTP/1.1 404"));

    let container = env::var("PIXELS_TEST_CONTAINER").unwrap();
    assert!(
        container.len() >= 12
            && container.len() <= 64
            && container
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    );
    let database = format!(
        "pixels_console_process_{}",
        if cfg!(windows) { "windows" } else { "linux" }
    );
    let terminated = Command::new("docker")
        .args([
            "exec",
            &container,
            "psql",
            "-X",
            "-v",
            "ON_ERROR_STOP=1",
            "-U",
            "pixels_admin",
            "-d",
            "postgres",
            "-c",
            &format!(
                "SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE datname='{database}' AND usename='pixels_console_runtime'"
            ),
        ])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .unwrap();
    assert!(terminated.success());
    wait_for_exit(&mut process);
    let output = process.take().wait_with_output().unwrap();
    assert!(!output.status.success());
    let stderr = String::from_utf8(output.stderr).unwrap();
    assert!(stderr.contains("runtime authority was lost"));
    assert!(!stderr.contains(password.as_str()));
}
