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
async fn fresh_login(f: &Fixture, user: Uuid, client: ClientType) -> TokenDigest {
    let revision: i64 =
        sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
            .bind(user)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    let key = token();
    f.identity
        .issue_session(user, revision, &key, client, Duration::from_secs(3600))
        .await
        .unwrap();
    key
}
async fn permission(f: &Fixture, allow: bool) {
    sqlx::query(if allow {
        "GRANT INSERT ON pixels.saved_connection_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.saved_connection_events FROM pixels_console_runtime"
    })
    .execute(&f.owner)
    .await
    .unwrap();
}
#[tokio::test]
async fn owner_client_and_explicit_target_are_not_legacy_device_or_account_fallbacks() {
    let f = Fixture::new().await;
    let s = store().await;
    let app = f.app(&DeploymentTarget::Webview).await;
    let user = f.session("user", ClientType::Android).await;
    let owner = f
        .identity
        .authenticate(&user, ClientType::Android)
        .await
        .unwrap()
        .user_id;
    let panel = fresh_login(&f, owner, ClientType::Panel).await;
    let other = f.session("user", ClientType::Android).await;
    let r = request(app.id);
    let a = s.create(&user, ClientType::Android, &r).await.unwrap();
    let p = s.create(&panel, ClientType::Panel, &r).await.unwrap();
    let b = s.create(&other, ClientType::Android, &r).await.unwrap();
    assert_ne!(a.id, p.id);
    assert_ne!(a.id, b.id);
    assert_eq!(a.owner_id, owner);
    assert_eq!(a.client_type, "android");
    assert_eq!(a.target, r.target);
    assert!(s.get(&other, ClientType::Android, a.id).await.is_err());
    assert!(s.get(&panel, ClientType::Panel, a.id).await.is_err());
    assert!(s.get(&user, ClientType::Panel, a.id).await.is_err());
    let viewer = f.session("viewer", ClientType::AdminWeb).await;
    for admin in [&f.admin, &viewer] {
        assert!(s.create(admin, ClientType::AdminWeb, &r).await.is_err());
    }
    let (guest, _) = f.guest().await;
    assert!(s.create(&guest, ClientType::Android, &r).await.is_err());
    let mut json =
        serde_json::json!({"request_id":r.request_id,"target":r.target,"settings":r.settings});
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
    let json = serde_json::to_string(&a).unwrap();
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
        .bind(owner).execute(&f.owner).await.unwrap();
    assert!(s.get(&user, ClientType::Android, a.id).await.is_err());
    assert!(s.create(&user, ClientType::Android, &r).await.is_err());
    assert!(s.delete(&user, ClientType::Android, a.id, 1).await.is_err());
    assert!(s.get(&panel, ClientType::Panel, p.id).await.is_ok());
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn concurrent_create_is_exactly_idempotent_and_cannot_reuse_request_for_another_body() {
    let f = Fixture::new().await;
    let s = store().await;
    let app = f.app(&DeploymentTarget::Webview).await;
    let user = f.session("user", ClientType::Android).await;
    let r = request(app.id);
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (s, user, r, b) = (s.clone(), user.clone(), r.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            s.create(&user, ClientType::Android, &r).await.unwrap()
        }));
    }
    let mut rows = Vec::new();
    for task in tasks {
        rows.push(task.await.unwrap());
    }
    assert!(rows.iter().all(|row| row == &rows[0]));
    let mut changed = r.clone();
    changed.settings.video_fps = 120;
    assert!(s
        .create(&user, ClientType::Android, &changed)
        .await
        .is_err());
    let events: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.saved_connection_events WHERE connection_id=$1",
    )
    .bind(rows[0].id)
    .fetch_one(&f.owner)
    .await
    .unwrap();
    assert_eq!(events, 1);
    assert_eq!(
        s.list(&user, ClientType::Android, None, 100)
            .await
            .unwrap()
            .len(),
        1
    );
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn concurrent_updates_have_one_cas_winner_and_delete_retries_do_not_resurrect() {
    let f = Fixture::new().await;
    let s = store().await;
    let app = f.app(&DeploymentTarget::Rdp).await;
    let user = f.session("user", ClientType::Panel).await;
    let r = request(app.id);
    let first = s.create(&user, ClientType::Panel, &r).await.unwrap();
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for i in 0..20 {
        let (s, user, b, mut settings) =
            (s.clone(), user.clone(), barrier.clone(), r.settings.clone());
        settings.name = format!("setting {i}");
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            s.update(&user, ClientType::Panel, first.id, 1, &settings)
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
    let current = s.get(&user, ClientType::Panel, first.id).await.unwrap();
    assert_eq!(
        current,
        s.create(&user, ClientType::Panel, &r).await.unwrap()
    );
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (s, user, b) = (s.clone(), user.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            s.delete(&user, ClientType::Panel, first.id, 2)
                .await
                .unwrap()
        }));
    }
    for task in tasks {
        let row = task.await.unwrap();
        assert!(row.deleted_at.is_some());
        assert_eq!(row.revision, 3);
    }
    assert!(s.create(&user, ClientType::Panel, &r).await.is_err());
    assert!(s
        .update(&user, ClientType::Panel, first.id, 3, &r.settings)
        .await
        .is_err());
    assert!(s
        .delete(&user, ClientType::Panel, first.id, 1)
        .await
        .is_err());
    assert!(s
        .list(&user, ClientType::Panel, None, 1)
        .await
        .unwrap()
        .is_empty());
    let count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.saved_connection_events WHERE connection_id=$1",
    )
    .bind(first.id)
    .fetch_one(&f.owner)
    .await
    .unwrap();
    assert_eq!(count, 3);
    // Preferences have not allocated or authorized an RDP frontend.
    let sessions: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.resource_sessions WHERE application_id=$1")
            .bind(app.id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(sessions, 0);
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn current_device_acl_is_required_for_edits_but_revoked_targets_remain_owner_removable() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, _) = f.connected().await;
    let device: Uuid = sqlx::query_scalar("SELECT device_id FROM pixels.nodes WHERE id=$1")
        .bind(node.id())
        .fetch_one(&f.owner)
        .await
        .unwrap();
    let key = f.session("user", ClientType::Android).await;
    let owner = f
        .identity
        .authenticate(&key, ClientType::Android)
        .await
        .unwrap()
        .user_id;
    let mut r = request(Uuid::new_v4());
    r.target = SavedConnectionTarget::Desktop { device_id: device };
    assert!(s.create(&key, ClientType::Android, &r).await.is_err());
    f.devices
        .replace_access(
            &f.admin,
            device,
            1,
            &DeviceAccess {
                users: vec![owner],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    let key = fresh_login(&f, owner, ClientType::Android).await;
    let saved = s.create(&key, ClientType::Android, &r).await.unwrap();
    f.devices
        .replace_access(
            &f.admin,
            device,
            2,
            &DeviceAccess {
                users: vec![],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    assert!(s.get(&key, ClientType::Android, saved.id).await.is_err()); // old login invalidated
    let current = fresh_login(&f, owner, ClientType::Android).await;
    assert_eq!(
        s.get(&current, ClientType::Android, saved.id)
            .await
            .unwrap(),
        saved
    );
    assert!(s
        .update(&current, ClientType::Android, saved.id, 1, &r.settings)
        .await
        .is_err());
    let mut new = r.clone();
    new.request_id = Uuid::new_v4();
    assert!(s.create(&current, ClientType::Android, &new).await.is_err());
    assert!(s
        .delete(&current, ClientType::Android, saved.id, 1)
        .await
        .unwrap()
        .deleted_at
        .is_some());
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn audit_failure_rolls_back_create_update_delete_and_runtime_cannot_reassign_identity() {
    let f = Fixture::new().await;
    let s = store().await;
    let app = f.app(&DeploymentTarget::Webview).await;
    let user = f.session("user", ClientType::Android).await;
    let r = request(app.id);
    permission(&f, false).await;
    let failure = s.create(&user, ClientType::Android, &r).await;
    permission(&f, true).await;
    assert!(failure.is_err());
    assert!(s
        .list(&user, ClientType::Android, None, 1)
        .await
        .unwrap()
        .is_empty());
    let saved = s.create(&user, ClientType::Android, &r).await.unwrap();
    let mut settings = r.settings.clone();
    settings.name = "changed".into();
    permission(&f, false).await;
    let failure = s
        .update(&user, ClientType::Android, saved.id, 1, &settings)
        .await;
    permission(&f, true).await;
    assert!(failure.is_err());
    assert_eq!(
        s.get(&user, ClientType::Android, saved.id).await.unwrap(),
        saved
    );
    permission(&f, false).await;
    let failure = s.delete(&user, ClientType::Android, saved.id, 1).await;
    permission(&f, true).await;
    assert!(failure.is_err());
    assert_eq!(
        s.get(&user, ClientType::Android, saved.id).await.unwrap(),
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
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn active_limit_is_atomic_pages_are_bounded_and_tombstones_do_not_consume_slots() {
    let f = Fixture::new().await;
    let s = store().await;
    let app = f.app(&DeploymentTarget::Webview).await;
    let user = f.session("user", ClientType::Android).await;
    let mut ids = Vec::new();
    for _ in 0..127 {
        ids.push(
            s.create(&user, ClientType::Android, &request(app.id))
                .await
                .unwrap()
                .id,
        );
    }
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (s, user, b) = (s.clone(), user.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            s.create(&user, ClientType::Android, &request(app.id)).await
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
        let page = s.list(&user, ClientType::Android, after, 17).await.unwrap();
        if page.is_empty() {
            break;
        }
        after = page.last().map(|r| r.id);
        collected.extend(page.iter().map(|r| r.id));
    }
    assert_eq!(collected, ids);
    assert!(s.list(&user, ClientType::Android, None, 0).await.is_err());
    assert!(s.list(&user, ClientType::Android, None, 101).await.is_err());
    s.delete(&user, ClientType::Android, ids[0], 1)
        .await
        .unwrap();
    s.create(&user, ClientType::Android, &request(app.id))
        .await
        .unwrap();
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn settings_survive_pool_restart_but_disabled_application_cannot_be_edited_or_started_from_them(
) {
    let f = Fixture::new().await;
    let s = store().await;
    let app = f.app(&DeploymentTarget::Webview).await;
    let user = f.session("user", ClientType::Android).await;
    let r = request(app.id);
    let saved = s.create(&user, ClientType::Android, &r).await.unwrap();
    s.close().await;
    assert!(s.get(&user, ClientType::Android, saved.id).await.is_err());
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
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(reopened
        .update(&user, ClientType::Android, saved.id, 1, &r.settings)
        .await
        .is_err());
    let mut fresh = r.clone();
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
            .create(&user, ClientType::Android, &r)
            .await
            .unwrap()
    );
    let instances: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.instances WHERE application_id=$1")
            .bind(app.id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(instances, 0);
    let mut invalid = r;
    invalid.request_id = Uuid::new_v4();
    invalid.settings.video_bitrate_bps = 0;
    assert!(matches!(
        reopened.create(&user, ClientType::Android, &invalid).await,
        Err(StoreError::InvalidInput)
    ));
    reopened.close().await;
    f.close().await;
}
