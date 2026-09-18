#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, node_report, token, Fixture};
use px_console_store::{
    ClientType, CommandOutcome, CommandReceipt, DeploymentTarget, NodeCommandAction,
    OpenResourceSession, RecordingCodec, RecordingReport, RecordingStore, ResourceCredential,
    ResourceSessionStore, SessionAccess, SessionTarget,
};
use std::{env, sync::Arc};
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
async fn permission(fixture: &Fixture, allow: bool) {
    sqlx::query(if allow {
        "GRANT INSERT ON pixels.recording_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.recording_events FROM pixels_console_runtime"
    })
    .execute(&fixture.owner)
    .await
    .unwrap();
}
#[tokio::test]
async fn autonomous_records_have_immutable_version_identity_not_filename_or_session_fallback() {
    let fixture = Fixture::new().await;
    let recording_store = store().await;
    let (node, _) = fixture.connected().await;
    let mut request = report();
    let first = recording_store.report(&node, &request).await.unwrap();
    assert!(first.session_id.is_none());
    assert_eq!(
        first,
        recording_store.report(&node, &request).await.unwrap()
    );
    let mut changed = request.clone();
    changed.source_sha256 = [19; 32];
    assert!(recording_store.report(&node, &changed).await.is_err());
    changed.source_sha256 = request.source_sha256;
    changed.size_bytes += 1;
    changed.sequence += 1;
    assert!(recording_store.report(&node, &changed).await.is_err());
    changed.source_id = Uuid::new_v4();
    changed.sequence = 1;
    let replacement = recording_store.report(&node, &changed).await.unwrap();
    assert_ne!(first.id, replacement.id);
    assert_eq!(first.file_name, replacement.file_name);
    request.sequence = 2;
    request.present = false;
    let absent = recording_store.report(&node, &request).await.unwrap();
    assert!(!absent.reported_present);
    assert_eq!(absent.revision, 2);
    let mut conflicting = request.clone();
    conflicting.present = true;
    assert!(recording_store.report(&node, &conflicting).await.is_err());
    request.sequence = 1;
    assert!(recording_store.report(&node, &request).await.is_err());
    assert_eq!(
        recording_store
            .list_managed(&fixture.admin, Some(node.id()), None, 100)
            .await
            .unwrap()
            .len(),
        2
    );
    recording_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn concurrent_duplicate_observations_have_one_catalog_identity_and_ordered_events() {
    let fixture = Fixture::new().await;
    let recording_store = store().await;
    let (node, _) = fixture.connected().await;
    let request = report();
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (recording_store, node, recording_report, start_barrier) = (
            recording_store.clone(),
            node.clone(),
            request.clone(),
            barrier.clone(),
        );
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            recording_store
                .report(&node, &recording_report)
                .await
                .unwrap()
        }));
    }
    let mut rows = Vec::new();
    for task in tasks {
        rows.push(task.await.unwrap());
    }
    assert!(rows.iter().all(|recording_record| {
        recording_record.id == rows[0].id && recording_record.revision == 1
    }));
    let id = rows[0].id;
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for index in 0..20 {
        let (recording_store, node, mut recording_report, start_barrier) = (
            recording_store.clone(),
            node.clone(),
            request.clone(),
            barrier.clone(),
        );
        recording_report.sequence = 2;
        recording_report.present = index % 2 == 0;
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            recording_store.report(&node, &recording_report).await
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
        .all(|recording_record| recording_record.revision == 2
            && recording_record.reported_present == successes[0].reported_present));
    let events: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.recording_events WHERE recording_id=$1")
            .bind(id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(events, 2);
    recording_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn reconnect_requires_current_node_and_explicit_new_generation_observation() {
    let fixture = Fixture::new().await;
    let recording_store = store().await;
    let (node, key) = fixture.connected().await;
    let mut request = report();
    request.sequence = 50;
    let first = recording_store.report(&node, &request).await.unwrap();
    fixture.nodes.close_connection(&node).await.unwrap();
    let offline = recording_store
        .list_managed(&fixture.admin, Some(node.id()), None, 1)
        .await
        .unwrap()
        .remove(0);
    assert_eq!(offline, first); // A historical presence observation, not a claim of current availability.
    assert!(recording_store.report(&node, &request).await.is_err());
    let current = fixture
        .nodes
        .open_connection(node.epoch(), &key, &token())
        .await
        .unwrap();
    fixture
        .nodes
        .report(&current, &node_report(1))
        .await
        .unwrap();
    request.sequence = 1;
    let reobserved = recording_store.report(&current, &request).await.unwrap();
    assert_eq!(reobserved.id, first.id);
    assert_eq!(reobserved.revision, 2);
    assert!(reobserved.node_generation > first.node_generation);
    assert!(recording_store.report(&node, &request).await.is_err());
    recording_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn optional_session_origin_is_exact_node_fk_and_cannot_authorize_cross_node_reporting() {
    let fixture = Fixture::new().await;
    let recording_store = store().await;
    let (node, app, _) = fixture.prepared(DeploymentTarget::Webview, 4).await;
    let (user, instance, command) = fixture.started(&node, app.id).await;
    let port = match command.action {
        NodeCommandAction::Start { port, .. } => port,
        _ => panic!("start required"),
    };
    fixture
        .instances
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
    let mut recording_report = report();
    recording_report.session_id = Some(session.id);
    let first = recording_store
        .report(&node, &recording_report)
        .await
        .unwrap();
    assert_eq!(first.session_id, Some(session.id));
    let (_, key) = fixture.node().await;
    let other = fixture
        .nodes
        .open_connection(node.epoch(), &key, &token())
        .await
        .unwrap();
    fixture.nodes.report(&other, &node_report(1)).await.unwrap();
    assert!(recording_store
        .report(&other, &recording_report)
        .await
        .is_err());
    recording_report.session_id = None;
    let own = recording_store
        .report(&other, &recording_report)
        .await
        .unwrap();
    assert_ne!(first.id, own.id);
    let mut missing = report();
    missing.present = false;
    assert!(recording_store.report(&node, &missing).await.is_err());
    missing.present = true;
    missing.session_id = Some(Uuid::new_v4());
    assert!(recording_store.report(&node, &missing).await.is_err());
    sessions.close().await;
    recording_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn failed_recording_audit_rolls_back_creation_observation_and_immutable_metadata() {
    let fixture = Fixture::new().await;
    let recording_store = store().await;
    let (node, _) = fixture.connected().await;
    let mut recording_report = report();
    permission(&fixture, false).await;
    let failed = recording_store.report(&node, &recording_report).await;
    permission(&fixture, true).await;
    assert!(failed.is_err());
    assert!(recording_store
        .list_managed(&fixture.admin, Some(node.id()), None, 100)
        .await
        .unwrap()
        .is_empty());
    let first = recording_store
        .report(&node, &recording_report)
        .await
        .unwrap();
    recording_report.sequence = 2;
    recording_report.present = false;
    permission(&fixture, false).await;
    let failed = recording_store.report(&node, &recording_report).await;
    permission(&fixture, true).await;
    assert!(failed.is_err());
    assert_eq!(
        first,
        recording_store
            .list_managed(&fixture.admin, Some(node.id()), None, 100)
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
    recording_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn owned_history_is_session_scoped_bounded_and_secret_free() {
    let fixture = Fixture::new().await;
    let recording_store = store().await;
    let (node, application, _) = fixture.prepared(DeploymentTarget::Webview, 4).await;
    let (user, instance, command) = fixture.started(&node, application.id).await;
    let port = match command.action {
        NodeCommandAction::Start { port, .. } => port,
        _ => panic!("start required"),
    };
    fixture
        .instances
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
                    application_id: application.id,
                    instance_id: instance.id,
                },
                access: SessionAccess::Controller,
            },
        )
        .await
        .unwrap();
    let mut ids = Vec::new();
    for _ in 0..3 {
        let mut recording_report = report();
        recording_report.session_id = Some(session.id);
        ids.push(
            recording_store
                .report(&node, &recording_report)
                .await
                .unwrap()
                .id,
        );
    }
    ids.sort();
    recording_store.report(&node, &report()).await.unwrap();
    let unrelated_user = fixture.session("user", ClientType::Android).await;
    assert!(recording_store
        .list_owned(&unrelated_user, ClientType::Android, None, 100)
        .await
        .unwrap()
        .is_empty());
    let mut after = None;
    let mut found = Vec::new();
    loop {
        let page = recording_store
            .list_owned(&user, ClientType::Android, after, 1)
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
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    assert_eq!(
        recording_store
            .list_managed(&viewer, Some(node.id()), None, 100)
            .await
            .unwrap()
            .len(),
        4
    );
    assert!(recording_store
        .list_managed(&viewer, None, None, 101)
        .await
        .is_err());
    assert!(recording_store
        .list_managed(&user, None, None, 1)
        .await
        .is_err());
    assert!(recording_store
        .list_owned(&user, ClientType::Android, None, 101)
        .await
        .is_err());
    sessions.close().await;
    recording_store.close().await;
    assert!(recording_store
        .list_managed(&viewer, None, None, 1)
        .await
        .is_err());
    let reopened = store().await;
    assert_eq!(
        reopened
            .list_managed(&viewer, Some(node.id()), None, 100)
            .await
            .unwrap()
            .len(),
        4
    );
    reopened.close().await;
    fixture.close().await;
}
