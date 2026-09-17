#[path = "support/runtime_fixture.rs"]
mod fixture;
use axum::http::StatusCode;
use fixture::{
    binary_call, call, config, deployment, login, policy, register, request, start, vault, PASSWORD,
};
use px_console_runtime::ConsoleRuntime;
use serde_json::{json, Value};
use std::time::Duration;
use uuid::Uuid;
const FIXTURE_KIND: &str = "api";

#[tokio::test]
async fn self_profile_and_avatar_are_revision_bound_typed_and_transactional() {
    let runtime = start().await;
    let router = runtime.router();
    let original_name = register(&router).await;
    let token = login(&router, &original_name, PASSWORD, "android").await;
    let (_, initial) = call(
        &router,
        "GET",
        "/api/console/profile",
        "android",
        Some(&token),
        Value::Null,
    )
    .await;
    assert_eq!(initial["revision"], 1);
    assert!(initial["avatar_url"].is_null());
    let changed_name = Uuid::new_v4().to_string();
    let (changed_status, changed) = call(
        &router,
        "PATCH",
        "/api/console/profile",
        "android",
        Some(&token),
        json!({"revision":1,"username":changed_name}),
    )
    .await;
    assert_eq!(changed_status, StatusCode::OK, "{changed}");
    assert_eq!(changed["revision"], 2);
    assert_eq!(changed["username"], changed_name);
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/profile",
            "panel",
            Some(&token),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    let png = hex::decode("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d4944415408d763f8cfc0f01f00050001ff89993d1d0000000049454e44ae426082").unwrap();
    assert_eq!(
        binary_call(
            &router,
            "PUT",
            "/api/console/profile/avatar?revision=2",
            "android",
            &token,
            "image/jpeg",
            png.clone(),
        )
        .await
        .0,
        StatusCode::BAD_REQUEST
    );
    let (avatar_status, _, avatar_profile) = binary_call(
        &router,
        "PUT",
        "/api/console/profile/avatar?revision=2",
        "android",
        &token,
        "image/png",
        png.clone(),
    )
    .await;
    assert_eq!(avatar_status, StatusCode::OK);
    let avatar_profile: Value = serde_json::from_slice(&avatar_profile).unwrap();
    assert_eq!(avatar_profile["revision"], 3);
    assert_eq!(avatar_profile["avatar_url"], "/api/console/profile/avatar");
    let (read_status, read_headers, read_body) = binary_call(
        &router,
        "GET",
        "/api/console/profile/avatar",
        "android",
        &token,
        "application/octet-stream",
        Vec::new(),
    )
    .await;
    assert_eq!(read_status, StatusCode::OK);
    assert_eq!(read_headers["content-type"], "image/png");
    assert_eq!(read_headers["x-content-type-options"], "nosniff");
    assert_eq!(read_body, png);
    assert_eq!(
        call(
            &router,
            "DELETE",
            "/api/console/profile/avatar?revision=2",
            "android",
            Some(&token),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    let (delete_status, deleted) = call(
        &router,
        "DELETE",
        "/api/console/profile/avatar?revision=3",
        "android",
        Some(&token),
        Value::Null,
    )
    .await;
    assert_eq!(delete_status, StatusCode::OK, "{deleted}");
    assert_eq!(deleted["revision"], 4);
    assert!(deleted["avatar_url"].is_null());
    assert_eq!(
        binary_call(
            &router,
            "GET",
            "/api/console/profile/avatar",
            "android",
            &token,
            "application/octet-stream",
            Vec::new(),
        )
        .await
        .0,
        StatusCode::NOT_FOUND
    );
    let oversized_avatar = vec![0_u8; px_console_store::MAX_AVATAR_BYTES + 1];
    assert_eq!(
        binary_call(
            &router,
            "PUT",
            "/api/console/profile/avatar?revision=4",
            "android",
            &token,
            "image/png",
            oversized_avatar,
        )
        .await
        .0,
        StatusCode::PAYLOAD_TOO_LARGE
    );
    let owner = config("OWNER").connect().await.unwrap();
    sqlx::query("REVOKE INSERT ON pixels.profile_events FROM pixels_console_runtime")
        .execute(&owner)
        .await
        .unwrap();
    let rejected_name = Uuid::new_v4().to_string();
    let failed_change = call(
        &router,
        "PATCH",
        "/api/console/profile",
        "android",
        Some(&token),
        json!({"revision":4,"username":rejected_name}),
    )
    .await;
    sqlx::query("GRANT INSERT ON pixels.profile_events TO pixels_console_runtime")
        .execute(&owner)
        .await
        .unwrap();
    owner.close().await;
    assert_eq!(failed_change.0, StatusCode::SERVICE_UNAVAILABLE);
    let (_, after_failure) = call(
        &router,
        "GET",
        "/api/console/profile",
        "android",
        Some(&token),
        Value::Null,
    )
    .await;
    assert_eq!(after_failure["username"], changed_name);
    assert_eq!(after_failure["revision"], 4);
    login(&router, &changed_name, PASSWORD, "android").await;
    runtime.shutdown().await;
}
#[tokio::test]
async fn native_registration_login_password_logout_and_restart_use_exact_identity() {
    let runtime = start().await;
    let router = runtime.router();
    let name = register(&router).await;
    let token = login(&router, &name, PASSWORD, "android").await;
    assert_eq!(token.len(), 64);
    let (status, profile) = call(
        &router,
        "GET",
        "/api/console/session",
        "android",
        Some(&token),
        Value::Null,
    )
    .await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(profile["username"], name);
    assert!(!profile.to_string().contains("password"));
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/session",
            "panel",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        call(
            &router,
            "POST",
            "/api/console/sessions",
            "admin_web",
            None,
            json!({"username":name,"password":PASSWORD})
        )
        .await
        .0,
        StatusCode::UNAUTHORIZED
    );
    assert_eq!(
        call(
            &router,
            "PATCH",
            "/api/console/password",
            "android",
            Some(&token),
            json!({"current_password":PASSWORD,"new_password":"replacement-synthetic-password"})
        )
        .await
        .0,
        StatusCode::NO_CONTENT
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/session",
            "android",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    runtime.shutdown().await;
    let runtime = start().await;
    let router = runtime.router();
    let token = login(&router, &name, "replacement-synthetic-password", "android").await;
    assert_eq!(
        call(
            &router,
            "DELETE",
            "/api/console/session",
            "android",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::NO_CONTENT
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/session",
            "android",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    runtime.shutdown().await;
}
#[tokio::test]
async fn management_roles_groups_and_cas_do_not_trust_client_supplied_privilege() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let name = Uuid::new_v4().to_string();
    let (status, user) = call(
        &router,
        "POST",
        "/api/console/users",
        "admin_web",
        Some(&admin),
        json!({"username":name,"password":PASSWORD,"role":"viewer"}),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED);
    let viewer = login(&router, &name, PASSWORD, "admin_web").await;
    let reset_path = format!(
        "/api/console/users/{}/password",
        user["id"].as_str().unwrap()
    );
    assert_eq!(
        call(
            &router,
            "PATCH",
            &reset_path,
            "admin_web",
            Some(&viewer),
            json!({"revision":1,"password":"managed-replacement-password"})
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/users?limit=10",
            "admin_web",
            Some(&viewer),
            Value::Null
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &router,
            "POST",
            "/api/console/groups",
            "admin_web",
            Some(&viewer),
            json!({"name":"viewer-forbidden","remark":""})
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    let (status, group) = call(
        &router,
        "POST",
        "/api/console/groups",
        "admin_web",
        Some(&admin),
        json!({"name":Uuid::new_v4().to_string(),"remark":"测试"}),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED);
    let path = format!(
        "/api/console/groups/{}/members",
        group["id"].as_str().unwrap()
    );
    assert_eq!(
        call(
            &router,
            "PATCH",
            &reset_path,
            "admin_web",
            Some(&admin),
            json!({"revision":2,"password":"managed-replacement-password"})
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        call(
            &router,
            "PATCH",
            &reset_path,
            "admin_web",
            Some(&admin),
            json!({"revision":1,"password":"managed-replacement-password"})
        )
        .await
        .0,
        StatusCode::OK
    );
    login(&router, &name, "managed-replacement-password", "admin_web").await;
    assert_eq!(
        call(
            &router,
            "PUT",
            &path,
            "admin_web",
            Some(&admin),
            json!({"revision":1,"members":[user["id"]]})
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &router,
            "PUT",
            &path,
            "admin_web",
            Some(&admin),
            json!({"revision":1,"members":[]})
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/users?limit=10",
            "admin_web",
            Some(&viewer),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/users?limit=10",
            "android",
            Some(&admin),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    runtime.shutdown().await;
}
#[tokio::test]
async fn duplicate_activation_drop_and_database_backend_loss_fail_closed() {
    let runtime = start().await;
    let router = runtime.router();
    assert!(ConsoleRuntime::activate(
        &config("RUNTIME"),
        deployment(),
        vault(),
        policy(),
        fixture::guests(),
    )
    .await
    .is_err());
    assert!(ConsoleRuntime::activate(
        &config("OWNER"),
        deployment(),
        vault(),
        policy(),
        fixture::guests(),
    )
    .await
    .is_err());
    assert!(ConsoleRuntime::activate(
        &config("RUNTIME"),
        Uuid::new_v4(),
        vault(),
        policy(),
        fixture::guests(),
    )
    .await
    .is_err());
    drop(runtime);
    assert_eq!(
        call(
            &router,
            "GET",
            "/health/ready",
            "android",
            None,
            Value::Null
        )
        .await
        .0,
        StatusCode::SERVICE_UNAVAILABLE
    );
    let runtime = start().await;
    let router = runtime.router();
    let owner = config("OWNER").connect().await.unwrap();
    // Exact service database and advisory lock holder, not a broad backend sweep.
    let pid:i32=sqlx::query_scalar("SELECT pid FROM pg_locks WHERE locktype='advisory' AND granted AND classid=0 AND objid::bigint=22091602 AND database=(SELECT oid FROM pg_database WHERE datname=current_database())").fetch_one(&owner).await.unwrap();
    // Owner cannot terminate another role's backend. Give the runtime role its own
    // connection to terminate the exact runtime lock holder, with no admin escalation.
    let killer = config("RUNTIME").connect().await.unwrap();
    let killed: bool = sqlx::query_scalar("SELECT pg_terminate_backend($1)")
        .bind(pid)
        .fetch_one(&killer)
        .await
        .unwrap();
    assert!(killed);
    tokio::time::timeout(Duration::from_secs(3), runtime.cancelled())
        .await
        .unwrap();
    assert_eq!(
        call(
            &router,
            "GET",
            "/health/ready",
            "android",
            None,
            Value::Null
        )
        .await
        .0,
        StatusCode::SERVICE_UNAVAILABLE
    );
    runtime.shutdown().await;
    killer.close().await;
    owner.close().await;
    let next = start().await;
    next.shutdown().await;
}
#[tokio::test]
async fn ingress_rejects_wrong_origins_forwarded_identity_old_shapes_and_bruteforce() {
    let runtime = start().await;
    let router = runtime.router();
    for origin in [None, Some("https://evil.example.test"), Some("null")] {
        assert_eq!(
            request(
                &router,
                "POST",
                "/api/console/sessions",
                "admin_web",
                None,
                json!({"username":"initial-admin","password":PASSWORD}),
                origin,
                false
            )
            .await
            .0,
            StatusCode::FORBIDDEN
        );
    }
    assert_eq!(
        request(
            &router,
            "POST",
            "/api/console/sessions",
            "android",
            None,
            json!({"username":"initial-admin","password":PASSWORD}),
            None,
            true
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    let (status, body) = call(
        &router,
        "POST",
        "/api/console/accounts",
        "android",
        None,
        json!({"username":"bad","password":PASSWORD,"role":"admin"}),
    )
    .await;
    assert_eq!(status, StatusCode::BAD_REQUEST);
    assert_eq!(body, json!({"code":"invalid_input"}));
    for index in 0..11 {
        let (status, body) = call(
            &router,
            "POST",
            "/api/console/sessions",
            "android",
            None,
            json!({"username":"nonexistent-account","password":PASSWORD}),
        )
        .await;
        assert_eq!(
            status,
            if index < 10 {
                StatusCode::UNAUTHORIZED
            } else {
                StatusCode::TOO_MANY_REQUESTS
            }
        );
        assert!(!body.to_string().contains(PASSWORD));
    }
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/v1/users",
            "android",
            None,
            Value::Null
        )
        .await
        .0,
        StatusCode::NOT_FOUND
    );
    runtime.shutdown().await;
}
#[tokio::test]
async fn failed_revocation_transaction_is_not_reported_as_success_and_recovers() {
    let runtime = start().await;
    let router = runtime.router();
    let name = register(&router).await;
    let token = login(&router, &name, PASSWORD, "android").await;
    let owner = config("OWNER").connect().await.unwrap();
    sqlx::query("REVOKE INSERT ON pixels.authorization_outbox FROM pixels_console_runtime")
        .execute(&owner)
        .await
        .unwrap();
    let (status, body) = call(
        &router,
        "PATCH",
        "/api/console/password",
        "android",
        Some(&token),
        json!({"current_password":PASSWORD,"new_password":"must-not-be-committed"}),
    )
    .await;
    sqlx::query("GRANT INSERT ON pixels.authorization_outbox TO pixels_console_runtime")
        .execute(&owner)
        .await
        .unwrap();
    assert_eq!(status, StatusCode::SERVICE_UNAVAILABLE);
    assert_eq!(body, json!({"code":"unavailable"}));
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/session",
            "android",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::OK
    );
    login(&router, &name, PASSWORD, "android").await;
    owner.close().await;
    runtime.shutdown().await;
}
