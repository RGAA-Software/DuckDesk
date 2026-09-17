#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, request, token, Fixture};
use px_console_store::{
    BeginFileTransfer, ClientType, CommandOutcome, CommandReceipt, DeploymentTarget,
    FileTransferStore, NodeCommandAction, NodeConnection, OpenResourceSession, ResourceCredential,
    ResourceSessionStore, SessionAccess, SessionTarget, TokenDigest, TransferDirection,
    TransferFailure, TransferOutcome, TransferProgress,
};
use std::{env, sync::Arc};
use tokio::sync::Barrier;
use uuid::Uuid;

#[tokio::test]
async fn expired_or_closing_frontend_rejects_new_progress_and_success_but_keeps_failure_bookkeeping(
) {
    let (f, t, s, node, _, session) = setup().await;
    let expired = t.begin(&node, &begin(session)).await.unwrap();
    let closing = t.begin(&node, &begin(session)).await.unwrap();
    let committed = t.begin(&node, &begin(session)).await.unwrap();
    let complete = TransferProgress {
        sequence: 1,
        transferred_bytes: 1024,
        outcome: TransferOutcome::Completed {
            received_sha256: [71; 32],
        },
    };
    let previous = t.report(&node, committed.id, &complete).await.unwrap();
    sqlx::query("UPDATE pixels.resource_sessions SET descriptor_expires_at=clock_timestamp()-interval '1 second' WHERE id=$1")
  .bind(session).execute(&f.owner).await.unwrap();
    assert!(t.report(&node, expired.id, &progress(1, 1)).await.is_err());
    assert!(t.report(&node, expired.id, &complete).await.is_err());
    assert_eq!(revision(&f, expired.id).await, 1);
    // Historical exact completion retry does not extend any transport permission.
    assert_eq!(
        t.report(&node, committed.id, &complete).await.unwrap(),
        previous
    );
    let failure = TransferProgress {
        sequence: 1,
        transferred_bytes: 0,
        outcome: TransferOutcome::Failed {
            reason: TransferFailure::TransportLost,
        },
    };
    assert_eq!(
        t.report(&node, expired.id, &failure).await.unwrap().state,
        "failed"
    );
    sqlx::query("UPDATE pixels.resource_sessions SET descriptor_expires_at=clock_timestamp()+interval '30 seconds' WHERE id=$1")
  .bind(session).execute(&f.owner).await.unwrap();
    s.begin_retirement(&node, session).await.unwrap();
    assert!(t.report(&node, closing.id, &progress(1, 1)).await.is_err());
    assert!(t.report(&node, closing.id, &complete).await.is_err());
    let cancelled = TransferProgress {
        sequence: 1,
        transferred_bytes: 0,
        outcome: TransferOutcome::Cancelled,
    };
    assert_eq!(
        t.report(&node, closing.id, &cancelled).await.unwrap().state,
        "cancelled"
    );
    t.close().await;
    s.close().await;
    f.close().await;
}

