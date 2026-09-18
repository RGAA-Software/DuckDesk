#[path = "support/runtime_fixture.rs"]
mod fixture;
use axum::http::StatusCode;
use fixture::{call, config, login, register, resource_call, start, PASSWORD};
use serde_json::{json, Value};
use uuid::Uuid;
const FIXTURE_KIND: &str = "directory";
fn spec(kind: &str, access: &str) -> Value {
    let launch = match kind {
        "rdp" => json!({"kind":"rdp"}),
        "webview" => {
            json!({"kind":"webview","entry_url":"https://example.test/app","video":{"codec":"h264","bitrate_kbps":8000}})
        }
        _ => {
            json!({"kind":"game_hook","executable_relative":"游戏 目录\\game.exe","arguments":"--title \"应用 名称\"","video":{"codec":"h265","bitrate_kbps":8000}})
        }
    };
    json!({"name":Uuid::new_v4().to_string(),"launch":launch,"access":access,"allow_observer":false,"allow_takeover":false,"disabled":false})
}
async fn create_device(router: &axum::Router, admin: &str) -> Value {
    let (status, value) = call(
        router,
        "POST",
        "/api/console/managed/devices",
        "admin_web",
        Some(admin),
        json!({"name":Uuid::new_v4().to_string(),"platform":"windows"}),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED, "{value}");
    value
}

fn saved_connection_settings(name: &str) -> Value {
    json!({
        "name":name,
        "video_bitrate_bps":10_000_000,
        "video_fps":60,
        "audio_enabled":true,
        "clipboard_enabled":true,
        "view_only":false,
        "maximize":false,
        "split_windows":false,
        "prefer_peer_to_peer":true,
        "audio_capture":"system_mix",
        "background_rgb":0
    })
}

