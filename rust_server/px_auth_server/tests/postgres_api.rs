use axum::{
    body::{to_bytes, Body},
    extract::ConnectInfo,
    http::{Request, StatusCode},
    Router,
};
use px_auth_server::{router, AppState};
use px_credentials as credentials;
use px_license::{LicenseSigner, LicenseTrustStore};
use px_pg::{DatabaseConfig, Transport};
use serde_json::{json, Value};
use std::{env, net::SocketAddr, path::PathBuf, sync::Arc};
use tower::ServiceExt;
use uuid::Uuid;

const PASSWORD: &str = "synthetic-auth-password";
struct ServerProcess(std::process::Child);
impl Drop for ServerProcess {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

#[test]
fn native_process_starts_serves_and_rejects_unsafe_configuration() {
    use std::{
        io::{BufRead, BufReader, Read, Write},
        process::{Command, Stdio},
        time::Duration,
    };
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let files = PrivateFiles::new();
    let signer = LicenseSigner::from_pkcs8(&hex::decode(TEST_KEY).unwrap()).unwrap();
    files.write_trust_store("trust-store.json", signer.public_key().try_into().unwrap());
    files.write_trust_store("wrong-key-trust-store.json", [2; 32]);
    let command = || {
        let mut command = Command::new(env!("CARGO_BIN_EXE_px_auth"));
        command
            .env(
                "PIXELS_DATABASE_URL",
                env::var("PIXELS_TEST_AUTH_RUNTIME_URL").unwrap(),
            )
            .env("PIXELS_AUTH_LOCAL_DEVELOPMENT", "1")
            .env("PIXELS_AUTH_LISTEN", "127.0.0.1:0")
            .env(
                "PIXELS_AUTH_TRUST_STORE",
                files.path.join("trust-store.json"),
            )
            .env(
                "PIXELS_AUTH_STATIC_DIRECTORY",
                PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/static"),
            )
            .env("PIXELS_AUTH_SIGNING_KEY", files.path.join("signing.der"))
            .env_remove("PIXELS_AUTH_TLS_CERT")
            .env_remove("PIXELS_AUTH_TLS_KEY")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000);
        }
        command
    };
    let mut server = ServerProcess(command().spawn().unwrap());
    let stdout = server.0.stdout.take().unwrap();
    let (sender, receiver) = std::sync::mpsc::channel();
    let reader = std::thread::spawn(move || {
        let line = BufReader::new(stdout).lines().next();
        let _ = sender.send(line);
    });
    let startup_line = receiver.recv_timeout(Duration::from_secs(20));
    let line = match startup_line {
        Ok(Some(Ok(line))) => line,
        unexpected => {
            let _ = server.0.kill();
            let status = server.0.wait();
            let mut stderr = String::new();
            if let Some(mut stream) = server.0.stderr.take() {
                let _ = stream.read_to_string(&mut stderr);
            }
            reader.join().unwrap();
            panic!(
                "Auth did not report its listener: result={unexpected:?}, status={status:?}, stderr={stderr}"
            );
        }
    };
    reader.join().unwrap();
    let address = line
        .strip_prefix("Auth listening http://")
        .unwrap()
        .split_whitespace()
        .next()
        .unwrap();
    for (path, status) in [
        ("/health/ready", "204"),
        ("/api/v1/verify/author", "404"),
        ("/", "200"),
    ] {
        let mut stream = std::net::TcpStream::connect(address).unwrap();
        stream
            .set_read_timeout(Some(Duration::from_secs(10)))
            .unwrap();
        stream
            .set_write_timeout(Some(Duration::from_secs(5)))
            .unwrap();
        write!(
            stream,
            "GET {path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
        )
        .unwrap();
        let mut response = String::new();
        stream.read_to_string(&mut response).unwrap();
        assert!(
            response.starts_with(&format!("HTTP/1.1 {status}")),
            "unexpected HTTP status"
        );
    }
    drop(server);
    // Refuse unsafe configuration or an owner DSN before binding; kill/reap if not.
    let owner_dsn = env::var("PIXELS_TEST_AUTH_OWNER_URL").unwrap();
    for (key, value) in [
        ("PIXELS_AUTH_LISTEN", "0.0.0.0:0".to_owned()),
        ("PIXELS_AUTH_TRUST_STORE", "invalid".to_owned()),
        (
            "PIXELS_AUTH_TRUST_STORE",
            files
                .path
                .join("wrong-key-trust-store.json")
                .to_string_lossy()
                .into_owned(),
        ),
        (
            "PIXELS_AUTH_SIGNING_KEY",
            "nonexistent-signing-key".to_owned(),
        ),
        ("PIXELS_AUTH_LOCAL_DEVELOPMENT", "0".to_owned()),
        ("PIXELS_DATABASE_URL", owner_dsn),
        (
            "PIXELS_DEPLOYMENT_ID",
            "00000000-0000-0000-0000-000000000000".to_owned(),
        ),
    ] {
        let mut invalid = ServerProcess(command().env(key, &value).spawn().unwrap());
        let deadline = std::time::Instant::now() + Duration::from_secs(10);
        loop {
            if let Some(status) = invalid.0.try_wait().unwrap() {
                assert!(!status.success());
                break;
            }
            assert!(
                std::time::Instant::now() < deadline,
                "invalid configuration did not terminate"
            );
            std::thread::sleep(Duration::from_millis(20));
        }
    }
}
const TEST_KEY: &str="3053020101300506032b6570042204209d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60a123032100d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
struct PrivateFiles {
    path: PathBuf,
}
impl PrivateFiles {
    fn new() -> Self {
        let path = env::temp_dir().join(format!("pixels-auth-fixture-{}", Uuid::new_v4()));
        std::fs::create_dir(&path).unwrap();
        let files = Self { path };
        #[cfg(windows)]
        {
            use std::{os::windows::process::CommandExt, process::Command};
            let user = Command::new("whoami")
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(user.status.success());
            let grant = format!(
                "{}:(OI)(CI)F",
                String::from_utf8(user.stdout).unwrap().trim()
            );
            let result = Command::new("icacls")
                .arg(&files.path)
                .args(["/inheritance:r", "/grant:r", &grant, "*S-1-5-18:(OI)(CI)F"])
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(result.status.success());
        }
        std::fs::write(
            files.path.join("signing.der"),
            hex::decode(TEST_KEY).unwrap(),
        )
        .unwrap();
        std::fs::write(files.path.join("password.txt"), PASSWORD).unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(&files.path, std::fs::Permissions::from_mode(0o700)).unwrap();
            for name in ["signing.der", "password.txt"] {
                std::fs::set_permissions(
                    files.path.join(name),
                    std::fs::Permissions::from_mode(0o600),
                )
                .unwrap();
            }
        }
        files
    }

    fn write_trust_store(&self, file_name: &str, public_key: [u8; 32]) {
        let trust_store = LicenseTrustStore::new(public_key, []).unwrap();
        std::fs::write(
            self.path.join(file_name),
            trust_store.canonical_bytes().unwrap(),
        )
        .unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(
                self.path.join(file_name),
                std::fs::Permissions::from_mode(0o600),
            )
            .unwrap();
        }
    }
}
impl Drop for PrivateFiles {
    fn drop(&mut self) {
        for name in [
            "signing.der",
            "trust-store.json",
            "wrong-generation-trust-store.json",
            "password.txt",
            "generated.der",
            "generated-trust-store.json",
        ] {
            let _ = std::fs::remove_file(self.path.join(name));
        }
        let _ = std::fs::remove_dir(&self.path);
    }
}
struct Fixture {
    app: Router,
    state: Arc<AppState>,
    owner: sqlx::PgPool,
    user: Uuid,
    username: String,
}
#[test]
fn explicit_key_generation_is_private_and_never_overwrites() {
    use std::process::{Command, Stdio};
    let files = PrivateFiles::new();
    let destination = files.path.join("generated.der");
    let command = || {
        let mut command = Command::new(env!("CARGO_BIN_EXE_px_auth_admin"));
        command
            .arg("generate-key")
            .env("PIXELS_AUTH_SIGNING_KEY", &destination)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000);
        }
        command
    };
    let generated = command().output().unwrap();
    assert!(
        generated.status.success(),
        "explicit key provisioning failed"
    );
    let bytes = px_private_files::private::read_private(&destination).unwrap();
    let signer = LicenseSigner::from_pkcs8(&bytes).unwrap();
    let output = String::from_utf8(generated.stdout).unwrap();
    assert!(output.contains(&format!("key_id={}", signer.key_id())));
    assert!(output.contains(&format!("public_key={}", hex::encode(signer.public_key()))));
    assert!(!output.contains(&hex::encode(bytes.as_slice())));
    assert!(!command().output().unwrap().status.success());
    assert_eq!(
        px_private_files::private::read_private(&destination)
            .unwrap()
            .as_slice(),
        bytes.as_slice()
    );
}
#[test]
fn explicit_trust_store_creation_supports_rotation_and_never_overwrites() {
    use std::process::{Command, Stdio};
    let files = PrivateFiles::new();
    let destination = files.path.join("generated-trust-store.json");
    let additional_public_key = "2a".repeat(32);
    let command = || {
        let mut command = Command::new(env!("CARGO_BIN_EXE_px_auth_admin"));
        command
            .arg("create-trust-store")
            .env("PIXELS_AUTH_SIGNING_KEY", files.path.join("signing.der"))
            .env("PIXELS_AUTH_TRUST_STORE", &destination)
            .env("PIXELS_AUTH_ADDITIONAL_PUBLIC_KEYS", &additional_public_key)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000);
        }
        command
    };
    let created = command().output().unwrap();
    assert!(
        created.status.success(),
        "explicit trust-store provisioning failed: {}",
        String::from_utf8_lossy(&created.stderr)
    );
    let trust_store = LicenseTrustStore::from_canonical_bytes(
        &px_private_files::private::read_private(&destination).unwrap(),
    )
    .unwrap();
    let signer = LicenseSigner::from_pkcs8(
        &px_private_files::private::read_private(&files.path.join("signing.der")).unwrap(),
    )
    .unwrap();
    assert_eq!(trust_store.trusted_keys.len(), 2);
    trust_store.verify_active_signer(&signer).unwrap();
    let output = String::from_utf8(created.stdout).unwrap();
    assert!(output.contains(&format!("active_key_id={}", signer.key_id())));
    assert!(output.contains("trusted_key_count=2"));
    assert!(!command().output().unwrap().status.success());
    assert_eq!(
        LicenseTrustStore::from_canonical_bytes(
            &px_private_files::private::read_private(&destination).unwrap()
        )
        .unwrap(),
        trust_store
    );
}
#[test]
fn private_file_rejects_broad_permissions_and_missing_material() {
    let files = PrivateFiles::new();
    let key = files.path.join("signing.der");
    assert!(px_private_files::private::read_private(&key).is_ok());
    assert!(px_private_files::private::read_private(&files.path.join("missing.der")).is_err());
    #[cfg(windows)]
    {
        use std::{os::windows::process::CommandExt, process::Command};
        let result = Command::new("icacls")
            .arg(&key)
            .args(["/grant", "*S-1-1-0:R"])
            .creation_flags(0x08000000)
            .output()
            .unwrap();
        assert!(result.status.success());
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&key, std::fs::Permissions::from_mode(0o644)).unwrap();
    }
    assert!(px_private_files::private::read_private(&key).is_err());
}
#[tokio::test]
async fn bootstrap_cli_is_owner_only_empty_only_and_concurrent_safe() {
    use std::{
        process::{Command, Stdio},
        time::{Duration, Instant},
    };
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let database = if cfg!(windows) {
        "pixels_auth_bootstrap_windows"
    } else {
        "pixels_auth_bootstrap_linux"
    };
    let dsn = |role: &str| {
        let source = env::var(format!("PIXELS_TEST_AUTH_{role}_URL")).unwrap();
        format!("{}/{database}", source.rsplit_once('/').unwrap().0)
    };
    let files = PrivateFiles::new();
    let command = |role: &str| {
        let mut command = Command::new(env!("CARGO_BIN_EXE_px_auth_admin"));
        command
            .arg("bootstrap")
            .env("PIXELS_DATABASE_URL", dsn(role))
            .env("PIXELS_AUTH_LOCAL_DEVELOPMENT", "1")
            .env("PIXELS_AUTH_INITIAL_USERNAME", "bootstrap-admin")
            .env(
                "PIXELS_AUTH_INITIAL_PASSWORD_FILE",
                files.path.join("password.txt"),
            )
            .stdout(Stdio::null())
            .stderr(Stdio::null());
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            command.creation_flags(0x08000000);
        }
        command
    };
    let wait = |mut process: ServerProcess| {
        let deadline = Instant::now() + Duration::from_secs(20);
        loop {
            if let Some(status) = process.0.try_wait().unwrap() {
                return status.success();
            }
            assert!(Instant::now() < deadline, "bootstrap exceeded deadline");
            std::thread::sleep(Duration::from_millis(20));
        }
    };
    assert!(!wait(ServerProcess(command("RUNTIME").spawn().unwrap())));
    let first = ServerProcess(command("OWNER").spawn().unwrap());
    let second = ServerProcess(command("OWNER").spawn().unwrap());
    assert_ne!(wait(first), wait(second));
    assert!(!wait(ServerProcess(command("OWNER").spawn().unwrap())));
    let owner = DatabaseConfig::parse(&dsn("OWNER"), Transport::LocalDevelopment)
        .unwrap()
        .connect()
        .await
        .unwrap();
    assert_eq!(
        sqlx::query_scalar::<_, i64>("SELECT count(*) FROM pixels.authors")
            .fetch_one(&owner)
            .await
            .unwrap(),
        1
    );
    let (hash, revision): (String, i64) =
        sqlx::query_as("SELECT password_hash,authorization_revision FROM pixels.authors")
            .fetch_one(&owner)
            .await
            .unwrap();
    assert!(credentials::verify(PASSWORD, &hash));
    assert_eq!(revision, 1);
    owner.close().await;
}
impl Fixture {
    async fn new(role: &str) -> Self {
        assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
        let config = DatabaseConfig::parse(
            &env::var("PIXELS_TEST_AUTH_RUNTIME_URL").unwrap(),
            Transport::LocalDevelopment,
        )
        .unwrap();
        let owner = DatabaseConfig::parse(
            &env::var("PIXELS_TEST_AUTH_OWNER_URL").unwrap(),
            Transport::LocalDevelopment,
        )
        .unwrap()
        .connect()
        .await
        .unwrap();
        let state = Arc::new(
            AppState::from_signer(
                &config,
                env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
                Arc::new(LicenseSigner::from_pkcs8(&hex::decode(TEST_KEY).unwrap()).unwrap()),
            )
            .await
            .unwrap(),
        );
        let app = router(
            state.clone(),
            &PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/static"),
        );
        let user = Uuid::new_v4();
        let username = user.to_string();
        let encoded = credentials::hash(PASSWORD).unwrap();
        sqlx::query("INSERT INTO pixels.authors(id,username_normalized,password_hash,role) VALUES($1,$2,$3,$4)").bind(user).bind(&username).bind(encoded.as_str()).bind(role).execute(&owner).await.unwrap();
        Self {
            app,
            state,
            owner,
            user,
            username,
        }
    }
    async fn login(&self) -> String {
        let response = call(
            &self.app,
            "POST",
            "/api/auth/sessions",
            None,
            json!({"username":self.username,"password":PASSWORD}),
        )
        .await;
        assert_eq!(response.0, StatusCode::OK);
        response.1["token"].as_str().unwrap().to_owned()
    }
    async fn close(self) {
        self.state.close().await;
        self.owner.close().await;
    }
}
async fn call(
    app: &Router,
    method: &str,
    path: &str,
    token: Option<&str>,
    body: Value,
) -> (StatusCode, Value) {
    let mut request = Request::builder()
        .method(method)
        .uri(path)
        .header("content-type", "application/json")
        .extension(ConnectInfo(
            "127.0.0.1:33000".parse::<SocketAddr>().unwrap(),
        ));
    if let Some(token) = token {
        request = request.header("authorization", format!("Bearer {token}"));
    }
    let response = app
        .clone()
        .oneshot(
            request
                .body(if body.is_null() {
                    Body::empty()
                } else {
                    Body::from(body.to_string())
                })
                .unwrap(),
        )
        .await
        .unwrap();
    let status = response.status();
    assert_eq!(response.headers().get("cache-control").unwrap(), "no-store");
    let bytes = to_bytes(response.into_body(), 65536).await.unwrap();
    let value = serde_json::from_slice(&bytes).unwrap_or(Value::Null);
    (status, value)
}
#[tokio::test]
async fn login_logout_password_reset_and_session_expiry_are_database_authoritative() {
    let fixture = Fixture::new("admin").await;
    let rejected = call(
        &fixture.app,
        "POST",
        "/api/auth/sessions",
        None,
        json!({"username":fixture.username,"password":"incorrect-password"}),
    )
    .await;
    let unknown = call(
        &fixture.app,
        "POST",
        "/api/auth/sessions",
        None,
        json!({"username":"unknown-operator","password":"incorrect-password"}),
    )
    .await;
    assert_eq!(rejected, unknown);
    assert_eq!(rejected.0, StatusCode::UNAUTHORIZED);
    let token = fixture.login().await;
    let me = call(
        &fixture.app,
        "GET",
        "/api/auth/me",
        Some(&token),
        Value::Null,
    )
    .await;
    assert_eq!(me.1["id"], fixture.user.to_string());
    assert!(me.1.get("password_hash").is_none());
    assert_eq!(
        call(
            &fixture.app,
            "DELETE",
            "/api/auth/session",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::NO_CONTENT
    );
    assert_eq!(
        call(
            &fixture.app,
            "GET",
            "/api/auth/me",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::UNAUTHORIZED
    );
    let token = fixture.login().await;
    let changed = call(
        &fixture.app,
        "PATCH",
        &format!("/api/auth/authors/{}/password", fixture.user),
        Some(&token),
        json!({"expected_revision":1,"password":"synthetic-new-password"}),
    )
    .await;
    assert_eq!(changed.0, StatusCode::NO_CONTENT);
    assert_eq!(
        call(
            &fixture.app,
            "GET",
            "/api/auth/me",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::UNAUTHORIZED
    );
    assert_eq!(
        call(
            &fixture.app,
            "POST",
            "/api/auth/sessions",
            None,
            json!({"username":fixture.username,"password":PASSWORD})
        )
        .await
        .0,
        StatusCode::UNAUTHORIZED
    );
    let token = call(
        &fixture.app,
        "POST",
        "/api/auth/sessions",
        None,
        json!({"username":fixture.username,"password":"synthetic-new-password"}),
    )
    .await
    .1["token"]
        .as_str()
        .unwrap()
        .to_owned();
    sqlx::query("UPDATE pixels.author_sessions SET created_at=clock_timestamp()-interval '10 hours',expires_at=clock_timestamp()-interval '2 hours' WHERE author_id=$1").bind(fixture.user).execute(&fixture.owner).await.unwrap();
    assert_eq!(
        call(
            &fixture.app,
            "GET",
            "/api/auth/me",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::UNAUTHORIZED
    );
    fixture.close().await;
}
#[tokio::test]
async fn admin_create_list_and_visitor_denials_have_no_hidden_write() {
    let fixture = Fixture::new("admin").await;
    let token = fixture.login().await;
    let username = Uuid::new_v4().to_string();
    let created = call(
        &fixture.app,
        "POST",
        "/api/auth/authors",
        Some(&token),
        json!({"username":username,"password":PASSWORD,"role":"visitor"}),
    )
    .await;
    assert_eq!(created.0, StatusCode::CREATED);
    assert_eq!(created.1["authorization_revision"], 1);
    let visitor = call(
        &fixture.app,
        "POST",
        "/api/auth/sessions",
        None,
        json!({"username":username,"password":PASSWORD}),
    )
    .await
    .1["token"]
        .as_str()
        .unwrap()
        .to_owned();
    let before: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.customers")
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    assert_eq!(
        call(
            &fixture.app,
            "POST",
            "/api/auth/customers",
            Some(&visitor),
            json!({"name":Uuid::new_v4().to_string(),"remark":""})
        )
        .await
        .0,
        StatusCode::UNAUTHORIZED
    );
    assert_eq!(
        call(
            &fixture.app,
            "GET",
            "/api/auth/authors?limit=100",
            Some(&visitor),
            Value::Null
        )
        .await
        .0,
        StatusCode::UNAUTHORIZED
    );
    assert_eq!(
        call(
            &fixture.app,
            "GET",
            "/api/auth/customers?limit=100",
            Some(&visitor),
            Value::Null
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &fixture.app,
            "GET",
            "/api/auth/licenses?limit=101",
            Some(&visitor),
            Value::Null
        )
        .await
        .0,
        StatusCode::BAD_REQUEST
    );
    assert_eq!(
        before,
        sqlx::query_scalar::<_, i64>("SELECT count(*) FROM pixels.customers")
            .fetch_one(&fixture.owner)
            .await
            .unwrap()
    );
    let rows = call(
        &fixture.app,
        "GET",
        "/api/auth/authors?limit=100",
        Some(&token),
        Value::Null,
    )
    .await
    .1;
    assert!(rows
        .as_array()
        .unwrap()
        .iter()
        .any(|row| row["username"] == username));
    assert!(!rows.to_string().contains("password"));
    fixture.close().await;
}
#[tokio::test]
async fn issuance_renewal_and_revocation_use_the_minimal_license_contract() {
    let fixture = Fixture::new("admin").await;
    let token = fixture.login().await;
    let customer = call(
        &fixture.app,
        "POST",
        "/api/auth/customers",
        Some(&token),
        json!({"name":Uuid::new_v4().to_string(),"remark":"synthetic"}),
    )
    .await;
    assert_eq!(customer.0, StatusCode::CREATED);
    let terms = json!({
        "customer_id":customer.1["id"],
        "deployment_id":Uuid::new_v4(),
        "expires_at":chrono::Utc::now().timestamp()+86400,
        "max_streams":4,
        "services":["cloud_applications","desktop","rdp"]
    });
    let issued = call(
        &fixture.app,
        "POST",
        "/api/auth/licenses/issue",
        Some(&token),
        json!({"request_id":Uuid::new_v4(),"request":{"operation":"create","terms":terms}}),
    )
    .await;
    assert_eq!(issued.0, StatusCode::OK);
    let license_id = issued.1["license_id"].as_str().unwrap();
    let renewed = call(
        &fixture.app,
        "POST",
        "/api/auth/licenses/issue",
        Some(&token),
        json!({"request_id":Uuid::new_v4(),"request":{"operation":"renew","license_id":license_id,"expected_revision":1,"terms":terms}}),
    )
    .await;
    assert_eq!(renewed.0, StatusCode::OK);
    assert_eq!(renewed.1["revision"], 2);
    assert_eq!(
        call(
            &fixture.app,
            "POST",
            &format!("/api/auth/licenses/{license_id}/revoke"),
            Some(&token),
            json!({"expected_revision":2}),
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &fixture.app,
            "POST",
            "/api/auth/licenses/verify",
            None,
            json!({}),
        )
        .await
        .0,
        StatusCode::NOT_FOUND
    );
    fixture.close().await;
}
#[tokio::test]
async fn malformed_legacy_requests_and_database_failure_never_succeed() {
    let fixture = Fixture::new("admin").await;
    let token = fixture.login().await;
    for path in [
        "/api/v1/verify/author",
        "/api/v1/create/authorization",
        "/api/gopico/verify",
    ] {
        assert_eq!(
            call(&fixture.app, "POST", path, Some(&token), json!({}))
                .await
                .0,
            StatusCode::NOT_FOUND
        );
    }
    assert_eq!(
        call(
            &fixture.app,
            "POST",
            "/api/auth/sessions",
            None,
            json!({"username":fixture.username,"password":PASSWORD,"role":"admin"})
        )
        .await
        .0,
        StatusCode::UNPROCESSABLE_ENTITY
    );
    assert_eq!(
        call(
            &fixture.app,
            "POST",
            "/api/auth/sessions",
            None,
            json!({"username":fixture.username,"password":"a".repeat(40000)})
        )
        .await
        .0,
        StatusCode::PAYLOAD_TOO_LARGE
    );
    assert_eq!(
        call(&fixture.app, "GET", "/health/ready", None, Value::Null)
            .await
            .0,
        StatusCode::NO_CONTENT
    );
    fixture.state.close().await;
    assert_eq!(
        call(&fixture.app, "GET", "/health/ready", None, Value::Null)
            .await
            .0,
        StatusCode::SERVICE_UNAVAILABLE
    );
    assert_eq!(
        call(&fixture.app, "GET", "/health/live", None, Value::Null)
            .await
            .0,
        StatusCode::NO_CONTENT
    );
    assert_eq!(
        call(
            &fixture.app,
            "POST",
            "/api/auth/customers",
            Some(&token),
            json!({"name":Uuid::new_v4().to_string(),"remark":""})
        )
        .await
        .0,
        StatusCode::SERVICE_UNAVAILABLE
    );
    fixture.close().await;
}
