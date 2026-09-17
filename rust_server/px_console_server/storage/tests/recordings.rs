#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, node_report, token, Fixture};
use px_console_store::{
    ClientType, CommandOutcome, CommandReceipt, DeploymentTarget, DeviceAccess, NodeCommandAction,
    OpenResourceSession, RecordingCodec, RecordingReport, RecordingStore, ResourceCredential,
    ResourceSessionStore, SessionAccess, SessionTarget,
};
use std::{env, sync::Arc, time::Duration};
use tokio::sync::Barrier;
use uuid::Uuid;
async fn store() -> RecordingStore {
    RecordingStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap()
}
fn report() -> RecordingReport {
    RecordingReport {
        source_id: Uuid::new_v4(),
        source_sha256: [18; 32],
        session_id: None,
        file_name: "录屏 1.mp4".into(),
        size_bytes: 1024,
        modified_unix_ms: 1_800_000_000_000,
        codec: RecordingCodec::H264,
        sequence: 1,
        present: true,
    }
}
async fn permission(f: &Fixture, allow: bool) {
    sqlx::query(if allow {
        "GRANT INSERT ON pixels.recording_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.recording_events FROM pixels_console_runtime"
    })
    .execute(&f.owner)
    .await
    .unwrap();
}
#[tokio::test]
async fn autonomous_records_have_immutable_version_identity_not_filename_or_session_fallback() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, _) = f.connected().await;
    let mut request = report();
    let first = s.report(&node, &request).await.unwrap();
    assert!(first.session_id.is_none());
    assert_eq!(first, s.report(&node, &request).await.unwrap());
    let mut changed = request.clone();
    changed.source_sha256 = [19; 32];
    assert!(s.report(&node, &changed).await.is_err());
    changed.source_sha256 = request.source_sha256;
    changed.size_bytes += 1;
    changed.sequence += 1;
    assert!(s.report(&node, &changed).await.is_err());
    changed.source_id = Uuid::new_v4();
    changed.sequence = 1;
    let replacement = s.report(&node, &changed).await.unwrap();
    assert_ne!(first.id, replacement.id);
    assert_eq!(first.file_name, replacement.file_name);
    request.sequence = 2;
    request.present = false;
    let absent = s.report(&node, &request).await.unwrap();
    assert!(!absent.reported_present);
    assert_eq!(absent.revision, 2);
    let mut conflicting = request.clone();
    conflicting.present = true;
    assert!(s.report(&node, &conflicting).await.is_err());
    request.sequence = 1;
    assert!(s.report(&node, &request).await.is_err());
    assert_eq!(
        s.list_managed(&f.admin, Some(node.id()), None, 100)
            .await
            .unwrap()
            .len(),
        2
    );
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn concurrent_duplicate_observations_have_one_catalog_identity_and_ordered_events() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, _) = f.connected().await;
    let request = report();
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (s, node, r, b) = (s.clone(), node.clone(), request.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            s.report(&node, &r).await.unwrap()
        }));
    }
    let mut rows = Vec::new();
    for task in tasks {
        rows.push(task.await.unwrap());
    }
    assert!(rows.iter().all(|r| r.id == rows[0].id && r.revision == 1));
    let id = rows[0].id;
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for index in 0..20 {
        let (s, node, mut r, b) = (s.clone(), node.clone(), request.clone(), barrier.clone());
        r.sequence = 2;
        r.present = index % 2 == 0;
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            s.report(&node, &r).await
        }));
    }
    let mut successes = Vec::new();
    let mut rejected = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(row) => successes.push(row),
            Err(_) => rejected += 1,
        }
    }
    assert_eq!(rejected, 10);
    assert_eq!(successes.len(), 10);
    assert!(successes
        .iter()
        .all(|r| r.revision == 2 && r.reported_present == successes[0].reported_present));
    let events: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.recording_events WHERE recording_id=$1")
            .bind(id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(events, 2);
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn reconnect_requires_current_node_and_explicit_new_generation_observation() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, key) = f.connected().await;
    let mut request = report();
    request.sequence = 50;
    let first = s.report(&node, &request).await.unwrap();
    f.nodes.close_connection(&node).await.unwrap();
    let offline = s
        .list_managed(&f.admin, Some(node.id()), None, 1)
        .await
        .unwrap()
        .remove(0);
    assert_eq!(offline, first); // A historical presence observation, not a claim of current availability.
    assert!(s.report(&node, &request).await.is_err());
    let current = f
        .nodes
        .open_connection(node.epoch(), &key, &token())
        .await
        .unwrap();
    f.nodes.report(&current, &node_report(1)).await.unwrap();
    request.sequence = 1;
    let reobserved = s.report(&current, &request).await.unwrap();
    assert_eq!(reobserved.id, first.id);
    assert_eq!(reobserved.revision, 2);
    assert!(reobserved.node_generation > first.node_generation);
    assert!(s.report(&node, &request).await.is_err());
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn optional_session_origin_is_exact_node_fk_and_cannot_authorize_cross_node_reporting() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, app, _) = f.prepared(DeploymentTarget::Webview, 4).await;
    let (user, instance, command) = f.started(&node, app.id).await;
    let port = match command.action {
        NodeCommandAction::Start { port, .. } => port,
        _ => panic!("start required"),
    };
    f.instances
        .acknowledge_command(
            &node,
            &CommandReceipt {
                command_id: command.id,
                lease_id: command.lease_id,
                instance_id: instance.id,
                launch_id: command.launch_id,
                instance_revision: command.instance_revision,
                outcome: CommandOutcome::Running { port },
            },
        )
        .await
        .unwrap();
    let sessions = ResourceSessionStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap();
    let session = sessions
        .open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &OpenResourceSession {
                request_id: Uuid::new_v4(),
                target: SessionTarget::CloudApplication {
                    application_id: app.id,
                    instance_id: instance.id,
                },
                access: SessionAccess::Controller,
            },
        )
        .await
        .unwrap();
    let mut r = report();
    r.session_id = Some(session.id);
    let first = s.report(&node, &r).await.unwrap();
    assert_eq!(first.session_id, Some(session.id));
    let (_, key) = f.node().await;
    let other = f
        .nodes
        .open_connection(node.epoch(), &key, &token())
        .await
        .unwrap();
    f.nodes.report(&other, &node_report(1)).await.unwrap();
    assert!(s.report(&other, &r).await.is_err());
    r.session_id = None;
    let own = s.report(&other, &r).await.unwrap();
    assert_ne!(first.id, own.id);
    let mut missing = report();
    missing.present = false;
    assert!(s.report(&node, &missing).await.is_err());
    missing.present = true;
    missing.session_id = Some(Uuid::new_v4());
    assert!(s.report(&node, &missing).await.is_err());
    sessions.close().await;
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn failed_recording_audit_rolls_back_creation_observation_and_immutable_metadata() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, _) = f.connected().await;
    let mut r = report();
    permission(&f, false).await;
    let failed = s.report(&node, &r).await;
    permission(&f, true).await;
    assert!(failed.is_err());
    assert!(s
        .list_managed(&f.admin, Some(node.id()), None, 100)
        .await
        .unwrap()
        .is_empty());
    let first = s.report(&node, &r).await.unwrap();
    r.sequence = 2;
    r.present = false;
    permission(&f, false).await;
    let failed = s.report(&node, &r).await;
    permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(
        first,
        s.list_managed(&f.admin, Some(node.id()), None, 100)
            .await
            .unwrap()[0]
    );
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(sqlx::query("DELETE FROM pixels.recordings WHERE id=$1")
        .bind(first.id)
        .execute(&runtime)
        .await
        .is_err());
    assert!(
        sqlx::query("UPDATE pixels.recordings SET file_name='replaced.mp4' WHERE id=$1")
            .bind(first.id)
            .execute(&runtime)
            .await
            .is_err()
    );
    runtime.close().await;
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn node_library_requires_current_device_acl_and_has_bounded_secret_free_history() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, _) = f.connected().await;
    let mut ids = Vec::new();
    for _ in 0..3 {
        ids.push(s.report(&node, &report()).await.unwrap().id);
    }
    ids.sort();
    let user = f.session("user", ClientType::Android).await;
    assert!(s
        .list_visible(&user, ClientType::Android, node.id(), None, 100)
        .await
        .is_err());
    let identity = f
        .identity
        .authenticate(&user, ClientType::Android)
        .await
        .unwrap();
    let device: Uuid = sqlx::query_scalar("SELECT device_id FROM pixels.nodes WHERE id=$1")
        .bind(node.id())
        .fetch_one(&f.owner)
        .await
        .unwrap();
    f.devices
        .replace_access(
            &f.admin,
            device,
            1,
            &DeviceAccess {
                users: vec![identity.user_id],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    let rev: i64 =
        sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
            .bind(identity.user_id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    let user = token();
    f.identity
        .issue_session(
            identity.user_id,
            rev,
            &user,
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let mut after = None;
    let mut found = Vec::new();
    loop {
        let page = s
            .list_visible(&user, ClientType::Android, node.id(), after, 1)
            .await
            .unwrap();
        if page.is_empty() {
            break;
        }
        let record = &page[0];
        found.push(record.id);
        after = Some(record.id);
        let text = serde_json::to_string(record).unwrap();
        for secret in ["metadata_hash", "source_id", "url", "password", "token"] {
            assert!(!text.contains(secret));
        }
    }
    assert_eq!(found, ids);
    let viewer = f.session("viewer", ClientType::AdminWeb).await;
    assert_eq!(
        s.list_managed(&viewer, Some(node.id()), None, 100)
            .await
            .unwrap()
            .len(),
        3
    );
    assert!(s.list_managed(&viewer, None, None, 101).await.is_err());
    assert!(s.list_managed(&user, None, None, 1).await.is_err());
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
    assert!(s
        .list_visible(&user, ClientType::Android, node.id(), None, 100)
        .await
        .is_err());
    s.close().await;
    assert!(s.list_managed(&viewer, None, None, 1).await.is_err());
    let reopened = store().await;
    assert_eq!(
        reopened
            .list_managed(&viewer, Some(node.id()), None, 100)
            .await
            .unwrap()
            .len(),
        3
    );
    reopened.close().await;
    f.close().await;
}