#[tokio::test]
async fn update_catalog_requires_explicit_approval_and_exact_client_identity() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let username = register(&router).await;
    let android = login(&router, &username, PASSWORD, "android").await;
    let request_id = Uuid::new_v4();
    let artifact = json!({
        "target":{
            "product":"android",
            "distribution":"official",
            "channel":"stable",
            "os":"android",
            "architecture":"aarch64"
        },
        "build_number":32018,
        "version":"3.2.18",
        "artifact_url":"https://downloads.example.test/pixels-3.2.18.apk",
        "sha256":"a".repeat(64),
        "size_bytes":12345678,
        "metadata_url":"https://downloads.example.test/3.2.18.targets.json",
        "metadata_sha256":"b".repeat(64)
    });
    let create_body = json!({"request_id":request_id,"artifact":artifact});
    let (created_status, created) = call(
        &router,
        "POST",
        "/api/console/managed/updates",
        "admin_web",
        Some(&admin),
        create_body.clone(),
    )
    .await;
    assert_eq!(created_status, StatusCode::CREATED, "{created}");
    assert_eq!(created["state"], "pending");
    assert_eq!(
        call(
            &router,
            "POST",
            "/api/console/managed/updates",
            "admin_web",
            Some(&admin),
            create_body,
        )
        .await
        .1,
        created
    );
    let latest_path = "/api/console/updates/latest?product=android&distribution=official&channel=stable&os=android&architecture=aarch64";
    assert_eq!(
        call(
            &router,
            "GET",
            latest_path,
            "android",
            Some(&android),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    let release_id = created["id"].as_str().unwrap();
    let (approved_status, approved) = call(
        &router,
        "PATCH",
        &format!("/api/console/managed/updates/{release_id}"),
        "admin_web",
        Some(&admin),
        json!({"revision":1,"decision":"approve"}),
    )
    .await;
    assert_eq!(approved_status, StatusCode::OK, "{approved}");
    let (latest_status, latest) = call(
        &router,
        "GET",
        latest_path,
        "android",
        Some(&android),
        Value::Null,
    )
    .await;
    assert_eq!(latest_status, StatusCode::OK, "{latest}");
    assert_eq!(latest, approved);
    assert_eq!(
        call(
            &router,
            "GET",
            latest_path,
            "panel",
            Some(&android),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/managed/updates?limit=100",
            "admin_web",
            Some(&admin),
            Value::Null,
        )
        .await
        .1
        .as_array()
        .unwrap()
        .len(),
        1
    );
    let (withdrawn_status, withdrawn) = call(
        &router,
        "PATCH",
        &format!("/api/console/managed/updates/{release_id}"),
        "admin_web",
        Some(&admin),
        json!({"revision":2,"decision":"withdraw"}),
    )
    .await;
    assert_eq!(withdrawn_status, StatusCode::OK, "{withdrawn}");
    assert_eq!(
        call(
            &router,
            "GET",
            latest_path,
            "android",
            Some(&android),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    runtime.shutdown().await;
}

#[tokio::test]
async fn saved_connections_are_user_client_acl_and_revision_bound() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let username = Uuid::new_v4().to_string();
    let (registration_status, registered_user) = call(
        &router,
        "POST",
        "/api/console/accounts",
        "android",
        None,
        json!({"username":username,"password":PASSWORD}),
    )
    .await;
    assert_eq!(
        registration_status,
        StatusCode::CREATED,
        "{registered_user}"
    );
    let device = create_device(&router, &admin).await;
    let device_id = device["device"]["id"].as_str().unwrap();
    assert_eq!(
        call(
            &router,
            "PUT",
            &format!("/api/console/managed/devices/{device_id}/access"),
            "admin_web",
            Some(&admin),
            json!({"revision":1,"users":[registered_user["id"]],"groups":[]}),
        )
        .await
        .0,
        StatusCode::OK
    );
    // ACL changes invalidate sessions for every affected user. A newly issued
    // Android session must observe the updated authorization revision.
    let user = login(&router, &username, PASSWORD, "android").await;
    let request_id = Uuid::new_v4();
    let create_body = json!({
        "request_id":request_id,
        "target":{"kind":"desktop","device_id":device_id},
        "settings":saved_connection_settings("工作电脑")
    });
    let (created_status, created) = call(
        &router,
        "POST",
        "/api/console/saved-connections",
        "android",
        Some(&user),
        create_body.clone(),
    )
    .await;
    assert_eq!(created_status, StatusCode::CREATED, "{created}");
    let (_, retried) = call(
        &router,
        "POST",
        "/api/console/saved-connections",
        "android",
        Some(&user),
        create_body,
    )
    .await;
    assert_eq!(retried, created);
    let connection_id = created["id"].as_str().unwrap();
    let (_, listed) = call(
        &router,
        "GET",
        "/api/console/saved-connections?limit=100",
        "android",
        Some(&user),
        Value::Null,
    )
    .await;
    assert_eq!(listed.as_array().unwrap().len(), 1);
    assert_eq!(
        call(
            &router,
            "GET",
            &format!("/api/console/saved-connections/{connection_id}"),
            "panel",
            Some(&user),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    let (updated_status, updated) = call(
        &router,
        "PATCH",
        &format!("/api/console/saved-connections/{connection_id}"),
        "android",
        Some(&user),
        json!({"revision":1,"settings":saved_connection_settings("工作电脑 2")}),
    )
    .await;
    assert_eq!(updated_status, StatusCode::OK, "{updated}");
    assert_eq!(updated["revision"], 2);
    assert_eq!(
        call(
            &router,
            "DELETE",
            &format!("/api/console/saved-connections/{connection_id}?revision=1"),
            "android",
            Some(&user),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    let (deleted_status, deleted) = call(
        &router,
        "DELETE",
        &format!("/api/console/saved-connections/{connection_id}?revision=2"),
        "android",
        Some(&user),
        Value::Null,
    )
    .await;
    assert_eq!(deleted_status, StatusCode::OK, "{deleted}");
    assert_eq!(deleted["revision"], 3);
    assert!(!deleted["deleted_at"].is_null());
    runtime.shutdown().await;
}

async fn issue_guest(router: &axum::Router) -> Value {
    let (status, value) = call(
        router,
        "POST",
        "/api/console/guest-sessions",
        "android",
        None,
        json!({}),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED, "{value}");
    value
}

#[tokio::test]
async fn guest_ingress_uses_direct_source_public_catalog_and_explicit_operator_blocks() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let name = register(&router).await;
    let user = login(&router, &name, PASSWORD, "android").await;

    let (_, public) = call(
        &router,
        "POST",
        "/api/console/managed/applications",
        "admin_web",
        Some(&admin),
        spec("webview", "public"),
    )
    .await;
    let (_, private) = call(
        &router,
        "POST",
        "/api/console/managed/applications",
        "admin_web",
        Some(&admin),
        spec("rdp", "acl"),
    )
    .await;

    assert_eq!(
        call(
            &router,
            "POST",
            "/api/console/guest-sessions",
            "android",
            Some(&user),
            json!({})
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        fixture::request(
            &router,
            "POST",
            "/api/console/guest-sessions",
            "android",
            None,
            json!({}),
            Some(fixture::ORIGIN),
            true,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );

    let first = issue_guest(&router).await;
    let second = issue_guest(&router).await;
    let first_token = first["token"].as_str().unwrap();
    let second_token = second["token"].as_str().unwrap();
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/guest-session",
            "android",
            Some(first_token),
            Value::Null
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/guest-session",
            "android",
            Some(&user),
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
            "/api/console/session",
            "android",
            Some(first_token),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );

    let (status, cards) = call(
        &router,
        "GET",
        "/api/console/guest/applications?limit=100",
        "android",
        Some(first_token),
        Value::Null,
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{cards}");
    assert!(cards
        .as_array()
        .unwrap()
        .iter()
        .any(|card| card["id"] == public["id"]));
    assert!(!cards
        .as_array()
        .unwrap()
        .iter()
        .any(|card| card["id"] == private["id"]));
    assert_eq!(
        call(
            &router,
            "GET",
            &format!(
                "/api/console/guest/applications/{}",
                private["id"].as_str().unwrap()
            ),
            "android",
            Some(first_token),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );

    let (status, managed) = call(
        &router,
        "GET",
        "/api/console/managed/guests?limit=100",
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{managed}");
    assert!(!managed.to_string().contains(first_token));
    assert!(!managed.to_string().contains("source_hash"));

    let first_id = first["session"]["id"].as_str().unwrap();
    let first_revision = first["session"]["revision"].as_i64().unwrap();
    assert_eq!(
        call(
            &router,
            "POST",
            &format!("/api/console/managed/guests/{first_id}/block"),
            "admin_web",
            Some(&admin),
            json!({"revision":first_revision})
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/guest-session",
            "android",
            Some(first_token),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );

    let second_id = second["session"]["id"].as_str().unwrap();
    let second_revision = second["session"]["revision"].as_i64().unwrap();
    assert_eq!(
        call(
            &router,
            "POST",
            &format!("/api/console/managed/guests/{second_id}/block-source"),
            "admin_web",
            Some(&admin),
            json!({"revision":second_revision,"lifetime_seconds":3600})
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/guest-session",
            "android",
            Some(second_token),
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
            "/api/console/guest-sessions",
            "android",
            None,
            json!({})
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );

    runtime.shutdown().await;
}

#[tokio::test]
async fn resource_ingress_requires_one_explicit_principal_kind_without_token_fallback() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let name = register(&router).await;
    let user = login(&router, &name, PASSWORD, "android").await;
    let (status, guest) = resource_call(
        &router,
        "POST",
        "/api/console/guest-sessions",
        "android",
        None,
        None,
        json!({}),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED, "{guest}");
    let guest_token = guest["token"].as_str().unwrap();
    let (_, application) = call(
        &router,
        "POST",
        "/api/console/managed/applications",
        "admin_web",
        Some(&admin),
        spec("webview", "public"),
    )
    .await;
    let start_request = json!({
        "request_id": Uuid::new_v4(),
        "application_id": application["id"],
        "deployment_id": null
    });

    for (token, subject, client) in [
        (Some(user.as_str()), None, "android"),
        (Some(user.as_str()), Some("device"), "android"),
    ] {
        assert_eq!(
            resource_call(
                &router,
                "POST",
                "/api/console/instances",
                client,
                token,
                subject,
                start_request.clone(),
            )
            .await
            .0,
            StatusCode::BAD_REQUEST
        );
    }
    for (token, subject, client) in [
        (user.as_str(), "guest", "android"),
        (guest_token, "user", "android"),
        (admin.as_str(), "user", "admin_web"),
    ] {
        assert_eq!(
            resource_call(
                &router,
                "POST",
                "/api/console/instances",
                client,
                Some(token),
                Some(subject),
                start_request.clone(),
            )
            .await
            .0,
            StatusCode::FORBIDDEN
        );
    }

    for (token, subject) in [(user.as_str(), "user"), (guest_token, "guest")] {
        assert_eq!(
            resource_call(
                &router,
                "POST",
                "/api/console/instances",
                "android",
                Some(token),
                Some(subject),
                start_request.clone(),
            )
            .await
            .0,
            StatusCode::SERVICE_UNAVAILABLE
        );
    }
    let (status, sessions) = call(
        &router,
        "GET",
        "/api/console/managed/resource-sessions?limit=100",
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{sessions}");
    assert_eq!(sessions, json!([]));

    for path in [
        "/api/console/activity/visits?limit=0",
        "/api/console/activity/channels?limit=100&unexpected=1",
        "/api/console/file-transfers?limit=101",
        "/api/console/recordings?node=not-a-uuid&limit=100",
    ] {
        assert_eq!(
            resource_call(
                &router,
                "GET",
                path,
                "android",
                Some(&user),
                Some("user"),
                Value::Null,
            )
            .await
            .0,
            StatusCode::BAD_REQUEST
        );
    }
    assert_eq!(
        call(
            &router,
            "GET",
            "/api/console/managed/activity/visits?limit=100",
            "android",
            Some(&user),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        resource_call(
            &router,
            "GET",
            "/api/console/activity/visits?limit=100",
            "android",
            Some(guest_token),
            Some("guest"),
            Value::Null,
        )
        .await,
        (StatusCode::OK, json!([]))
    );
    assert_eq!(
        resource_call(
            &router,
            "GET",
            &format!("/api/console/recordings?node={}&limit=100", Uuid::new_v4()),
            "android",
            Some(guest_token),
            Some("guest"),
            Value::Null,
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );

    runtime.shutdown().await;
}
#[tokio::test]
async fn device_directory_enforces_live_acl_cas_secret_boundaries_and_atomic_failure() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    for path in [
        "/api/console/managed/devices?limit=0",
        "/api/console/managed/devices?limit=1&unexpected=1",
        "/api/console/managed/devices/not-a-uuid/access",
    ] {
        let (status, error) =
            call(&router, "GET", path, "admin_web", Some(&admin), Value::Null).await;
        assert_eq!(status, StatusCode::BAD_REQUEST);
        assert_eq!(error, json!({"code":"invalid_input"}));
    }
    let name = register(&router).await;
    let token = login(&router, &name, PASSWORD, "android").await;
    let (_, user) = call(
        &router,
        "GET",
        "/api/console/session",
        "android",
        Some(&token),
        Value::Null,
    )
    .await;
    let created = create_device(&router, &admin).await;
    let id = created["device"]["id"].as_str().unwrap();
    let visible = format!("/api/console/devices/{id}");
    let managed = format!("/api/console/managed/devices/{id}");
    assert_eq!(
        call(
            &router,
            "GET",
            &visible,
            "android",
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
            "/api/console/managed/devices",
            "android",
            Some(&token),
            json!({"name":"forbidden","platform":"windows"})
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        call(
            &router,
            "PUT",
            &format!("{managed}/access"),
            "admin_web",
            Some(&admin),
            json!({"revision":1,"users":[user["id"]],"groups":[]})
        )
        .await
        .0,
        StatusCode::OK
    );
    let token = login(&router, &name, PASSWORD, "android").await;
    assert_eq!(
        call(
            &router,
            "GET",
            &visible,
            "android",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(&router, "GET", &visible, "panel", Some(&token), Value::Null)
            .await
            .0,
        StatusCode::FORBIDDEN
    );
    assert_eq!(
        call(
            &router,
            "PATCH",
            &managed,
            "admin_web",
            Some(&admin),
            json!({"revision":1,"name":"stale","disabled":false})
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    let (status, rotated) = call(
        &router,
        "POST",
        &format!("{managed}/credential"),
        "admin_web",
        Some(&admin),
        json!({"revision":2}),
    )
    .await;
    assert_eq!(status, StatusCode::OK);
    assert_ne!(created["enrollment_token"], rotated["enrollment_token"]);
    let (status, list) = call(
        &router,
        "GET",
        "/api/console/managed/devices?limit=100",
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(status, StatusCode::OK);
    for forbidden in [
        "enrollment_token",
        "enrollment_hash",
        created["enrollment_token"].as_str().unwrap(),
        rotated["enrollment_token"].as_str().unwrap(),
    ] {
        assert!(!list.to_string().contains(forbidden))
    }
    let owner = config("OWNER").connect().await.unwrap();
    let failed_name = Uuid::new_v4().to_string();
    sqlx::query("REVOKE INSERT ON pixels.device_audit FROM pixels_console_runtime")
        .execute(&owner)
        .await
        .unwrap();
    let (status, error) = call(
        &router,
        "POST",
        "/api/console/managed/devices",
        "admin_web",
        Some(&admin),
        json!({"name":failed_name,"platform":"windows"}),
    )
    .await;
    sqlx::query("GRANT INSERT ON pixels.device_audit TO pixels_console_runtime")
        .execute(&owner)
        .await
        .unwrap();
    assert_eq!(status, StatusCode::SERVICE_UNAVAILABLE);
    assert_eq!(error, json!({"code":"unavailable"}));
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.devices WHERE name=$1")
        .bind(failed_name)
        .fetch_one(&owner)
        .await
        .unwrap();
    assert_eq!(count, 0);
    assert_eq!(
        call(
            &router,
            "DELETE",
            &format!("{managed}?revision=3"),
            "admin_web",
            Some(&admin),
            Value::Null
        )
        .await
        .0,
        StatusCode::NO_CONTENT
    );
    let token = login(&router, &name, PASSWORD, "android").await;
    assert_eq!(
        call(
            &router,
            "GET",
            &visible,
            "android",
            Some(&token),
            Value::Null
        )
        .await
        .0,
        StatusCode::FORBIDDEN
    );
    owner.close().await;
    runtime.shutdown().await;
}
#[tokio::test]
async fn cloud_catalog_is_independent_typed_and_never_falls_back_to_device_identity() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let name = register(&router).await;
    let user = login(&router, &name, PASSWORD, "android").await;
    let mut applications = Vec::new();
    for kind in ["game_hook", "webview", "rdp"] {
        let (status, row) = call(
            &router,
            "POST",
            "/api/console/managed/applications",
            "admin_web",
            Some(&admin),
            spec(kind, "public"),
        )
        .await;
        assert_eq!(status, StatusCode::CREATED, "{row}");
        applications.push(row);
    }
    let (status, cards) = call(
        &router,
        "GET",
        "/api/console/applications?limit=100",
        "android",
        Some(&user),
        Value::Null,
    )
    .await;
    assert_eq!(status, StatusCode::OK);
    for row in &applications {
        assert!(cards
            .as_array()
            .unwrap()
            .iter()
            .any(|card| card["id"] == row["id"]));
        let id = row["id"].as_str().unwrap();
        assert_eq!(
            call(
                &router,
                "GET",
                &format!("/api/console/applications/{id}"),
                "android",
                Some(&user),
                Value::Null
            )
            .await
            .0,
            StatusCode::OK
        );
        assert_eq!(
            call(
                &router,
                "GET",
                &format!("/api/console/devices/{id}"),
                "android",
                Some(&user),
                Value::Null
            )
            .await
            .0,
            StatusCode::FORBIDDEN
        );
    }
    assert!(!cards.to_string().contains("executable_relative"));
    assert!(!cards.to_string().contains("entry_url"));
    let mut wrong = spec("rdp", "public");
    wrong["launch"]["video"] = json!({"codec":"h264","bitrate_kbps":8000});
    assert_eq!(
        call(
            &router,
            "POST",
            "/api/console/managed/applications",
            "admin_web",
            Some(&admin),
            wrong
        )
        .await
        .0,
        StatusCode::BAD_REQUEST
    );
    let (_, private) = call(
        &router,
        "POST",
        "/api/console/managed/applications",
        "admin_web",
        Some(&admin),
        spec("webview", "acl"),
    )
    .await;
    let id = private["id"].as_str().unwrap();
    let path = format!("/api/console/applications/{id}");
    assert_eq!(
        call(&router, "GET", &path, "android", Some(&user), Value::Null)
            .await
            .0,
        StatusCode::FORBIDDEN
    );
    let (_, group) = call(
        &router,
        "POST",
        "/api/console/groups",
        "admin_web",
        Some(&admin),
        json!({"name":Uuid::new_v4().to_string(),"remark":""}),
    )
    .await;
    let (_, profile) = call(
        &router,
        "GET",
        "/api/console/session",
        "android",
        Some(&user),
        Value::Null,
    )
    .await;
    assert_eq!(
        call(
            &router,
            "PUT",
            &format!(
                "/api/console/groups/{}/members",
                group["id"].as_str().unwrap()
            ),
            "admin_web",
            Some(&admin),
            json!({"revision":1,"members":[profile["id"]]})
        )
        .await
        .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &router,
            "PUT",
            &format!("/api/console/managed/applications/{id}/groups"),
            "admin_web",
            Some(&admin),
            json!({"revision":1,"groups":[group["id"]]})
        )
        .await
        .0,
        StatusCode::OK
    );
    let user = login(&router, &name, PASSWORD, "android").await;
    assert_eq!(
        call(&router, "GET", &path, "android", Some(&user), Value::Null)
            .await
            .0,
        StatusCode::OK
    );
    assert_eq!(
        call(
            &router,
            "DELETE",
            &format!("/api/console/managed/applications/{id}?revision=2"),
            "admin_web",
            Some(&admin),
            Value::Null
        )
        .await
        .0,
        StatusCode::NO_CONTENT
    );
    assert_ne!(
        call(&router, "GET", &path, "android", Some(&user), Value::Null)
            .await
            .0,
        StatusCode::OK
    );
    runtime.shutdown().await;
}
#[tokio::test]
async fn node_and_deployment_management_cannot_manufacture_readiness_or_change_target_mode() {
    let runtime = start().await;
    let router = runtime.router();
    let admin = login(&router, "initial-admin", PASSWORD, "admin_web").await;
    let device = create_device(&router, &admin).await;
    let (status, node) = call(
        &router,
        "POST",
        "/api/console/managed/nodes",
        "admin_web",
        Some(&admin),
        json!({"device_id":device["device"]["id"],"product":"cloud_node","max_instances":4}),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED, "{node}");
    assert_ne!(node["node_token"], device["enrollment_token"]);
    assert_eq!(node["node"]["fresh"], false);
    let node_id = node["node"]["id"].as_str().unwrap();
    let (status, app) = call(
        &router,
        "POST",
        "/api/console/managed/applications",
        "admin_web",
        Some(&admin),
        spec("rdp", "public"),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED);
    let configuration =
        json!({"target":{"kind":"rdp"},"gpu_key":null,"capacity":1,"disabled":false});
    let (status, deployment) = call(
        &router,
        "POST",
        "/api/console/managed/deployments",
        "admin_web",
        Some(&admin),
        json!({"application_id":app["id"],"node_id":node_id,"configuration":configuration}),
    )
    .await;
    assert_eq!(status, StatusCode::CREATED, "{deployment}");
    assert_eq!(deployment["observed_state"], "pending");
    let path = format!(
        "/api/console/managed/deployments/{}",
        deployment["id"].as_str().unwrap()
    );
    let invalid = json!({"revision":1,"configuration":{"target":{"kind":"webview"},"gpu_key":null,"capacity":1,"disabled":false}});
    assert_ne!(
        call(&router, "PATCH", &path, "admin_web", Some(&admin), invalid)
            .await
            .0,
        StatusCode::OK
    );
    let fake = json!({"sequence":1,"ready":true,"generation":1});
    assert_eq!(
        call(
            &router,
            "POST",
            &format!("/api/console/managed/nodes/{node_id}/report"),
            "admin_web",
            Some(&admin),
            fake
        )
        .await
        .0,
        StatusCode::NOT_FOUND
    );
    let (status, list) = call(
        &router,
        "GET",
        "/api/console/managed/nodes?limit=100",
        "admin_web",
        Some(&admin),
        Value::Null,
    )
    .await;
    assert_eq!(status, StatusCode::OK);
    assert!(!list
        .to_string()
        .contains(node["node_token"].as_str().unwrap()));
    assert!(!list.to_string().contains("credential_hash"));
    let path = format!("/api/console/managed/nodes/{node_id}");
    assert_eq!(call(&router,"PATCH",&path,"admin_web",Some(&admin),json!({"revision":1,"configuration":{"draining":true,"disabled":false,"max_instances":2}})).await.0,StatusCode::OK);
    assert_eq!(call(&router,"PATCH",&path,"admin_web",Some(&admin),json!({"revision":1,"configuration":{"draining":false,"disabled":false,"max_instances":4}})).await.0,StatusCode::FORBIDDEN);
    runtime.shutdown().await;
}
