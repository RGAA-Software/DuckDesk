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
    let (fixture, transfer_store, session_store, node, _, session) = setup().await;
    let expired = transfer_store.begin(&node, &begin(session)).await.unwrap();
    let closing = transfer_store.begin(&node, &begin(session)).await.unwrap();
    let committed = transfer_store.begin(&node, &begin(session)).await.unwrap();
    let complete = TransferProgress {
        sequence: 1,
        transferred_bytes: 1024,
        outcome: TransferOutcome::Completed {
            received_sha256: [71; 32],
        },
    };
    let previous = transfer_store
        .report(&node, committed.id, &complete)
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.resource_sessions SET descriptor_expires_at=clock_timestamp()-interval '1 second' WHERE id=$1")
  .bind(session).execute(&fixture.owner).await.unwrap();
    assert!(transfer_store
        .report(&node, expired.id, &progress(1, 1))
        .await
        .is_err());
    assert!(transfer_store
        .report(&node, expired.id, &complete)
        .await
        .is_err());
    assert_eq!(revision(&fixture, expired.id).await, 1);
    // Historical exact completion retry does not extend any transport permission.
    assert_eq!(
        transfer_store
            .report(&node, committed.id, &complete)
            .await
            .unwrap(),
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
        transfer_store
            .report(&node, expired.id, &failure)
            .await
            .unwrap()
            .state,
        "failed"
    );
    sqlx::query("UPDATE pixels.resource_sessions SET descriptor_expires_at=clock_timestamp()+interval '30 seconds' WHERE id=$1")
  .bind(session).execute(&fixture.owner).await.unwrap();
    session_store
        .begin_retirement(&node, session)
        .await
        .unwrap();
    assert!(transfer_store
        .report(&node, closing.id, &progress(1, 1))
        .await
        .is_err());
    assert!(transfer_store
        .report(&node, closing.id, &complete)
        .await
        .is_err());
    let cancelled = TransferProgress {
        sequence: 1,
        transferred_bytes: 0,
        outcome: TransferOutcome::Cancelled,
    };
    assert_eq!(
        transfer_store
            .report(&node, closing.id, &cancelled)
            .await
            .unwrap()
            .state,
        "cancelled"
    );
    transfer_store.close().await;
    session_store.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn frontend_close_records_missing_outcomes_as_unknown_and_rolls_back_on_audit_failure() {
    let (fixture, transfer_store, session_store, node, _, session) = setup().await;
    let done = transfer_store.begin(&node, &begin(session)).await.unwrap();
    let pending = transfer_store.begin(&node, &begin(session)).await.unwrap();
    let completed = TransferProgress {
        sequence: 1,
        transferred_bytes: 1024,
        outcome: TransferOutcome::Completed {
            received_sha256: [71; 32],
        },
    };
    transfer_store
        .report(&node, done.id, &completed)
        .await
        .unwrap();
    let fence = session_store
        .begin_retirement(&node, session)
        .await
        .unwrap();
    permission(&fixture, false).await;
    let failed = session_store
        .finish_retirement(&node, session, fence.challenge_id)
        .await;
    permission(&fixture, true).await;
    assert!(failed.is_err());
    assert_eq!(revision(&fixture, pending.id).await, 1);
    let state: String =
        sqlx::query_scalar("SELECT state FROM pixels.resource_sessions WHERE id=$1")
            .bind(session)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(state, "closing");
    session_store
        .finish_retirement(&node, session, fence.challenge_id)
        .await
        .unwrap();
    session_store
        .finish_retirement(&node, session, fence.challenge_id)
        .await
        .unwrap();
    let states: Vec<(Uuid, String, Option<String>)> =
        sqlx::query_as("SELECT id,state,reason FROM pixels.file_transfers WHERE id=ANY($1)")
            .bind(vec![done.id, pending.id])
            .fetch_all(&fixture.owner)
            .await
            .unwrap();
    assert!(states.iter().any(|state_record| {
        state_record.0 == done.id && state_record.1 == "completed" && state_record.2.is_none()
    }));
    assert!(states.iter().any(|state_record| {
        state_record.0 == pending.id
            && state_record.1 == "unknown"
            && state_record.2.as_deref() == Some("frontend_closed")
    }));
    assert_eq!(revision(&fixture, pending.id).await, 2);
    assert!(transfer_store
        .report(&node, pending.id, &completed)
        .await
        .is_err());
    assert!(transfer_store.begin(&node, &begin(session)).await.is_err());
    transfer_store.close().await;
    session_store.close().await;
    fixture.close().await;
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
    let fixture = Fixture::new().await;
    let transfers = store().await;
    let sessions = ResourceSessionStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap();
    let (node, app, _) = fixture.prepared(DeploymentTarget::Webview, 4).await;
    let user = fixture.session("user", ClientType::Android).await;
    let running_instance = fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            node.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let cmd = fixture
        .instances
        .next_command(&node)
        .await
        .unwrap()
        .unwrap();
    let port = match cmd.action {
        NodeCommandAction::Start { port, .. } => port,
        _ => panic!("start required"),
    };
    fixture
        .instances
        .acknowledge_command(
            &node,
            &CommandReceipt {
                command_id: cmd.id,
                lease_id: cmd.lease_id,
                instance_id: running_instance.id,
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
                    instance_id: running_instance.id,
                },
                access: SessionAccess::Controller,
            },
        )
        .await
        .unwrap();
    let ticket = token();
    let session_descriptor = sessions
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
        .admit_frontend(
            &node,
            session.id,
            session_descriptor.session.revision,
            &ticket,
        )
        .await
        .unwrap();
    (fixture, transfers, sessions, node, user, session.id)
}
fn begin(session: Uuid) -> BeginFileTransfer {
    BeginFileTransfer {
        request_id: Uuid::new_v4(),
        session_id: session,
        direction: TransferDirection::ToNode,
        file_name: "合成文件 sample.bin".into(),
        total_bytes: 1024,
        expected_sha256: Some([71; 32]),
    }
}
fn progress(sequence: u64, bytes: u64) -> TransferProgress {
    TransferProgress {
        sequence,
        transferred_bytes: bytes,
        outcome: TransferOutcome::Progress,
    }
}
async fn permission(fixture: &Fixture, allow: bool) {
    sqlx::query(if allow {
        "GRANT INSERT ON pixels.file_transfer_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.file_transfer_events FROM pixels_console_runtime"
    })
    .execute(&fixture.owner)
    .await
    .unwrap();
}
async fn revision(fixture: &Fixture, id: Uuid) -> i64 {
    sqlx::query_scalar("SELECT revision FROM pixels.file_transfers WHERE id=$1")
        .bind(id)
        .fetch_one(&fixture.owner)
        .await
        .unwrap()
}
#[tokio::test]
async fn transfer_creation_and_reports_are_idempotent_monotonic_and_hash_verified() {
    let (fixture, transfer_store, session_store, node, _, session) = setup().await;
    let request = begin(session);
    let record = transfer_store.begin(&node, &request).await.unwrap();
    assert_eq!(record, transfer_store.begin(&node, &request).await.unwrap());
    let mut altered = request.clone();
    altered.total_bytes += 1;
    assert!(transfer_store.begin(&node, &altered).await.is_err());
    let first = transfer_store
        .report(&node, record.id, &progress(1, 100))
        .await
        .unwrap();
    assert_eq!(
        first,
        transfer_store
            .report(&node, record.id, &progress(1, 100))
            .await
            .unwrap()
    );
    assert!(transfer_store
        .report(&node, record.id, &progress(1, 101))
        .await
        .is_err());
    assert!(transfer_store
        .report(&node, record.id, &progress(2, 99))
        .await
        .is_err());
    assert!(transfer_store
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
    assert!(transfer_store
        .report(&node, record.id, &mismatch)
        .await
        .is_err());
    let completed = TransferProgress {
        sequence: 2,
        transferred_bytes: 1024,
        outcome: TransferOutcome::Completed {
            received_sha256: [71; 32],
        },
    };
    let final_record = transfer_store
        .report(&node, record.id, &completed)
        .await
        .unwrap();
    assert_eq!(final_record.state, "completed");
    assert!(final_record.ended_at.is_some());
    assert_eq!(
        final_record,
        transfer_store
            .report(&node, record.id, &completed)
            .await
            .unwrap()
    );
    assert!(transfer_store
        .report(&node, record.id, &progress(3, 1024))
        .await
        .is_err());
    let mut observed_hash_request = begin(session);
    observed_hash_request.expected_sha256 = None;
    let observed_hash_record = transfer_store
        .begin(&node, &observed_hash_request)
        .await
        .unwrap();
    let observed_hash = [73; 32];
    let observed_hash_completed = TransferProgress {
        sequence: 1,
        transferred_bytes: 1024,
        outcome: TransferOutcome::Completed {
            received_sha256: observed_hash,
        },
    };
    assert_eq!(
        transfer_store
            .report(&node, observed_hash_record.id, &observed_hash_completed)
            .await
            .unwrap()
            .state,
        "completed"
    );
    let (stored_expected, stored_received): (Vec<u8>, Vec<u8>) = sqlx::query_as(
        "SELECT expected_sha256,received_sha256 FROM pixels.file_transfers WHERE id=$1",
    )
    .bind(observed_hash_record.id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(stored_expected, observed_hash);
    assert_eq!(stored_received, observed_hash);
    transfer_store.close().await;
    session_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn twenty_competing_creates_and_same_sequence_reports_have_one_committed_result() {
    let (fixture, transfer_store, session_store, node, _, session) = setup().await;
    let request = begin(session);
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (transfer_store, node, begin_request, start_barrier) = (
            transfer_store.clone(),
            node.clone(),
            request.clone(),
            barrier.clone(),
        );
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            transfer_store.begin(&node, &begin_request).await.unwrap()
        }));
    }
    let mut records = Vec::new();
    for task in tasks {
        records.push(task.await.unwrap());
    }
    assert!(records
        .iter()
        .all(|transfer_record| transfer_record.id == records[0].id));
    let id = records[0].id;
    let barrier = Arc::new(Barrier::new(20));
    let mut tasks = Vec::new();
    for bytes in 0..20 {
        let (transfer_store, node, start_barrier) =
            (transfer_store.clone(), node.clone(), barrier.clone());
        tasks.push(tokio::spawn(async move {
            start_barrier.wait().await;
            transfer_store.report(&node, id, &progress(1, bytes)).await
        }));
    }
    let mut wins = 0;
    for task in tasks {
        if task.await.unwrap().is_ok() {
            wins += 1;
        }
    }
    assert_eq!(wins, 1);
    assert_eq!(revision(&fixture, id).await, 2);
    let events: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.file_transfer_events WHERE transfer_id=$1")
            .bind(id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(events, 2);
    transfer_store.close().await;
    session_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn wrong_node_observer_and_expired_admission_cannot_create_transfer() {
    let (fixture, transfer_store, session_store, node, _, session) = setup().await;
    let (_, key) = fixture.node().await;
    let other = fixture
        .nodes
        .open_connection(node.epoch(), &key, &token())
        .await
        .unwrap();
    fixture
        .nodes
        .report(&other, &fixture::node_report(1))
        .await
        .unwrap();
    assert!(transfer_store.begin(&other, &begin(session)).await.is_err());
    let record = transfer_store.begin(&node, &begin(session)).await.unwrap();
    assert!(transfer_store
        .report(&other, record.id, &progress(1, 1))
        .await
        .is_err());
    sqlx::query("UPDATE pixels.resource_sessions SET access_role='observer' WHERE id=$1")
        .bind(session)
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(transfer_store.begin(&node, &begin(session)).await.is_err());
    sqlx::query("UPDATE pixels.resource_sessions SET access_role='controller',descriptor_expires_at=clock_timestamp()-interval '1 second' WHERE id=$1")
        .bind(session).execute(&fixture.owner).await.unwrap();
    assert!(transfer_store.begin(&node, &begin(session)).await.is_err());
    assert!(transfer_store
        .begin(&node, &begin(Uuid::new_v4()))
        .await
        .is_err());
    let mut invalid = begin(session);
    invalid.total_bytes = u64::MAX;
    assert!(transfer_store.begin(&node, &invalid).await.is_err());
    transfer_store.close().await;
    session_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn revoked_access_can_record_final_failure_but_restart_marks_unfinished_unknown() {
    let (fixture, transfer_store, session_store, node, user, session) = setup().await;
    let record = transfer_store.begin(&node, &begin(session)).await.unwrap();
    let unfinished = transfer_store.begin(&node, &begin(session)).await.unwrap();
    let login = fixture
        .identity
        .authenticate(&user, ClientType::Android)
        .await
        .unwrap();
    fixture
        .identity
        .revoke_session(login.user_id, login.session_id)
        .await
        .unwrap();
    assert!(transfer_store.begin(&node, &begin(session)).await.is_err());
    assert!(transfer_store
        .report(&node, record.id, &progress(1, 20))
        .await
        .is_err());
    assert!(transfer_store
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
    assert_eq!(revision(&fixture, record.id).await, 1);
    let failure = TransferProgress {
        sequence: 1,
        transferred_bytes: 0,
        outcome: TransferOutcome::Failed {
            reason: TransferFailure::PolicyRevoked,
        },
    };
    assert_eq!(
        transfer_store
            .report(&node, record.id, &failure)
            .await
            .unwrap()
            .state,
        "failed"
    );
    fixture.nodes.begin_runtime().await.unwrap();
    let states: Vec<(Uuid, String, Option<chrono::DateTime<chrono::Utc>>)> =
        sqlx::query_as("SELECT id,state,ended_at FROM pixels.file_transfers WHERE id=ANY($1)")
            .bind(vec![record.id, unfinished.id])
            .fetch_all(&fixture.owner)
            .await
            .unwrap();
    assert!(states.iter().any(|state_record| {
        state_record.0 == record.id && state_record.1 == "failed" && state_record.2.is_some()
    }));
    assert!(states.iter().any(|state_record| {
        state_record.0 == unfinished.id && state_record.1 == "unknown" && state_record.2.is_none()
    }));
    assert!(transfer_store
        .report(&node, unfinished.id, &progress(1, 20))
        .await
        .is_err());
    transfer_store.close().await;
    session_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn event_failure_rolls_back_begin_progress_and_node_invalidation() {
    let (fixture, transfer_store, session_store, node, _, session) = setup().await;
    let request = begin(session);
    permission(&fixture, false).await;
    let failed = transfer_store.begin(&node, &request).await;
    permission(&fixture, true).await;
    assert!(failed.is_err());
    let record = transfer_store.begin(&node, &request).await.unwrap();
    permission(&fixture, false).await;
    let failed = transfer_store
        .report(&node, record.id, &progress(1, 1))
        .await;
    permission(&fixture, true).await;
    assert!(failed.is_err());
    assert_eq!(revision(&fixture, record.id).await, 1);
    permission(&fixture, false).await;
    let failed = fixture.nodes.close_connection(&node).await;
    permission(&fixture, true).await;
    assert!(failed.is_err());
    assert_eq!(revision(&fixture, record.id).await, 1);
    assert!(session_store.list_node(&node).await.is_ok());
    assert!(transfer_store
        .report(&node, record.id, &progress(1, 1))
        .await
        .is_ok());
    transfer_store.close().await;
    session_store.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn pagination_identity_privileges_and_reconnect_preserve_history_without_secrets() {
    let (fixture, transfer_store, session_store, node, user, session) = setup().await;
    let mut ids = Vec::new();
    for _ in 0..3 {
        ids.push(
            transfer_store
                .begin(&node, &begin(session))
                .await
                .unwrap()
                .id,
        );
    }
    ids.sort();
    let stranger = fixture.session("user", ClientType::Android).await;
    assert!(transfer_store
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
        let page = transfer_store
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
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    assert_eq!(
        transfer_store
            .list_managed(&viewer, None, Some(node.id()), 100)
            .await
            .unwrap()
            .len(),
        3
    );
    assert!(transfer_store
        .list_managed(&user, None, None, 1)
        .await
        .is_err());
    assert!(transfer_store
        .list_managed(&viewer, None, None, 101)
        .await
        .is_err());
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
    transfer_store.close().await;
    assert!(transfer_store
        .list_managed(&viewer, None, None, 1)
        .await
        .is_err());
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
    session_store.close().await;
    fixture.close().await;
}
