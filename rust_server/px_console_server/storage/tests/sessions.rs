#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, node_report, request, token, Fixture};
use px_console_store::{
    ApplicationInstance, ClientType, CommandOutcome, CommandReceipt, DeploymentTarget,
    DeviceAccess, NodeConnection, OpenResourceSession, ResourceCredential, ResourceSession,
    ResourceSessionStore, SessionAccess, SessionTarget, StoreError, TokenDigest,
};
use std::{env, sync::Arc, time::Duration};
use tokio::sync::Barrier;
use uuid::Uuid;

#[tokio::test]
async fn close_request_needs_node_proof_and_observer_policy_is_not_control_authority() {
    let (f, s, node, user, instance, session) = opened().await;
    let mut observer = open_request(instance.application_id, instance.id);
    observer.access = SessionAccess::Observer;
    assert!(s
        .open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &observer
        )
        .await
        .is_err());
    sqlx::query("UPDATE pixels.applications SET allow_observer=true WHERE id=$1")
        .bind(instance.application_id)
        .execute(&f.owner)
        .await
        .unwrap();
    let watching = s
        .open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &observer,
        )
        .await
        .unwrap();
    let ticket = token();
    let descriptor = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            watching.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    let grant = s
        .admit_frontend(&node, watching.id, descriptor.session.revision, &ticket)
        .await
        .unwrap();
    assert_eq!(grant.session.access_role, "observer");
    let closing = s
        .request_close(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
        )
        .await
        .unwrap();
    assert_eq!(closing.state, "closing");
    assert_eq!(
        closing,
        s.request_close(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            closing.revision
        )
        .await
        .unwrap()
    );
    assert_eq!(
        s.open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &open_request(instance.application_id, instance.id)
        )
        .await
        .unwrap_err(),
        StoreError::NoCapacity
    );
    let listed = s.list_node(&node).await.unwrap();
    assert_eq!(listed.len(), 2);
    assert!(listed
        .iter()
        .any(|r| r.id == session.id && r.state == "closing"));
    let fence = s.begin_retirement(&node, session.id).await.unwrap();
    s.finish_retirement(&node, session.id, fence.challenge_id)
        .await
        .unwrap();
    assert_eq!(s.list_node(&node).await.unwrap().len(), 1);
    assert!(s
        .open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &open_request(instance.application_id, instance.id)
        )
        .await
        .is_ok());
    sqlx::query("UPDATE pixels.applications SET allow_observer=false WHERE id=$1")
        .bind(instance.application_id)
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(s
        .admit_frontend(&node, watching.id, descriptor.session.revision, &ticket)
        .await
        .is_err());
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn draining_keeps_existing_lease_but_endpoint_change_and_wrong_node_reject() {
    let (f, s, node, user, instance, session) = opened().await;
    let ticket = token();
    let descriptor = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    let (_, other_key) = f.node().await;
    let other = f
        .nodes
        .open_connection(node.epoch(), &other_key, &token())
        .await
        .unwrap();
    f.nodes.report(&other, &node_report(1)).await.unwrap();
    assert!(s
        .admit_frontend(&other, session.id, descriptor.session.revision, &ticket)
        .await
        .is_err());
    assert!(s.begin_retirement(&other, session.id).await.is_err());
    assert!(s.list_node(&other).await.unwrap().is_empty());
    let draining = f
        .nodes
        .configure(
            &f.admin,
            node.id(),
            1,
            px_console_store::NodeConfiguration {
                draining: true,
                disabled: false,
                max_instances: 4,
            },
        )
        .await
        .unwrap();
    assert!(s
        .admit_frontend(&node, session.id, descriptor.session.revision, &ticket)
        .await
        .is_ok());
    let fence = s.begin_retirement(&node, session.id).await.unwrap();
    s.finish_retirement(&node, session.id, fence.challenge_id)
        .await
        .unwrap();
    assert_eq!(
        s.open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &open_request(instance.application_id, instance.id)
        )
        .await
        .unwrap_err(),
        StoreError::NoCapacity
    );
    f.nodes
        .configure(
            &f.admin,
            node.id(),
            draining.revision,
            px_console_store::NodeConfiguration {
                draining: false,
                disabled: false,
                max_instances: 4,
            },
        )
        .await
        .unwrap();
    let new = s
        .open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &open_request(instance.application_id, instance.id),
        )
        .await
        .unwrap();
    let next = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            new.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    let mut changed = node_report(2);
    changed.public_host = "changed.example.test".into();
    f.nodes.report(&node, &changed).await.unwrap();
    assert_eq!(row(&f, new.id).await.0, "reconcile_required");
    assert!(s
        .admit_frontend(&node, new.id, next.session.revision, &ticket)
        .await
        .is_err());
    assert!(s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            new.id,
            next.session.revision,
            &token()
        )
        .await
        .is_err());
    s.close().await;
    f.close().await;
}

