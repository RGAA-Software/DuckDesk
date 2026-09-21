use axum::{
    body::{to_bytes, Body},
    http::{Request, StatusCode},
    Router,
};
use px_desk_server::{config::Settings, router, AppState};
use px_pg::{DatabaseConfig, Transport};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};
use std::{env, path::PathBuf, sync::Arc};
use tower::ServiceExt;
use uuid::Uuid;

const TOKEN: &str = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";

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
    let command = || {
        let mut command = Command::new(env!("CARGO_BIN_EXE_px_desk"));
        command
            .env(
                "PIXELS_DATABASE_URL",
                env::var("PIXELS_TEST_DESK_RUNTIME_URL").unwrap(),
            )
            .env("PIXELS_DESK_LOCAL_DEVELOPMENT", "1")
            .env("PIXELS_DESK_LISTEN", "127.0.0.1:0")
            .env(
                "PIXELS_DESK_ADMIN_TOKEN_SHA256",
                hex::encode(Sha256::digest(TOKEN.as_bytes())),
            )
            .env(
                "PIXELS_DESK_STATIC_DIRECTORY",
                PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("tests/static"),
            )
            .env_remove("PIXELS_DESK_TLS_CERT")
            .env_remove("PIXELS_DESK_TLS_KEY")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped());
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
                "Desk did not report its listener: result={unexpected:?}, status={status:?}, stderr={stderr}"
            );
        }
    };
    reader.join().unwrap();
    let address = line
        .strip_prefix("Desk listening http://")
        .unwrap()
        .split_whitespace()
        .next()
        .unwrap();
    for (path, status) in [
        ("/health/ready", "204"),
        ("/api/v1/query/issues", "404"),
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
    let owner_dsn = env::var("PIXELS_TEST_DESK_OWNER_URL").unwrap();
    for (key, value) in [
        ("PIXELS_DESK_LISTEN", "0.0.0.0:0"),
        ("PIXELS_DESK_ADMIN_TOKEN_SHA256", "invalid"),
        ("PIXELS_DESK_LOCAL_DEVELOPMENT", "0"),
        ("PIXELS_DATABASE_URL", owner_dsn.as_str()),
        (
            "PIXELS_DEPLOYMENT_ID",
            "00000000-0000-0000-0000-000000000000",
        ),
    ] {
        let mut invalid = ServerProcess(command().env(key, value).spawn().unwrap());
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

async fn application(token: &str) -> (Router, Arc<AppState>, sqlx::PgPool) {
    assert_eq!(
        env::var("PIXELS_PG_ISOLATED_TEST").as_deref(),
        Ok("1"),
        "isolated harness required"
    );
    let database = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_DESK_RUNTIME_URL").unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap();
    let owner = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_DESK_OWNER_URL").unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
    .connect()
    .await
    .unwrap();
    let settings = Settings {
        database,
        deployment: env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
        listen: "127.0.0.1:0".parse().unwrap(),
        static_directory: PathBuf::from(env!("CARGO_MANIFEST_DIR")),
        tls: None,
        admin_digest: Sha256::digest(token.as_bytes()).into(),
    };
    let state = Arc::new(AppState::connect(&settings).await.unwrap());
    (
        router(state.clone(), &settings.static_directory),
        state,
        owner,
    )
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
        .header("content-type", "application/json");
    if let Some(token) = token {
        request = request.header("authorization", format!("Bearer {token}"));
    }
    let response = app
        .clone()
        .oneshot(request.body(Body::from(body.to_string())).unwrap())
        .await
        .unwrap();
    let status = response.status();
    let bytes = to_bytes(response.into_body(), 65536).await.unwrap();
    (
        status,
        serde_json::from_slice(&bytes).unwrap_or(Value::Null),
    )
}
async fn login(app: &Router) -> String {
    let (status, body) = call(
        app,
        "POST",
        "/api/desk/admin/sessions",
        None,
        json!({"token":TOKEN}),
    )
    .await;
    assert_eq!(status, StatusCode::OK);
    let token = body["access_token"].as_str().unwrap().to_owned();
    assert_ne!(token, TOKEN);
    token
}
fn consult() -> Value {
    json!({"request_id":Uuid::new_v4(),"title":"咨询 ' 😀","your_name":"测试","description":"private synthetic content",
        "email":"test@example.invalid","wechat":"","qq":"","consult_type":"personal"})
}

#[tokio::test]
async fn health_and_retired_routes() {
    let (app, state, owner) = application(TOKEN).await;
    assert_eq!(
        call(&app, "GET", "/health/ready", None, Value::Null)
            .await
            .0,
        StatusCode::NO_CONTENT
    );
    for route in [
        "/api/v1/admin/verify",
        "/api/v1/query/product/version",
        "/api/unknown",
        "/api",
    ] {
        assert_eq!(
            call(&app, "POST", route, None, json!({})).await.0,
            StatusCode::NOT_FOUND
        );
    }
    state.close().await;
    assert_eq!(
        call(&app, "GET", "/health/ready", None, Value::Null)
            .await
            .0,
        StatusCode::SERVICE_UNAVAILABLE
    );
    assert_eq!(
        call(&app, "GET", "/health/live", None, Value::Null).await.0,
        StatusCode::NO_CONTENT
    );
    owner.close().await;
}

#[tokio::test]
async fn admin_sessions_revoke_expire_rotate_and_restart() {
    let (app, state, owner) = application(TOKEN).await;
    let path = "/api/desk/consults?page=1&page_size=10";
    let before: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.admin_sessions")
        .fetch_one(&owner)
        .await
        .unwrap();
    assert_eq!(
        call(
            &app,
            "POST",
            "/api/desk/admin/sessions",
            None,
            json!({"token":"bad"})
        )
        .await
        .0,
        StatusCode::UNAUTHORIZED
    );
    let after: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.admin_sessions")
        .fetch_one(&owner)
        .await
        .unwrap();
    assert_eq!(before, after);
    assert_eq!(
        call(&app, "GET", path, Some(TOKEN), Value::Null).await.0,
        StatusCode::UNAUTHORIZED
    );
    let token = login(&app).await;
    assert_eq!(
        call(&app, "GET", path, Some(&token), Value::Null).await.0,
        StatusCode::OK
    );
    state.close().await;
    let (restarted, restarted_state, other) = application(TOKEN).await;
    assert_eq!(
        call(&restarted, "GET", path, Some(&token), Value::Null)
            .await
            .0,
        StatusCode::OK
    );
    let (rotated, rotated_state, third) = application(&"a".repeat(64)).await;
    assert_eq!(
        call(&rotated, "GET", path, Some(&token), Value::Null)
            .await
            .0,
        StatusCode::UNAUTHORIZED
    );
    assert_eq!(
        call(
            &restarted,
            "DELETE",
            "/api/desk/admin/session",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::NO_CONTENT
    );
    assert_eq!(
        call(&restarted, "GET", path, Some(&token), Value::Null)
            .await
            .0,
        StatusCode::UNAUTHORIZED
    );
    let expiring = login(&restarted).await;
    sqlx::query("UPDATE pixels.admin_sessions SET created_at=clock_timestamp()-interval '9 hours',expires_at=clock_timestamp()-interval '1 hour' WHERE token_hash=$1")
        .bind(&Sha256::digest(expiring.as_bytes())[..]).execute(&owner).await.unwrap();
    assert_eq!(
        call(&restarted, "GET", path, Some(&expiring), Value::Null)
            .await
            .0,
        StatusCode::UNAUTHORIZED
    );
    restarted_state.close().await;
    rotated_state.close().await;
    owner.close().await;
    other.close().await;
    third.close().await;
}

#[tokio::test]
async fn feedback_idempotency_pagination_cas_and_permissions() {
    let (app, state, owner) = application(TOKEN).await;
    let body = consult();
    let id = body["request_id"].as_str().unwrap();
    let (status, receipt) = call(&app, "POST", "/api/desk/consults", None, body.clone()).await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(receipt, json!({"id":id}));
    assert_eq!(
        call(&app, "POST", "/api/desk/consults", None, body.clone()).await,
        (StatusCode::OK, receipt)
    );
    let mut changed = body.clone();
    changed["description"] = json!("different");
    assert_eq!(
        call(&app, "POST", "/api/desk/consults", None, changed)
            .await
            .0,
        StatusCode::CONFLICT
    );
    let token = login(&app).await;
    let list = "/api/desk/consults?page=1&page_size=100";
    assert_eq!(
        call(&app, "GET", list, None, Value::Null).await.0,
        StatusCode::UNAUTHORIZED
    );
    let items = call(&app, "GET", list, Some(&token), Value::Null).await.1;
    assert!(items["items"]
        .as_array()
        .unwrap()
        .iter()
        .any(|response_record| {
            response_record["id"] == id && response_record["revision"] == 1
        }));
    let path = format!("/api/desk/consults/{id}");
    let mark = json!({"expected_revision":1,"processed":true});
    assert_eq!(
        call(&app, "PATCH", &path, None, mark.clone()).await.0,
        StatusCode::UNAUTHORIZED
    );
    let (first_response, second_response) = tokio::join!(
        call(&app, "PATCH", &path, Some(&token), mark.clone()),
        call(&app, "PATCH", &path, Some(&token), mark)
    );
    assert!(
        (first_response.0 == StatusCode::OK && second_response.0 == StatusCode::CONFLICT)
            || (second_response.0 == StatusCode::OK && first_response.0 == StatusCode::CONFLICT)
    );
    let revision: i64 = sqlx::query_scalar("SELECT revision FROM pixels.feedback WHERE id=$1")
        .bind(id.parse::<Uuid>().unwrap())
        .fetch_one(&owner)
        .await
        .unwrap();
    assert_eq!(revision, 2);
    let runtime = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_DESK_RUNTIME_URL").unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
    .connect()
    .await
    .unwrap();
    assert!(
        sqlx::query("UPDATE pixels.feedback SET description='tampered' WHERE id=$1")
            .bind(id.parse::<Uuid>().unwrap())
            .execute(&runtime)
            .await
            .is_err()
    );
    for query in [
        "page=0&page_size=10",
        "page=1&page_size=101",
        "page=1&page_size=10&sort_time=-1",
    ] {
        assert!(call(
            &app,
            "GET",
            &format!("/api/desk/consults?{query}"),
            Some(&token),
            Value::Null
        )
        .await
        .0
        .is_client_error());
    }
    runtime.close().await;
    state.close().await;
    owner.close().await;
}

#[tokio::test]
async fn invalid_feedback_has_no_side_effects_and_issue_is_distinct() {
    let (app, state, owner) = application(TOKEN).await;
    let before: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.feedback")
        .fetch_one(&owner)
        .await
        .unwrap();
    let base = consult();
    for body in [
        json!({}),
        json!({"unexpected":true}),
        {
            let mut invalid_body = base.clone();
            invalid_body["title"] = json!("");
            invalid_body
        },
        {
            let mut invalid_body = base.clone();
            invalid_body["os"] = json!("Windows");
            invalid_body
        },
        {
            let mut invalid_body = base.clone();
            invalid_body["description"] = json!("x".repeat(8193));
            invalid_body
        },
    ] {
        assert!(call(&app, "POST", "/api/desk/consults", None, body)
            .await
            .0
            .is_client_error());
    }
    let mut oversized = base.clone();
    oversized["description"] = json!("x".repeat(40000));
    assert_eq!(
        call(&app, "POST", "/api/desk/consults", None, oversized)
            .await
            .0,
        StatusCode::PAYLOAD_TOO_LARGE
    );
    let after: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.feedback")
        .fetch_one(&owner)
        .await
        .unwrap();
    assert_eq!(before, after);
    let mut issue = base;
    issue.as_object_mut().unwrap().remove("consult_type");
    issue["version"] = json!("1.0.1");
    issue["os"] = json!("Windows");
    assert_eq!(
        call(&app, "POST", "/api/desk/issues", None, issue.clone())
            .await
            .0,
        StatusCode::OK
    );
    let token = login(&app).await;
    let rows = call(
        &app,
        "GET",
        "/api/desk/issues?page=1&page_size=100",
        Some(&token),
        Value::Null,
    )
    .await
    .1;
    assert!(rows["items"]
        .as_array()
        .unwrap()
        .iter()
        .any(|response_record| response_record["id"] == issue["request_id"]));
    state.close().await;
    owner.close().await;
}

fn release(
    product: &str,
    distribution: &str,
    channel: &str,
    os: &str,
    architecture: &str,
    build: i64,
) -> Value {
    json!({"target":{"product":product,"distribution":distribution,"channel":channel,"os":os,"architecture":architecture},
        "build_number":build,"version":"1.2.3","metadata_base_url":"https://example.invalid/metadata/",
        "targets_base_url":"https://example.invalid/targets/","target_name":"release/pixels.bin","sha256":"a".repeat(64),
        "size_bytes":1234})
}
#[tokio::test]
async fn versions_are_persisted_dimensioned_and_write_failures_do_not_publish() {
    let (app, state, owner) = application(TOKEN).await;
    let token = login(&app).await;
    let seed = chrono::Utc::now().timestamp_micros();
    for (product, os, arch) in [
        ("cloud_node", "windows", "x86_64"),
        ("client", "windows", "x86_64"),
        ("remote", "windows", "x86_64"),
        ("android", "android", "aarch64"),
        ("server", "windows", "x86_64"),
        ("server", "linux", "x86_64"),
    ] {
        for distribution in ["official", "customer"] {
            for channel in ["stable", "preview"] {
                let body = release(product, distribution, channel, os, arch, seed);
                assert_eq!(
                    call(&app, "POST", "/api/desk/versions", None, body.clone())
                        .await
                        .0,
                    StatusCode::UNAUTHORIZED
                );
                assert_eq!(
                    call(
                        &app,
                        "POST",
                        "/api/desk/versions",
                        Some(&token),
                        body.clone()
                    )
                    .await
                    .0,
                    StatusCode::OK
                );
                assert_eq!(
                    call(
                        &app,
                        "POST",
                        "/api/desk/versions",
                        Some(&token),
                        body.clone()
                    )
                    .await
                    .0,
                    StatusCode::CONFLICT
                );
                let mut changed = body.clone();
                changed["sha256"] = json!("c".repeat(64));
                assert_eq!(
                    call(&app, "POST", "/api/desk/versions", Some(&token), changed)
                        .await
                        .0,
                    StatusCode::CONFLICT
                );
                let mut older = body;
                older["build_number"] = json!(seed - 1);
                older["version"] = json!("older");
                assert_eq!(
                    call(&app, "POST", "/api/desk/versions", Some(&token), older)
                        .await
                        .0,
                    StatusCode::OK
                );
                let path=format!("/api/desk/versions?product={product}&distribution={distribution}&channel={channel}&os={os}&architecture={arch}");
                let (status, result) = call(&app, "GET", &path, None, Value::Null).await;
                assert_eq!(status, StatusCode::OK);
                assert_eq!(result["build_number"], seed);
                assert_eq!(result["version"], "1.2.3");
                assert_eq!(result["os"], os);
                assert_eq!(result["architecture"], arch);
                assert_eq!(result["size_bytes"], 1234);
                assert_eq!(result["sha256"], "a".repeat(64));
                assert_eq!(result["target_name"], "release/pixels.bin");
            }
        }
    }
    sqlx::query("REVOKE INSERT ON pixels.versions FROM pixels_desk_runtime")
        .execute(&owner)
        .await
        .unwrap();
    let failed = call(
        &app,
        "POST",
        "/api/desk/versions",
        Some(&token),
        release(
            "client",
            "official",
            "stable",
            "windows",
            "x86_64",
            seed + 1,
        ),
    )
    .await;
    sqlx::query("GRANT INSERT ON pixels.versions TO pixels_desk_runtime")
        .execute(&owner)
        .await
        .unwrap();
    assert_eq!(failed.0, StatusCode::SERVICE_UNAVAILABLE);
    let current=call(&app,"GET","/api/desk/versions?product=client&distribution=official&channel=stable&os=windows&architecture=x86_64",None,Value::Null).await.1;
    assert_eq!(current["build_number"], seed);
    state.close().await;
    owner.close().await;
}
#[tokio::test]
async fn release_platform_missing_fields_invalid_metadata_and_races_cannot_publish_wrong_artifacts()
{
    let (app, state, owner) = application(TOKEN).await;
    let token = login(&app).await;
    let base = release(
        "server",
        "customer",
        "preview",
        "linux",
        "x86_64",
        chrono::Utc::now().timestamp_micros(),
    );
    let before: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.versions")
        .fetch_one(&owner)
        .await
        .unwrap();
    for field in ["os", "architecture", "distribution"] {
        let mut body = base.clone();
        body["target"].as_object_mut().unwrap().remove(field);
        assert!(call(&app, "POST", "/api/desk/versions", Some(&token), body)
            .await
            .0
            .is_client_error());
    }
    for (field, value) in [
        ("size_bytes", json!(0)),
        ("metadata_base_url", json!("https://u:p@example.invalid/a/")),
        ("target_name", json!("../escape.bin")),
        (
            "targets_base_url",
            json!("https://example.invalid/a?token=x"),
        ),
    ] {
        let mut body = base.clone();
        body[field] = value;
        assert!(call(&app, "POST", "/api/desk/versions", Some(&token), body)
            .await
            .0
            .is_client_error());
    }
    for query in [
        "product=server&distribution=customer&channel=preview",
        "product=android&distribution=customer&channel=preview&os=windows&architecture=x86_64",
        "product=panel&distribution=official&channel=stable&os=windows&architecture=x86_64",
    ] {
        assert!(call(
            &app,
            "GET",
            &format!("/api/desk/versions?{query}"),
            None,
            Value::Null
        )
        .await
        .0
        .is_client_error());
    }
    let after: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.versions")
        .fetch_one(&owner)
        .await
        .unwrap();
    assert_eq!(before, after);
    let mut tasks = tokio::task::JoinSet::new();
    for _ in 0..20 {
        let app = app.clone();
        let token = token.clone();
        let body = base.clone();
        tasks.spawn(async move {
            call(&app, "POST", "/api/desk/versions", Some(&token), body)
                .await
                .0
        });
    }
    let mut successes = 0;
    while let Some(status) = tasks.join_next().await {
        match status.unwrap() {
            StatusCode::OK => successes += 1,
            StatusCode::CONFLICT => (),
            other => panic!("unexpected {other}"),
        }
    }
    assert_eq!(successes, 1);
    state.close().await;
    owner.close().await;
}
