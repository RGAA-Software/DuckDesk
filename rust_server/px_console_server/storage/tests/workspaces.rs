#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, request, token, Fixture};
use px_console_store::{
    ClientType, CommandOutcome, CommandReceipt, DeploymentTarget, NodeCommand, ResourceCredential,
    StoreError, WorkspaceCommandLease, WorkspaceKey, WorkspaceStore, WorkspaceVault,
};
use std::{env, sync::Arc};
use uuid::Uuid;
use zeroize::Zeroizing;

fn material(id: Uuid, byte: u8) -> WorkspaceKey {
    WorkspaceKey {
        id,
        bytes: Zeroizing::new([byte; 32]),
    }
}
fn vault(id: Uuid, byte: u8) -> Arc<WorkspaceVault> {
    Arc::new(WorkspaceVault::new(id, vec![material(id, byte)]).unwrap())
}
async fn store(vault: Arc<WorkspaceVault>) -> WorkspaceStore {
    WorkspaceStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
        vault,
    )
    .await
    .unwrap()
}
fn lease(command: &NodeCommand) -> WorkspaceCommandLease {
    WorkspaceCommandLease {
        command_id: command.id,
        lease_id: command.lease_id,
    }
}
fn receipt(command: &NodeCommand) -> CommandReceipt {
    CommandReceipt {
        command_id: command.id,
        lease_id: command.lease_id,
        instance_id: command.instance_id,
        launch_id: command.launch_id,
        instance_revision: command.instance_revision,
        outcome: CommandOutcome::Absent,
    }
}
async fn count(fixture: &Fixture, node: Uuid) -> i64 {
    sqlx::query_scalar("SELECT count(*) FROM pixels.rdp_workspaces WHERE node_id=$1")
        .bind(node)
        .fetch_one(&fixture.owner)
        .await
        .unwrap()
}
async fn revision(fixture: &Fixture, id: Uuid) -> i64 {
    sqlx::query_scalar("SELECT revision FROM pixels.rdp_workspaces WHERE id=$1")
        .bind(id)
        .fetch_one(&fixture.owner)
        .await
        .unwrap()
}
#[tokio::test]
async fn persistent_workspace_reuses_identity_after_runtime_stop_and_another_visitor() {
    let fixture = Fixture::new().await;
    let secret = vault(Uuid::new_v4(), 61);
    let db = store(secret.clone()).await;
    let (node, app, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let (_, instance, start) = fixture.started(&node, app.id).await;
    let first = db
        .credentials_for_start(&node, lease(&start))
        .await
        .unwrap();
    assert_eq!(first.account_name.len(), 20);
    let sid = "S-1-5-21-11-22-33-1001";
    for _ in 0..2 {
        db.confirm_account(&node, lease(&start), first.workspace_id, sid)
            .await
            .unwrap();
    }
    assert_eq!(revision(&fixture, first.workspace_id).await, 2);
    assert!(db
        .confirm_account(
            &node,
            lease(&start),
            first.workspace_id,
            "S-1-5-21-11-22-33-1002"
        )
        .await
        .is_err());
    db.close().await;
    let db = store(secret).await;
    let restarted = db
        .credentials_for_start(&node, lease(&start))
        .await
        .unwrap();
    assert_eq!(restarted.workspace_id, first.workspace_id);
    assert_eq!(*restarted.password, *first.password);
    fixture
        .instances
        .stop_managed(
            &fixture.admin,
            node.epoch(),
            instance.id,
            start.instance_revision,
        )
        .await
        .unwrap();
    let stop = fixture
        .instances
        .next_command(&node)
        .await
        .unwrap()
        .unwrap();
    fixture
        .instances
        .acknowledge_command(&node, &receipt(&stop))
        .await
        .unwrap();
    let (guest, _) = fixture.guest().await;
    fixture
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            node.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let next = fixture
        .instances
        .next_command(&node)
        .await
        .unwrap()
        .unwrap();
    let other = db.credentials_for_start(&node, lease(&next)).await.unwrap();
    assert_eq!(other.workspace_id, first.workspace_id);
    assert_eq!(other.account_name, first.account_name);
    assert_eq!(other.windows_sid.as_deref(), Some(sid));
    assert_eq!(*other.password, *first.password);
    assert_eq!(count(&fixture, node.id()).await, 1);
    let tables:i64=sqlx::query_scalar("SELECT count(*) FROM information_schema.columns WHERE table_schema='pixels' AND table_name IN ('rdp_workspaces','workspace_secrets') AND column_name LIKE '%password%'")
        .fetch_one(&fixture.owner).await.unwrap();
    assert_eq!(tables, 0);
    db.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn twenty_credential_requests_have_one_identity_and_cas_rotation_has_one_winner() {
    let fixture = Fixture::new().await;
    let old = Uuid::new_v4();
    let new = Uuid::new_v4();
    let db = store(vault(old, 61)).await;
    let (node, app, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let (_, _, start) = fixture.started(&node, app.id).await;
    let barrier = Arc::new(tokio::sync::Barrier::new(20));
    let mut tasks = tokio::task::JoinSet::new();
    for _ in 0..20 {
        let db = db.clone();
        let node = node.clone();
        let lease = lease(&start);
        let barrier = barrier.clone();
        tasks.spawn(async move {
            barrier.wait().await;
            db.credentials_for_start(&node, lease).await
        });
    }
    let first = tasks.join_next().await.unwrap().unwrap().unwrap();
    while let Some(result) = tasks.join_next().await {
        let credential = result.unwrap().unwrap();
        assert_eq!(credential.workspace_id, first.workspace_id);
        assert_eq!(*credential.password, *first.password);
    }
    assert_eq!(count(&fixture, node.id()).await, 1);
    let rotate = store(Arc::new(
        WorkspaceVault::new(new, vec![material(old, 61), material(new, 62)]).unwrap(),
    ))
    .await;
    let (left, right) = tokio::join!(
        rotate.rewrap_managed(&fixture.admin, first.workspace_id, 1),
        rotate.rewrap_managed(&fixture.admin, first.workspace_id, 1)
    );
    assert_eq!(usize::from(left.is_ok()) + usize::from(right.is_ok()), 1);
    assert_eq!(revision(&fixture, first.workspace_id).await, 2);
    let audits: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.workspace_audit WHERE workspace_id=$1")
            .bind(first.workspace_id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(audits, 2);
    let rotated = rotate
        .credentials_for_start(&node, lease(&start))
        .await
        .unwrap();
    assert_eq!(*rotated.password, *first.password);
    let retired = store(vault(new, 62)).await;
    assert_eq!(
        *retired
            .credentials_for_start(&node, lease(&start))
            .await
            .unwrap()
            .password,
        *first.password
    );
    assert!(db
        .credentials_for_start(&node, lease(&start))
        .await
        .is_err());
    retired.close().await;
    rotate.close().await;
    db.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn wrong_mode_lease_node_and_revoked_owner_cannot_create_credentials() {
    let fixture = Fixture::new().await;
    let db = store(vault(Uuid::new_v4(), 61)).await;
    let (node, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let (_, _, start) = fixture.started(&node, app.id).await;
    assert!(db
        .credentials_for_start(&node, lease(&start))
        .await
        .is_err());
    assert_eq!(count(&fixture, node.id()).await, 0);
    let (node, app, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let (_, instance, start) = fixture.started(&node, app.id).await;
    assert!(db
        .credentials_for_start(
            &node,
            WorkspaceCommandLease {
                command_id: start.id,
                lease_id: Uuid::new_v4()
            }
        )
        .await
        .is_err());
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
    assert!(db
        .credentials_for_start(&other, lease(&start))
        .await
        .is_err());
    let origin: (Uuid, Uuid) =
        sqlx::query_as("SELECT owner_user,login_session_id FROM pixels.instances WHERE id=$1")
            .bind(instance.id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    fixture
        .identity
        .revoke_session(origin.0, origin.1)
        .await
        .unwrap();
    assert!(db
        .credentials_for_start(&node, lease(&start))
        .await
        .is_err());
    assert_eq!(count(&fixture, node.id()).await, 0);
    db.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn audit_failure_rolls_back_creation_confirmation_and_reencryption() {
    let fixture = Fixture::new().await;
    let db = store(vault(Uuid::new_v4(), 61)).await;
    let (node, app, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let (_, _, start) = fixture.started(&node, app.id).await;
    sqlx::query("REVOKE INSERT ON pixels.workspace_audit FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let outcome = db.credentials_for_start(&node, lease(&start)).await;
    sqlx::query("GRANT INSERT ON pixels.workspace_audit TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(outcome.is_err());
    assert_eq!(count(&fixture, node.id()).await, 0);
    let credential = db
        .credentials_for_start(&node, lease(&start))
        .await
        .unwrap();
    for confirm in [true, false] {
        sqlx::query("REVOKE INSERT ON pixels.workspace_audit FROM pixels_console_runtime")
            .execute(&fixture.owner)
            .await
            .unwrap();
        let failed = if confirm {
            db.confirm_account(
                &node,
                lease(&start),
                credential.workspace_id,
                "S-1-5-21-1-2-3-1001",
            )
            .await
            .is_err()
        } else {
            db.rewrap_managed(&fixture.admin, credential.workspace_id, 1)
                .await
                .is_err()
        };
        sqlx::query("GRANT INSERT ON pixels.workspace_audit TO pixels_console_runtime")
            .execute(&fixture.owner)
            .await
            .unwrap();
        assert!(failed);
        assert_eq!(revision(&fixture, credential.workspace_id).await, 1);
        assert!(db
            .credentials_for_start(&node, lease(&start))
            .await
            .unwrap()
            .windows_sid
            .is_none());
    }
    db.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn management_is_secret_free_bounded_and_requires_current_management_role() {
    let fixture = Fixture::new().await;
    let db = store(vault(Uuid::new_v4(), 61)).await;
    let (node, app, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let (user, _, start) = fixture.started(&node, app.id).await;
    let credential = db
        .credentials_for_start(&node, lease(&start))
        .await
        .unwrap();
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    assert!(db.list_managed(&user, None, 10).await.is_err());
    assert!(db.list_managed(&viewer, None, 0).await.is_err());
    assert!(db.list_managed(&viewer, None, 101).await.is_err());
    assert!(db
        .rewrap_managed(&viewer, credential.workspace_id, 1)
        .await
        .is_err());
    let mut after = None;
    let mut found = false;
    loop {
        let rows = db.list_managed(&viewer, after, 100).await.unwrap();
        if rows.is_empty() {
            break;
        }
        let json = serde_json::to_string(&rows).unwrap();
        for forbidden in ["password", "ciphertext", "nonce", "key_id"] {
            assert!(!json.contains(forbidden));
        }
        found |= rows.iter().any(|row| row.id == credential.workspace_id);
        after = rows.last().map(|row| row.id);
    }
    assert!(found);
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(sqlx::query("DELETE FROM pixels.rdp_workspaces WHERE id=$1")
        .bind(credential.workspace_id)
        .execute(&runtime)
        .await
        .is_err());
    assert!(sqlx::query(
        "UPDATE pixels.rdp_workspaces SET account_name='pxrdp_ffffffffffffff' WHERE id=$1"
    )
    .bind(credential.workspace_id)
    .execute(&runtime)
    .await
    .is_err());
    runtime.close().await;
    db.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn missing_or_tampered_keys_fail_closed_without_replacing_persistent_identity() {
    let fixture = Fixture::new().await;
    let id = Uuid::new_v4();
    let db = store(vault(id, 61)).await;
    let (node, app, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let (_, _, start) = fixture.started(&node, app.id).await;
    let original = db
        .credentials_for_start(&node, lease(&start))
        .await
        .unwrap();
    for bad in [vault(id, 62), vault(Uuid::new_v4(), 61)] {
        let wrong = store(bad).await;
        assert!(matches!(
            wrong.credentials_for_start(&node, lease(&start)).await,
            Err(StoreError::RecoveryRequired)
        ));
        assert_eq!(count(&fixture, node.id()).await, 1);
        assert_eq!(revision(&fixture, original.workspace_id).await, 1);
        wrong.close().await;
    }
    sqlx::query("UPDATE pixels.workspace_secrets SET ciphertext=set_byte(ciphertext,0,get_byte(ciphertext,0)#1) WHERE workspace_id=$1")
        .bind(original.workspace_id).execute(&fixture.owner).await.unwrap();
    assert!(matches!(
        db.credentials_for_start(&node, lease(&start)).await,
        Err(StoreError::RecoveryRequired)
    ));
    assert_eq!(count(&fixture, node.id()).await, 1);
    sqlx::query("UPDATE pixels.workspace_secrets SET ciphertext=set_byte(ciphertext,0,get_byte(ciphertext,0)#1) WHERE workspace_id=$1")
        .bind(original.workspace_id).execute(&fixture.owner).await.unwrap();
    assert_eq!(
        *db.credentials_for_start(&node, lease(&start))
            .await
            .unwrap()
            .password,
        *original.password
    );
    db.close().await;
    fixture.close().await;
}