async fn store() -> ResourceSessionStore {
    ResourceSessionStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap()
}
fn open_request(app: Uuid, instance: Uuid) -> OpenResourceSession {
    OpenResourceSession {
        request_id: Uuid::new_v4(),
        target: SessionTarget::CloudApplication {
            application_id: app,
            instance_id: instance,
        },
        access: SessionAccess::Controller,
    }
}
async fn running(
    f: &Fixture,
    node: &NodeConnection,
    app: Uuid,
    key: &TokenDigest,
    client: ClientType,
    guest: bool,
) -> ApplicationInstance {
    let credential = if guest {
        ResourceCredential::Guest(key)
    } else {
        ResourceCredential::User(key)
    };
    let instance = f
        .instances
        .reserve(credential, client, node.epoch(), &request(app))
        .await
        .unwrap();
    let command = f.instances.next_command(node).await.unwrap().unwrap();
    let port = match command.action {
        px_console_store::NodeCommandAction::Start { port, .. } => port,
        _ => panic!("expected start command"),
    };
    f.instances
        .acknowledge_command(
            node,
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
        .unwrap()
}
async fn row(f: &Fixture, id: Uuid) -> (String, i64, Option<Vec<u8>>) {
    sqlx::query_as(
        "SELECT state,revision,descriptor_hash FROM pixels.resource_sessions WHERE id=$1",
    )
    .bind(id)
    .fetch_one(&f.owner)
    .await
    .unwrap()
}
async fn count(f: &Fixture, node: Uuid) -> i64 {
    sqlx::query_scalar("SELECT count(*) FROM pixels.resource_sessions WHERE node_id=$1")
        .bind(node)
        .fetch_one(&f.owner)
        .await
        .unwrap()
}
async fn event_permission(f: &Fixture, allow: bool) {
    let sql = if allow {
        "GRANT INSERT ON pixels.resource_session_events TO pixels_console_runtime"
    } else {
        "REVOKE INSERT ON pixels.resource_session_events FROM pixels_console_runtime"
    };
    sqlx::query(sql).execute(&f.owner).await.unwrap();
}
async fn opened() -> (
    Fixture,
    ResourceSessionStore,
    NodeConnection,
    TokenDigest,
    ApplicationInstance,
    ResourceSession,
) {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, app, _) = f.prepared(DeploymentTarget::Webview, 4).await;
    let user = f.session("user", ClientType::Android).await;
    let instance = running(&f, &node, app.id, &user, ClientType::Android, false).await;
    let session = s
        .open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &open_request(app.id, instance.id),
        )
        .await
        .unwrap();
    (f, s, node, user, instance, session)
}
#[tokio::test]
async fn android_targets_idempotency_owner_and_guest_are_explicit() {
    let (f, s, node, user, instance, session) = opened().await;
    assert_eq!(
        session.target,
        SessionTarget::CloudApplication {
            application_id: instance.application_id,
            instance_id: instance.id
        }
    );
    assert_eq!(session.client_type, "android");
    let json = serde_json::to_value(&session).unwrap();
    assert_eq!(json["target"]["kind"], "cloud_application");
    assert!(json["target"].get("device_id").is_none());
    for body in [
        r#"{"request_id":"00000000-0000-4000-8000-000000000001","target":{"kind":"cloud_application","application_id":"00000000-0000-4000-8000-000000000002"},"access":"controller"}"#,
        r#"{"request_id":"00000000-0000-4000-8000-000000000001","target":{"kind":"desktop","device_id":"00000000-0000-4000-8000-000000000002","account_id":"fallback"},"access":"controller"}"#,
    ] {
        assert!(serde_json::from_str::<OpenResourceSession>(body).is_err());
    }
    let other = f.session("user", ClientType::Android).await;
    let mut req = open_request(instance.application_id, instance.id);
    assert!(s
        .open(ResourceCredential::User(&other), ClientType::Android, &req)
        .await
        .is_err());
    assert!(s
        .open(ResourceCredential::User(&user), ClientType::Panel, &req)
        .await
        .is_err());
    req.target = SessionTarget::CloudApplication {
        application_id: Uuid::new_v4(),
        instance_id: instance.id,
    };
    assert!(s
        .open(ResourceCredential::User(&user), ClientType::Android, &req)
        .await
        .is_err());
    let (guest, _) = f.guest().await;
    assert!(s
        .open(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            &open_request(instance.application_id, instance.id)
        )
        .await
        .is_err());
    let guest_instance = running(
        &f,
        &node,
        instance.application_id,
        &guest,
        ClientType::Android,
        true,
    )
    .await;
    let req = open_request(instance.application_id, guest_instance.id);
    let first = s
        .open(ResourceCredential::Guest(&guest), ClientType::Android, &req)
        .await
        .unwrap();
    assert_eq!(
        first,
        s.open(ResourceCredential::Guest(&guest), ClientType::Android, &req)
            .await
            .unwrap()
    );
    let mut changed = req.clone();
    changed.access = SessionAccess::Observer;
    assert!(s
        .open(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            &changed
        )
        .await
        .is_err());
    assert!(s
        .open(ResourceCredential::User(&guest), ClientType::Android, &req)
        .await
        .is_err());
    assert_eq!(count(&f, node.id()).await, 2);
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn rdp_last_frontend_is_atomic_and_never_uses_android_or_observer_fallback() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, app, _) = f.prepared(DeploymentTarget::Rdp, 1).await;
    let key = f.session("user", ClientType::Panel).await;
    let instance = running(&f, &node, app.id, &key, ClientType::Panel, false).await;
    let barrier = Arc::new(Barrier::new(20));
    let mut handles = Vec::new();
    for _ in 0..20 {
        let (s, key, b) = (s.clone(), key.clone(), barrier.clone());
        let req = open_request(app.id, instance.id);
        handles.push(tokio::spawn(async move {
            b.wait().await;
            s.open(ResourceCredential::User(&key), ClientType::Panel, &req)
                .await
        }));
    }
    let mut winner = None;
    let mut denied = 0;
    for h in handles {
        match h.await.unwrap() {
            Ok(session) => {
                assert!(winner.is_none());
                winner = Some(session)
            }
            Err(StoreError::NoCapacity) => denied += 1,
            other => panic!("unexpected result: {other:?}"),
        }
    }
    assert_eq!(denied, 19);
    let winner = winner.unwrap();
    let mut req = open_request(app.id, instance.id);
    req.access = SessionAccess::Observer;
    assert!(s
        .open(ResourceCredential::User(&key), ClientType::Panel, &req)
        .await
        .is_err());
    let descriptor = s
        .descriptor(
            ResourceCredential::User(&key),
            ClientType::Panel,
            winner.id,
            1,
            &token(),
        )
        .await
        .unwrap();
    assert_eq!(descriptor.transport, "rdp");
    let challenge = s.begin_retirement(&node, winner.id).await.unwrap();
    let closed = s
        .finish_retirement(&node, winner.id, challenge.challenge_id)
        .await
        .unwrap();
    assert_eq!(closed.state, "closed");
    assert_eq!(
        closed,
        s.finish_retirement(&node, winner.id, challenge.challenge_id)
            .await
            .unwrap()
    );
    assert_eq!(
        f.instances
            .get(
                ResourceCredential::User(&key),
                ClientType::Panel,
                instance.id
            )
            .await
            .unwrap()
            .state,
        "running"
    );
    assert!(s
        .open(
            ResourceCredential::User(&key),
            ClientType::Panel,
            &open_request(app.id, instance.id)
        )
        .await
        .is_ok());
    // Explicitly provision a separate Android-origin RDP fixture; descriptor/open must still deny it.
    let (node2, app2, _) = f.prepared(DeploymentTarget::Rdp, 1).await;
    let android = f.session("user", ClientType::Android).await;
    let i2 = running(&f, &node2, app2.id, &android, ClientType::Android, false).await;
    assert!(s
        .open(
            ResourceCredential::User(&android),
            ClientType::Android,
            &open_request(app2.id, i2.id)
        )
        .await
        .is_err());
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn descriptors_cas_expire_rotate_and_node_admission_is_current_authority() {
    let (f, s, node, user, instance, session) = opened().await;
    let ticket = token();
    let descriptor = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    assert_eq!(descriptor.host, "node.example.test");
    assert_eq!(descriptor.port, 4613);
    assert_eq!(descriptor.transport, "native");
    let seconds = (descriptor.expires_at - chrono::Utc::now()).num_seconds();
    assert!((0..=30).contains(&seconds));
    let first = s
        .admit_frontend(&node, session.id, descriptor.session.revision, &ticket)
        .await
        .unwrap();
    assert_eq!(first.session.state, "connected");
    assert_eq!(first.expires_at, descriptor.expires_at);
    assert!((1..=30000).contains(&first.valid_for_ms));
    assert_eq!(
        first.session,
        s.admit_frontend(&node, session.id, descriptor.session.revision, &ticket)
            .await
            .unwrap()
            .session
    );
    assert!(s
        .admit_frontend(&node, session.id, descriptor.session.revision, &token())
        .await
        .is_err());
    assert!(s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &token()
        )
        .await
        .is_err());
    let next_ticket = token();
    let next = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            descriptor.session.revision,
            &next_ticket,
        )
        .await
        .unwrap();
    assert!(s
        .admit_frontend(&node, session.id, descriptor.session.revision, &ticket)
        .await
        .is_err());
    sqlx::query("UPDATE pixels.resource_sessions SET descriptor_expires_at=clock_timestamp()-interval '1 second' WHERE id=$1")
        .bind(session.id).execute(&f.owner).await.unwrap();
    assert!(s
        .admit_frontend(&node, session.id, next.session.revision, &next_ticket)
        .await
        .is_err());
    // Expired tickets do not mean the frontend is gone.
    assert_eq!(
        s.open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &open_request(instance.application_id, instance.id)
        )
        .await
        .unwrap_err(),
        StoreError::NoCapacity
    );
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn reconnect_retirement_is_fenced_challenged_and_does_not_free_unknown_occupancy() {
    let (f, s, node, user, instance, session) = opened().await;
    let key = token();
    let descriptor = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &key,
        )
        .await
        .unwrap();
    let old = s.begin_retirement(&node, session.id).await.unwrap();
    let epoch = f.nodes.begin_runtime().await.unwrap();
    assert_eq!(row(&f, session.id).await.0, "reconcile_required");
    assert!(s
        .finish_retirement(&node, session.id, old.challenge_id)
        .await
        .is_err());
    assert!(s
        .admit_frontend(&node, session.id, descriptor.session.revision, &key)
        .await
        .is_err());
    let mut enrollment_bytes = [0_u8; 32];
    enrollment_bytes[..16].copy_from_slice(Uuid::new_v4().as_bytes());
    enrollment_bytes[16..].copy_from_slice(Uuid::new_v4().as_bytes());
    let enrollment = TokenDigest::from_sha256(enrollment_bytes);
    // Owner fixture simulates explicit credential provisioning, never product fallback.
    sqlx::query("UPDATE pixels.nodes SET credential_hash=$2 WHERE id=$1")
        .bind(node.id())
        .bind(enrollment_bytes.as_slice())
        .execute(&f.owner)
        .await
        .unwrap();
    let current = f
        .nodes
        .open_connection(epoch, &enrollment, &token())
        .await
        .unwrap();
    f.nodes.report(&current, &node_report(1)).await.unwrap();
    assert!(s
        .finish_retirement(&current, session.id, old.challenge_id)
        .await
        .is_err());
    let c1 = s.begin_retirement(&current, session.id).await.unwrap();
    let c2 = s.begin_retirement(&current, session.id).await.unwrap();
    assert!(s
        .finish_retirement(&current, session.id, c1.challenge_id)
        .await
        .is_err());
    sqlx::query("UPDATE pixels.resource_session_retirements SET deadline=clock_timestamp()-interval '1 second' WHERE session_id=$1")
        .bind(session.id).execute(&f.owner).await.unwrap();
    assert!(s
        .finish_retirement(&current, session.id, c2.challenge_id)
        .await
        .is_err());
    assert_eq!(row(&f, session.id).await.0, "closing");
    let current_challenge = s.begin_retirement(&current, session.id).await.unwrap();
    s.finish_retirement(&current, session.id, current_challenge.challenge_id)
        .await
        .unwrap();
    let state: String = sqlx::query_scalar("SELECT state FROM pixels.instances WHERE id=$1")
        .bind(instance.id)
        .fetch_one(&f.owner)
        .await
        .unwrap();
    assert_eq!(state, "reconcile_required"); // No runtime stop or fabricated instance reconciliation.
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn logout_and_guest_revocation_deny_descriptors_and_new_login_cannot_rebind_origin() {
    let (f, s, node, user, instance, session) = opened().await;
    let ticket = token();
    let d = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    let login = f
        .identity
        .authenticate(&user, ClientType::Android)
        .await
        .unwrap();
    f.identity
        .revoke_session(login.user_id, login.session_id)
        .await
        .unwrap();
    assert!(s
        .admit_frontend(&node, session.id, d.session.revision, &ticket)
        .await
        .is_err());
    let replacement = token();
    f.identity
        .issue_session(
            login.user_id,
            login.authorization_revision,
            &replacement,
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    assert!(s
        .descriptor(
            ResourceCredential::User(&replacement),
            ClientType::Android,
            session.id,
            d.session.revision,
            &token()
        )
        .await
        .is_err());
    assert!(s
        .open(
            ResourceCredential::User(&replacement),
            ClientType::Android,
            &open_request(instance.application_id, instance.id)
        )
        .await
        .is_err());
    assert!(s
        .get(
            ResourceCredential::User(&replacement),
            ClientType::Android,
            session.id
        )
        .await
        .is_ok());
    let (guest, _) = f.guest().await;
    let i = running(
        &f,
        &node,
        instance.application_id,
        &guest,
        ClientType::Android,
        true,
    )
    .await;
    let g = s
        .open(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            &open_request(i.application_id, i.id),
        )
        .await
        .unwrap();
    let gt = token();
    let gd = s
        .descriptor(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            g.id,
            1,
            &gt,
        )
        .await
        .unwrap();
    f.guests.logout(&guest, ClientType::Android).await.unwrap();
    assert!(s
        .admit_frontend(&node, g.id, gd.session.revision, &gt)
        .await
        .is_err());
    assert_eq!(row(&f, g.id).await.0, "pending");
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn desktop_requires_device_acl_and_never_accepts_guest_or_admin_web() {
    let f = Fixture::new().await;
    let s = store().await;
    let (node, _, _) = f.prepared(DeploymentTarget::Webview, 4).await;
    let device: Uuid = sqlx::query_scalar("SELECT device_id FROM pixels.nodes WHERE id=$1")
        .bind(node.id())
        .fetch_one(&f.owner)
        .await
        .unwrap();
    let user = f.session("user", ClientType::Android).await;
    let identity = f
        .identity
        .authenticate(&user, ClientType::Android)
        .await
        .unwrap();
    let req = OpenResourceSession {
        request_id: Uuid::new_v4(),
        target: SessionTarget::Desktop { device_id: device },
        access: SessionAccess::Controller,
    };
    assert!(s
        .open(ResourceCredential::User(&user), ClientType::Android, &req)
        .await
        .is_err());
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
    // ACL changes revoke authorization revisions; obtain an explicit new login, not an old-token fallback.
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
    let session = s
        .open(ResourceCredential::User(&user), ClientType::Android, &req)
        .await
        .unwrap();
    let ticket = token();
    let d = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    assert_eq!(d.port, 4601);
    let (guest, _) = f.guest().await;
    assert!(s
        .open(ResourceCredential::Guest(&guest), ClientType::Android, &req)
        .await
        .is_err());
    assert!(s
        .open(
            ResourceCredential::User(&f.admin),
            ClientType::AdminWeb,
            &req
        )
        .await
        .is_err());
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
        .admit_frontend(&node, session.id, d.session.revision, &ticket)
        .await
        .is_err());
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn event_failures_rollback_creation_descriptor_confirmation_and_retirement() {
    let (f, s, node, user, instance, session) = opened().await;
    let ticket = token();
    event_permission(&f, false).await;
    let failed = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &ticket,
        )
        .await;
    event_permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(row(&f, session.id).await, ("pending".into(), 1, None));
    let d = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    let before = row(&f, session.id).await;
    event_permission(&f, false).await;
    let failed = s
        .admit_frontend(&node, session.id, d.session.revision, &ticket)
        .await;
    event_permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(before, row(&f, session.id).await);
    event_permission(&f, false).await;
    let failed = s.begin_retirement(&node, session.id).await;
    event_permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(before, row(&f, session.id).await);
    let challenge = s.begin_retirement(&node, session.id).await.unwrap();
    event_permission(&f, false).await;
    let failed = s
        .finish_retirement(&node, session.id, challenge.challenge_id)
        .await;
    event_permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(row(&f, session.id).await.0, "closing");
    s.finish_retirement(&node, session.id, challenge.challenge_id)
        .await
        .unwrap();
    event_permission(&f, false).await;
    let failed = s
        .open(
            ResourceCredential::User(&user),
            ClientType::Android,
            &open_request(instance.application_id, instance.id),
        )
        .await;
    event_permission(&f, true).await;
    assert!(failed.is_err());
    assert_eq!(count(&f, node.id()).await, 1);
    s.close().await;
    f.close().await;
}
#[tokio::test]
async fn management_is_bounded_redacted_and_persistent_without_runtime_delete_privileges() {
    let (f, s, node, user, _, session) = opened().await;
    let ticket = token();
    let d = s
        .descriptor(
            ResourceCredential::User(&user),
            ClientType::Android,
            session.id,
            1,
            &ticket,
        )
        .await
        .unwrap();
    let viewer = f.session("viewer", ClientType::AdminWeb).await;
    assert!(s.list_managed(&viewer, None, 0).await.is_err());
    assert!(s.list_managed(&viewer, None, 101).await.is_err());
    assert!(s.list_managed(&user, None, 1).await.is_err());
    let mut after = None;
    let mut found = false;
    loop {
        let page = s.list_managed(&viewer, after, 10).await.unwrap();
        if page.is_empty() {
            break;
        }
        for record in &page {
            if record.id == session.id {
                found = true;
            }
            assert!(after.is_none_or(|previous| record.id > previous));
            after = Some(record.id);
            let text = serde_json::to_string(record).unwrap();
            for secret in [
                "descriptor_hash",
                "login_session_id",
                "password",
                "public_host",
                "credential",
            ] {
                assert!(!text.contains(secret));
            }
        }
    }
    assert!(found);
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(
        sqlx::query("DELETE FROM pixels.resource_sessions WHERE id=$1")
            .bind(session.id)
            .execute(&runtime)
            .await
            .is_err()
    );
    assert!(
        sqlx::query("UPDATE pixels.resource_sessions SET owner_user=$2 WHERE id=$1")
            .bind(session.id)
            .bind(Uuid::new_v4())
            .execute(&runtime)
            .await
            .is_err()
    );
    runtime.close().await;
    s.close().await;
    assert!(s.list_managed(&viewer, None, 1).await.is_err());
    let reconnected = store().await;
    assert_eq!(
        reconnected
            .get(
                ResourceCredential::User(&user),
                ClientType::Android,
                session.id
            )
            .await
            .unwrap()
            .revision,
        d.session.revision
    );
    assert!(reconnected
        .admit_frontend(&node, session.id, d.session.revision, &ticket)
        .await
        .is_ok());
    reconnected.close().await;
    f.close().await;
}
