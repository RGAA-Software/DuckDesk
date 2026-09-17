#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, token, Fixture};
use px_console_store::{
    AudioCapturePreference, ClientType, CreateSavedConnection, DeploymentTarget, DeviceAccess,
    SavedConnectionSettings, SavedConnectionStore, SavedConnectionTarget, StoreError, TokenDigest,
};
use std::{env, sync::Arc, time::Duration};
use tokio::sync::Barrier;
use uuid::Uuid;
async fn store() -> SavedConnectionStore {
    SavedConnectionStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap()
}
fn request(app: Uuid) -> CreateSavedConnection {
    CreateSavedConnection {
        request_id: Uuid::new_v4(),
        target: SavedConnectionTarget::CloudApplication {
            application_id: app,
        },
        settings: SavedConnectionSettings {
            name: "远程 云应用".into(),
            video_bitrate_bps: 10_000_000,
            video_fps: 60,
            audio_enabled: true,
            clipboard_enabled: true,
            view_only: false,
            maximize: false,
            split_windows: false,
            prefer_peer_to_peer: true,
            audio_capture: AudioCapturePreference::SystemMix,
            background_rgb: 0,
        },
    }
}
async fn fresh_login(fixture: &Fixture, user: Uuid, client: ClientType) -> TokenDigest {
    let revision: i64 =
        sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
            .bind(user)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    let key = token();
    fixture
        .identity
        .issue_session(user, revision, &key, client, Duration::from_secs(3600))
        .await
        .unwrap();
    key
}
async fn permission(fixture: &Fixture, allow: bool) {
    sqlx::query(if allow {
        "GRANT INSERT ON pixels.saved_connection_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.saved_connection_events FROM pixels_console_runtime"
    })
    .execute(&fixture.owner)
    .await
    .unwrap();
}
#[tokio::test]
async fn owner_client_and_explicit_target_are_not_legacy_device_or_account_fallbacks() {
    let fixture = Fixture::new().await;
    let connection_store = store().await;
    let app = fixture.app(&DeploymentTarget::Webview).await;
    let user = fixture.session("user", ClientType::Android).await;
    let owner = fixture
        .identity
        .authenticate(&user, ClientType::Android)
        .await
        .unwrap()
        .user_id;
    let panel = fresh_login(&fixture, owner, ClientType::Panel).await;
    let other = fixture.session("user", ClientType::Android).await;
    let create_request = request(app.id);
    let android_connection = connection_store
        .create(&user, ClientType::Android, &create_request)
        .await
        .unwrap();
    let panel_connection = connection_store
        .create(&panel, ClientType::Panel, &create_request)
        .await
        .unwrap();
    let other_user_connection = connection_store
        .create(&other, ClientType::Android, &create_request)
        .await
        .unwrap();
    assert_ne!(android_connection.id, panel_connection.id);
    assert_ne!(android_connection.id, other_user_connection.id);
    assert_eq!(android_connection.owner_id, owner);
    assert_eq!(android_connection.client_type, "android");
    assert_eq!(android_connection.target, create_request.target);
    assert!(connection_store
        .get(&other, ClientType::Android, android_connection.id)
        .await
        .is_err());
    assert!(connection_store
        .get(&panel, ClientType::Panel, android_connection.id)
        .await
        .is_err());
    assert!(connection_store
        .get(&user, ClientType::Panel, android_connection.id)
        .await
        .is_err());
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    for admin in [&fixture.admin, &viewer] {
        assert!(connection_store
            .create(admin, ClientType::AdminWeb, &create_request)
            .await
            .is_err());
    }
    let (guest, _) = fixture.guest().await;
    assert!(connection_store
        .create(&guest, ClientType::Android, &create_request)
        .await
        .is_err());
    let mut json = serde_json::json!({"request_id":create_request.request_id,"target":create_request.target,"settings":create_request.settings});
    json["owner_id"] = serde_json::json!(owner);
    assert!(serde_json::from_value::<CreateSavedConnection>(json).is_err());
    for invalid in [
        serde_json::json!({"kind":"cloud_application","device_id":app.id}),
        serde_json::json!({"kind":"cloud_application","application_id":app.id,"instance_id":Uuid::new_v4()}),
        serde_json::json!({"kind":"account","account_id":owner}),
        serde_json::json!({"kind":"desktop","device_id":app.id,"host":"example.test"}),
    ] {
        assert!(serde_json::from_value::<SavedConnectionTarget>(invalid).is_err());
    }
    let json = serde_json::to_string(&android_connection).unwrap();
    for secret in [
        "request_hash",
        "password",
        "host",
        "port",
        "token",
        "instance_id",
    ] {
        assert!(!json.contains(secret));
    }
    sqlx::query("UPDATE pixels.login_sessions SET created_at=clock_timestamp()-interval '2 seconds',expires_at=clock_timestamp()-interval '1 second' WHERE user_id=$1 AND client_type='android'")
        .bind(owner).execute(&fixture.owner).await.unwrap();
    assert!(connection_store
        .get(&user, ClientType::Android, android_connection.id)
        .await
        .is_err());
    assert!(connection_store
        .create(&user, ClientType::Android, &create_request)
        .await
        .is_err());
    assert!(connection_store
        .delete(&user, ClientType::Android, android_connection.id, 1)
        .await
        .is_err());
    assert!(connection_store
        .get(&panel, ClientType::Panel, panel_connection.id)
        .await
        .is_ok());
    connection_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn concurrent_create_is_exactly_idempotent_and_cannot_reuse_request_for_another_body() {
    let fixture = Fixture::new().await;
    let connection_store = store().await;
    let app = fixture.app(&DeploymentTarget::Webview).await;
    let user = fixture.session("user", ClientType::Android).await;
    let create_request = request(app.id);
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (connection_store, user, create_request, start_barrier) = (
            connection_store.clone(),
            user.clone(),
            create_request.clone(),
            barrier.clone(),
        );
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            connection_store
                .create(&user, ClientType::Android, &create_request)
                .await
                .unwrap()
        }));
    }
    let mut rows = Vec::new();
    for task in tasks {
        rows.push(task.await.unwrap());
    }
    assert!(rows.iter().all(|row| row == &rows[0]));
    let mut changed = create_request.clone();
    changed.settings.video_fps = 120;
    assert!(connection_store
        .create(&user, ClientType::Android, &changed)
        .await
        .is_err());
    let events: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.saved_connection_events WHERE connection_id=$1",
    )
    .bind(rows[0].id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(events, 1);
    assert_eq!(
        connection_store
            .list(&user, ClientType::Android, None, 100)
            .await
            .unwrap()
            .len(),
        1
    );
    connection_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn concurrent_updates_have_one_cas_winner_and_delete_retries_do_not_resurrect() {
    let fixture = Fixture::new().await;
    let connection_store = store().await;
    let app = fixture.app(&DeploymentTarget::Rdp).await;
    let user = fixture.session("user", ClientType::Panel).await;
    let create_request = request(app.id);
    let first = connection_store
        .create(&user, ClientType::Panel, &create_request)
        .await
        .unwrap();
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for contender_index in 0..20 {
        let (connection_store, user, start_barrier, mut settings) = (
            connection_store.clone(),
            user.clone(),
            barrier.clone(),
            create_request.settings.clone(),
        );
        settings.name = format!("setting {contender_index}");
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            connection_store
                .update(&user, ClientType::Panel, first.id, 1, &settings)
                .await
        }));
    }
    let mut successes = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(row) => {
                assert_eq!(row.revision, 2);
                successes += 1;
            }
            Err(StoreError::Rejected) => {}
            other => panic!("unexpected {other:?}"),
        }
    }
    assert_eq!(successes, 1);
    let current = connection_store
        .get(&user, ClientType::Panel, first.id)
        .await
        .unwrap();
    assert_eq!(
        current,
        connection_store
            .create(&user, ClientType::Panel, &create_request)
            .await
            .unwrap()
    );
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (connection_store, user, start_barrier) =
            (connection_store.clone(), user.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            connection_store
                .delete(&user, ClientType::Panel, first.id, 2)
                .await
                .unwrap()
        }));
    }
    for task in tasks {
        let row = task.await.unwrap();
        assert!(row.deleted_at.is_some());
        assert_eq!(row.revision, 3);
    }
    assert!(connection_store
        .create(&user, ClientType::Panel, &create_request)
        .await
        .is_err());
    assert!(connection_store
        .update(
            &user,
            ClientType::Panel,
            first.id,
            3,
            &create_request.settings
        )
        .await
        .is_err());
    assert!(connection_store
        .delete(&user, ClientType::Panel, first.id, 1)
        .await
        .is_err());
    assert!(connection_store
        .list(&user, ClientType::Panel, None, 1)
        .await
        .unwrap()
        .is_empty());
    let count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.saved_connection_events WHERE connection_id=$1",
    )
    .bind(first.id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(count, 3);
    // Preferences have not allocated or authorized an RDP frontend.
    let sessions: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.resource_sessions WHERE application_id=$1")
            .bind(app.id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(sessions, 0);
    connection_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn current_device_acl_is_required_for_edits_but_revoked_targets_remain_owner_removable() {
    let fixture = Fixture::new().await;
    let connection_store = store().await;
    let (node, _) = fixture.connected().await;
    let device: Uuid = sqlx::query_scalar("SELECT device_id FROM pixels.nodes WHERE id=$1")
        .bind(node.id())
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    let key = fixture.session("user", ClientType::Android).await;
    let owner = fixture
        .identity
        .authenticate(&key, ClientType::Android)
        .await
        .unwrap()
        .user_id;
    let mut create_request = request(Uuid::new_v4());
    create_request.target = SavedConnectionTarget::Desktop { device_id: device };
    assert!(connection_store
        .create(&key, ClientType::Android, &create_request)
        .await
        .is_err());
    fixture
        .devices
        .replace_access(
            &fixture.admin,
            device,
            1,
            &DeviceAccess {
                users: vec![owner],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    let key = fresh_login(&fixture, owner, ClientType::Android).await;
    let saved = connection_store
        .create(&key, ClientType::Android, &create_request)
        .await
        .unwrap();
    fixture
        .devices
        .replace_access(
            &fixture.admin,
            device,
            2,
            &DeviceAccess {
                users: vec![],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    assert!(connection_store
        .get(&key, ClientType::Android, saved.id)
        .await
        .is_err()); // old login invalidated
    let current = fresh_login(&fixture, owner, ClientType::Android).await;
    assert_eq!(
        connection_store
            .get(&current, ClientType::Android, saved.id)
            .await
            .unwrap(),
        saved
    );
    assert!(connection_store
        .update(
            &current,
            ClientType::Android,
            saved.id,
            1,
            &create_request.settings
        )
        .await
        .is_err());
    let mut new = create_request.clone();
    new.request_id = Uuid::new_v4();
    assert!(connection_store
        .create(&current, ClientType::Android, &new)
        .await
        .is_err());
    assert!(connection_store
        .delete(&current, ClientType::Android, saved.id, 1)
        .await
        .unwrap()
        .deleted_at
        .is_some());
    connection_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn audit_failure_rolls_back_create_update_delete_and_runtime_cannot_reassign_identity() {
    let fixture = Fixture::new().await;
    let connection_store = store().await;
    let app = fixture.app(&DeploymentTarget::Webview).await;
    let user = fixture.session("user", ClientType::Android).await;
    let create_request = request(app.id);
    permission(&fixture, false).await;
    let failure = connection_store
        .create(&user, ClientType::Android, &create_request)
        .await;
    permission(&fixture, true).await;
    assert!(failure.is_err());
    assert!(connection_store
        .list(&user, ClientType::Android, None, 1)
        .await
        .unwrap()
        .is_empty());
    let saved = connection_store
        .create(&user, ClientType::Android, &create_request)
        .await
        .unwrap();
    let mut settings = create_request.settings.clone();
    settings.name = "changed".into();
    permission(&fixture, false).await;
    let failure = connection_store
        .update(&user, ClientType::Android, saved.id, 1, &settings)
        .await;
    permission(&fixture, true).await;
    assert!(failure.is_err());
    assert_eq!(
        connection_store
            .get(&user, ClientType::Android, saved.id)
            .await
            .unwrap(),
        saved
    );
    permission(&fixture, false).await;
    let failure = connection_store
        .delete(&user, ClientType::Android, saved.id, 1)
        .await;
    permission(&fixture, true).await;
    assert!(failure.is_err());
    assert_eq!(
        connection_store
            .get(&user, ClientType::Android, saved.id)
            .await
            .unwrap(),
        saved
    );
    let runtime = config("RUNTIME").connect().await.unwrap();
    for query in ["DELETE FROM pixels.saved_connections WHERE id=$1",
        "UPDATE pixels.saved_connections SET owner_id=gen_random_uuid() WHERE id=$1",
        "UPDATE pixels.saved_connections SET application_id=gen_random_uuid() WHERE id=$1",
        "UPDATE pixels.saved_connections SET request_hash=decode(repeat('00',32),'hex') WHERE id=$1"] {
        assert!(sqlx::query(query).bind(saved.id).execute(&runtime).await.is_err());
    }
    runtime.close().await;
    connection_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn active_limit_is_atomic_pages_are_bounded_and_tombstones_do_not_consume_slots() {
    let fixture = Fixture::new().await;
    let connection_store = store().await;
    let app = fixture.app(&DeploymentTarget::Webview).await;
    let user = fixture.session("user", ClientType::Android).await;
    let mut ids = Vec::new();
    for _ in 0..127 {
        ids.push(
            connection_store
                .create(&user, ClientType::Android, &request(app.id))
                .await
                .unwrap()
                .id,
        );
    }
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (connection_store, user, start_barrier) =
            (connection_store.clone(), user.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            connection_store
                .create(&user, ClientType::Android, &request(app.id))
                .await
        }));
    }
    let mut success = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(row) => {
                success += 1;
                ids.push(row.id);
            }
            Err(StoreError::NoCapacity) => {}
            other => panic!("unexpected {other:?}"),
        }
    }
    assert_eq!(success, 1);
    ids.sort();
    let mut collected = Vec::new();
    let mut after = None;
    loop {
        let page = connection_store
            .list(&user, ClientType::Android, after, 17)
            .await
            .unwrap();
        if page.is_empty() {
            break;
        }
        after = page.last().map(|saved_connection| saved_connection.id);
        collected.extend(page.iter().map(|saved_connection| saved_connection.id));
    }
    assert_eq!(collected, ids);
    assert!(connection_store
        .list(&user, ClientType::Android, None, 0)
        .await
        .is_err());
    assert!(connection_store
        .list(&user, ClientType::Android, None, 101)
        .await
        .is_err());
    connection_store
        .delete(&user, ClientType::Android, ids[0], 1)
        .await
        .unwrap();
    connection_store
        .create(&user, ClientType::Android, &request(app.id))
        .await
        .unwrap();
    connection_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn settings_survive_pool_restart_but_disabled_application_cannot_be_edited_or_started_from_them(
) {
    let fixture = Fixture::new().await;
    let connection_store = store().await;
    let app = fixture.app(&DeploymentTarget::Webview).await;
    let user = fixture.session("user", ClientType::Android).await;
    let create_request = request(app.id);
    let saved = connection_store
        .create(&user, ClientType::Android, &create_request)
        .await
        .unwrap();
    connection_store.close().await;
    assert!(connection_store
        .get(&user, ClientType::Android, saved.id)
        .await
        .is_err());
    let reopened = store().await;
    assert_eq!(
        saved,
        reopened
            .get(&user, ClientType::Android, saved.id)
            .await
            .unwrap()
    );
    // Simulate a disabled catalog entry; access must be checked even with a live login.
    sqlx::query("UPDATE pixels.applications SET disabled=true WHERE id=$1")
        .bind(app.id)
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(reopened
        .update(
            &user,
            ClientType::Android,
            saved.id,
            1,
            &create_request.settings
        )
        .await
        .is_err());
    let mut fresh = create_request.clone();
    fresh.request_id = Uuid::new_v4();
    assert!(reopened
        .create(&user, ClientType::Android, &fresh)
        .await
        .is_err());
    assert_eq!(
        saved,
        reopened
            .get(&user, ClientType::Android, saved.id)
            .await
            .unwrap()
    );
    // Exact create retry only returns the old preference; it has no launch side effect.
    assert_eq!(
        saved,
        reopened
            .create(&user, ClientType::Android, &create_request)
            .await
            .unwrap()
    );
    let instances: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.instances WHERE application_id=$1")
            .bind(app.id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(instances, 0);
    let mut invalid = create_request;
    invalid.request_id = Uuid::new_v4();
    invalid.settings.video_bitrate_bps = 0;
    assert!(matches!(
        reopened.create(&user, ClientType::Android, &invalid).await,
        Err(StoreError::InvalidInput)
    ));
    reopened.close().await;
    fixture.close().await;
}