#[tokio::test]
async fn frontend_close_records_missing_outcomes_as_unknown_and_rolls_back_on_audit_failure() {
    let (f, t, s, node, _, session) = setup().await;
    let done = t.begin(&node, &begin(session)).await.unwrap();
    let pending = t.begin(&node, &begin(session)).await.unwrap();
    let completed = TransferProgress {
        sequence: 1,
        transferred_bytes: 1024,
        outcome: TransferOutcome::Completed {
            received_sha256: [71; 32],
        },
    };
    t.report(&node, done.id, &completed).await.unwrap();
    let fence = s.begin_retirement(&node, session).await.unwrap();
    permission(&f, false).await;
    let failed = s
        .finish_retirement(&node, session, fence.challenge_id)
        .await;
    permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(revision(&f, pending.id).await, 1);
    let state: String =
        sqlx::query_scalar("SELECT state FROM pixels.resource_sessions WHERE id=$1")
            .bind(session)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(state, "closing");
    s.finish_retirement(&node, session, fence.challenge_id)
        .await
        .unwrap();
    s.finish_retirement(&node, session, fence.challenge_id)
        .await
        .unwrap();
    let states: Vec<(Uuid, String, Option<String>)> =
        sqlx::query_as("SELECT id,state,reason FROM pixels.file_transfers WHERE id=ANY($1)")
            .bind(vec![done.id, pending.id])
            .fetch_all(&f.owner)
            .await
            .unwrap();
    assert!(states
        .iter()
        .any(|r| r.0 == done.id && r.1 == "completed" && r.2.is_none()));
    assert!(states.iter().any(|r| r.0 == pending.id
        && r.1 == "unknown"
        && r.2.as_deref() == Some("frontend_closed")));
    assert_eq!(revision(&f, pending.id).await, 2);
    assert!(t.report(&node, pending.id, &completed).await.is_err());
    assert!(t.begin(&node, &begin(session)).await.is_err());
    t.close().await;
    s.close().await;
    f.close().await;
}
async fn store() -> FileTransferStore {
    FileTransferStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap()
}
async fn setup() -> (
    Fixture,
    FileTransferStore,
    ResourceSessionStore,
    NodeConnection,
    TokenDigest,
    Uuid,
) {
    let f = Fixture::new().await;
    let transfers = store().await;
    let sessions = ResourceSessionStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap();
    let (node, app, _) = f.prepared(DeploymentTarget::Webview, 4).await;
    let user = f.session("user", ClientType::Android).await;
    let i = f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            node.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let cmd = f.instances.next_command(&node).await.unwrap().unwrap();
    let port = match cmd.action {
        NodeCommandAction::Start { port, .. } => port,
        _ => panic!("start required"),
    };
    f.instances
        .acknowledge_command(
            &node,
            &CommandReceipt {
                command_id: cmd.id,
                lease_id: cmd.lease_id,
                instance_id: i.id,
                launch_id: cmd.launch_id,
                instance_revision: cmd.instance_revision,
                outcome: CommandOutcome::Running { port },
            },
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
                    instance_id: i.id,
                },
                access: SessionAccess::Controller,
            },
        )
        .await
        .unwrap();
    let ticket = token();
    let d = sessions
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    sessions
        .admit_frontend(&node, session.id, d.session.revision, &ticket)
        .await
        .unwrap();
    (f, transfers, sessions, node, user, session.id)
}
fn begin(session: Uuid) -> BeginFileTransfer {
    BeginFileTransfer {
        request_id: Uuid::new_v4(),
        session_id: session,
        direction: TransferDirection::ToNode,
        file_name: "合成文件 sample.bin".into(),
        total_bytes: 1024,
        expected_sha256: [71; 32],
    }
}
fn progress(sequence: u64, bytes: u64) -> TransferProgress {
    TransferProgress {
        sequence,
        transferred_bytes: bytes,
        outcome: TransferOutcome::Progress,
    }
}
async fn permission(f: &Fixture, allow: bool) {
    sqlx::query(if allow {
        "GRANT INSERT ON pixels.file_transfer_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.file_transfer_events FROM pixels_console_runtime"
    })
    .execute(&f.owner)
    .await
    .unwrap();
}
async fn revision(f: &Fixture, id: Uuid) -> i64 {
    sqlx::query_scalar("SELECT revision FROM pixels.file_transfers WHERE id=$1")
        .bind(id)
        .fetch_one(&f.owner)
        .await
        .unwrap()
}
#[tokio::test]
async fn transfer_creation_and_reports_are_idempotent_monotonic_and_hash_verified() {
    let (f, t, s, node, _, session) = setup().await;
    let request = begin(session);
    let record = t.begin(&node, &request).await.unwrap();
    assert_eq!(record, t.begin(&node, &request).await.unwrap());
    let mut altered = request.clone();
    altered.total_bytes += 1;
    assert!(t.begin(&node, &altered).await.is_err());
    let first = t.report(&node, record.id, &progress(1, 100)).await.unwrap();
    assert_eq!(
        first,
        t.report(&node, record.id, &progress(1, 100)).await.unwrap()
    );
    assert!(t.report(&node, record.id, &progress(1, 101)).await.is_err());
    assert!(t.report(&node, record.id, &progress(2, 99)).await.is_err());
    assert!(t
        .report(&node, record.id, &progress(2, 1025))
        .await
        .is_err());
    let mismatch = TransferProgress {
        sequence: 2,
        transferred_bytes: 1024,
        outcome: TransferOutcome::Completed {
            received_sha256: [72; 32],
        },
    };
    assert!(t.report(&node, record.id, &mismatch).await.is_err());
    let completed = TransferProgress {
        sequence: 2,
        transferred_bytes: 1024,
        outcome: TransferOutcome::Completed {
            received_sha256: [71; 32],
        },
    };
    let final_record = t.report(&node, record.id, &completed).await.unwrap();
    assert_eq!(final_record.state, "completed");
    assert!(final_record.ended_at.is_some());
    assert_eq!(
        final_record,
        t.report(&node, record.id, &completed).await.unwrap()
    );
    assert!(t
        .report(&node, record.id, &progress(3, 1024))
        .await
        .is_err());
    t.close().await;
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn twenty_competing_creates_and_same_sequence_reports_have_one_committed_result() {
    let (f, t, s, node, _, session) = setup().await;
    let request = begin(session);
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (t, node, r, b) = (t.clone(), node.clone(), request.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            t.begin(&node, &r).await.unwrap()
        }));
    }
    let mut records = Vec::new();
    for task in tasks {
        records.push(task.await.unwrap());
    }
    assert!(records.iter().all(|r| r.id == records[0].id));
    let id = records[0].id;
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for bytes in 0..20 {
        let (t, node, b) = (t.clone(), node.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            b.wait().await;
            t.report(&node, id, &progress(1, bytes)).await
        }));
    }
    let mut wins = 0;
    for task in tasks {
        if task.await.unwrap().is_ok() {
            wins += 1;
        }
    }
    assert_eq!(wins, 1);
    assert_eq!(revision(&f, id).await, 2);
    let events: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.file_transfer_events WHERE transfer_id=$1")
            .bind(id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(events, 2);
    t.close().await;
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn wrong_node_observer_and_expired_admission_cannot_create_transfer() {
    let (f, t, s, node, _, session) = setup().await;
    let (_, key) = f.node().await;
    let other = f
        .nodes
        .open_connection(node.epoch(), &key, &token())
        .await
        .unwrap();
    f.nodes
        .report(&other, &fixture::node_report(1))
        .await
        .unwrap();
    assert!(t.begin(&other, &begin(session)).await.is_err());
    let record = t.begin(&node, &begin(session)).await.unwrap();
    assert!(t.report(&other, record.id, &progress(1, 1)).await.is_err());
    sqlx::query("UPDATE pixels.resource_sessions SET access_role='observer' WHERE id=$1")
        .bind(session)
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(t.begin(&node, &begin(session)).await.is_err());
    sqlx::query("UPDATE pixels.resource_sessions SET access_role='controller',descriptor_expires_at=clock_timestamp()-interval '1 second' WHERE id=$1")
        .bind(session).execute(&f.owner).await.unwrap();
    assert!(t.begin(&node, &begin(session)).await.is_err());
    assert!(t.begin(&node, &begin(Uuid::new_v4())).await.is_err());
    let mut invalid = begin(session);
    invalid.total_bytes = u64::MAX;
    assert!(t.begin(&node, &invalid).await.is_err());
    t.close().await;
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn revoked_access_can_record_final_failure_but_restart_marks_unfinished_unknown() {
    let (f, t, s, node, user, session) = setup().await;
    let record = t.begin(&node, &begin(session)).await.unwrap();
    let unfinished = t.begin(&node, &begin(session)).await.unwrap();
    let login = f
        .identity
        .authenticate(&user, ClientType::Android)
        .await
        .unwrap();
    f.identity
        .revoke_session(login.user_id, login.session_id)
        .await
        .unwrap();
    assert!(t.begin(&node, &begin(session)).await.is_err());
    assert!(t.report(&node, record.id, &progress(1, 20)).await.is_err());
    assert!(t
        .report(
            &node,
            record.id,
            &TransferProgress {
                sequence: 1,
                transferred_bytes: 1024,
                outcome: TransferOutcome::Completed {
                    received_sha256: [71; 32]
                }
            }
        )
        .await
        .is_err());
    assert_eq!(revision(&f, record.id).await, 1);
    let failure = TransferProgress {
        sequence: 1,
        transferred_bytes: 0,
        outcome: TransferOutcome::Failed {
            reason: TransferFailure::PolicyRevoked,
        },
    };
    assert_eq!(
        t.report(&node, record.id, &failure).await.unwrap().state,
        "failed"
    );
    f.nodes.begin_runtime().await.unwrap();
    let states: Vec<(Uuid, String, Option<chrono::DateTime<chrono::Utc>>)> =
        sqlx::query_as("SELECT id,state,ended_at FROM pixels.file_transfers WHERE id=ANY($1)")
            .bind(vec![record.id, unfinished.id])
            .fetch_all(&f.owner)
            .await
            .unwrap();
    assert!(states
        .iter()
        .any(|r| r.0 == record.id && r.1 == "failed" && r.2.is_some()));
    assert!(states
        .iter()
        .any(|r| r.0 == unfinished.id && r.1 == "unknown" && r.2.is_none()));
    assert!(t
        .report(&node, unfinished.id, &progress(1, 20))
        .await
        .is_err());
    t.close().await;
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn event_failure_rolls_back_begin_progress_and_node_invalidation() {
    let (f, t, s, node, _, session) = setup().await;
    let request = begin(session);
    permission(&f, false).await;
    let failed = t.begin(&node, &request).await;
    permission(&f, true).await;
    assert!(failed.is_err());
    let record = t.begin(&node, &request).await.unwrap();
    permission(&f, false).await;
    let failed = t.report(&node, record.id, &progress(1, 1)).await;
    permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(revision(&f, record.id).await, 1);
    permission(&f, false).await;
    let failed = f.nodes.close_connection(&node).await;
    permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(revision(&f, record.id).await, 1);
    assert!(s.list_node(&node).await.is_ok());
    assert!(t.report(&node, record.id, &progress(1, 1)).await.is_ok());
    t.close().await;
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn pagination_identity_privileges_and_reconnect_preserve_history_without_secrets() {
    let (f, t, s, node, user, session) = setup().await;
    let mut ids = Vec::new();
    for _ in 0..3 {
        ids.push(t.begin(&node, &begin(session)).await.unwrap().id);
    }
    ids.sort();
    let stranger = f.session("user", ClientType::Android).await;
    assert!(t
        .list_owned(
            ResourceCredential::User(&stranger),
            ClientType::Android,
            None,
            100
        )
        .await
        .unwrap()
        .is_empty());
    let mut found = Vec::new();
    let mut after = None;
    loop {
        let page = t
            .list_owned(
                ResourceCredential::User(&user),
                ClientType::Android,
                after,
                1,
            )
            .await
            .unwrap();
        if page.is_empty() {
            break;
        }
        let record = &page[0];
        found.push(record.id);
        after = Some(record.id);
        let json = serde_json::to_string(record).unwrap();
        for secret in [
            "request_hash",
            "expected_sha256",
            "report_hash",
            "password",
            "token",
        ] {
            assert!(!json.contains(secret));
        }
    }
    assert_eq!(found, ids);
    let viewer = f.session("viewer", ClientType::AdminWeb).await;
    assert_eq!(
        t.list_managed(&viewer, None, Some(node.id()), 100)
            .await
            .unwrap()
            .len(),
        3
    );
    assert!(t.list_managed(&user, None, None, 1).await.is_err());
    assert!(t.list_managed(&viewer, None, None, 101).await.is_err());
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(sqlx::query("DELETE FROM pixels.file_transfers WHERE id=$1")
        .bind(ids[0])
        .execute(&runtime)
        .await
        .is_err());
    assert!(
        sqlx::query("UPDATE pixels.file_transfers SET total_bytes=100 WHERE id=$1")
            .bind(ids[0])
            .execute(&runtime)
            .await
            .is_err()
    );
    runtime.close().await;
    t.close().await;
    assert!(t.list_managed(&viewer, None, None, 1).await.is_err());
    let restarted = store().await;
    assert_eq!(
        restarted
            .list_owned(
                ResourceCredential::User(&user),
                ClientType::Android,
                None,
                100
            )
            .await
            .unwrap()
            .len(),
        3
    );
    restarted.close().await;
    s.close().await;
    f.close().await;
}
