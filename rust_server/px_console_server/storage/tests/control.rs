use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ClientType, ControlStore, DeliveryFailure, GroupStore, IdentityStore, PasswordDigest, Role,
    StoreError, TokenDigest, Username,
};
use px_pg::{DatabaseConfig, DatabaseError, Transport};
use std::{
    env,
    sync::{Arc, OnceLock},
    time::Duration,
};
use uuid::Uuid;

fn config(role: &str) -> DatabaseConfig {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let source = env::var(format!("PIXELS_TEST_CONSOLE_{role}_URL")).unwrap();
    let database = if cfg!(windows) {
        "pixels_console_control_windows"
    } else {
        "pixels_console_control_linux"
    };
    DatabaseConfig::parse(
        &format!("{}/{database}", source.rsplit_once('/').unwrap().0),
        Transport::LocalDevelopment,
    )
    .unwrap()
}
fn name() -> Username {
    Username::parse(&Uuid::new_v4().to_string()).unwrap()
}
fn token() -> TokenDigest {
    let mut bytes = [0; 32];
    bytes[..16].copy_from_slice(Uuid::new_v4().as_bytes());
    bytes[16..].copy_from_slice(Uuid::new_v4().as_bytes());
    TokenDigest::from_sha256(bytes)
}
fn password() -> PasswordDigest {
    static HASH: OnceLock<String> = OnceLock::new();
    PasswordDigest::parse(
        HASH.get_or_init(|| {
            Argon2::default()
                .hash_password(
                    b"synthetic-password",
                    &SaltString::encode_b64(&[53; 16]).unwrap(),
                )
                .unwrap()
                .to_string()
        })
        .clone(),
    )
    .unwrap()
}
struct Fixture {
    control: ControlStore,
    identity: IdentityStore,
    groups: GroupStore,
    owner: sqlx::PgPool,
    admin: TokenDigest,
    id: Uuid,
}
impl Fixture {
    async fn new() -> Self {
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let identity = IdentityStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let control = ControlStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let groups = GroupStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let owner = config("OWNER").connect().await.unwrap();
        let user = identity.register(&name(), &password()).await.unwrap();
        sqlx::query("UPDATE pixels.users SET role='admin' WHERE id=$1")
            .bind(user.id)
            .execute(&owner)
            .await
            .unwrap();
        let admin = token();
        identity
            .issue_session(
                user.id,
                1,
                &admin,
                ClientType::AdminWeb,
                Duration::from_secs(3600),
            )
            .await
            .unwrap();
        Self {
            control,
            identity,
            groups,
            owner,
            admin,
            id: user.id,
        }
    }
    async fn session(&self, id: Uuid, revision: i64, client: ClientType) -> TokenDigest {
        let token = token();
        self.identity
            .issue_session(id, revision, &token, client, Duration::from_secs(3600))
            .await
            .unwrap();
        token
    }
    async fn close(self) {
        self.groups.close().await;
        self.control.close().await;
        self.identity.close().await;
        self.owner.close().await;
    }
    async fn drain(&self) {
        for _ in 0..20 {
            let events = self.control.claim_events(100).await.unwrap();
            if events.is_empty() {
                return;
            }
            for event in events {
                self.control
                    .complete_event(event.id, event.lease_id)
                    .await
                    .unwrap();
            }
        }
        panic!("outbox fixture unexpectedly large");
    }
}
#[tokio::test]
async fn a_last_admin_and_two_competing_self_demotions_preserve_an_administrator() {
    let f = Fixture::new().await;
    assert_eq!(sqlx::query_scalar::<_,i64>("SELECT count(*) FROM pixels.users WHERE role='admin' AND NOT disabled AND deleted_at IS NULL").fetch_one(&f.owner).await.unwrap(),1,"requires the dedicated fresh control fixture database");
    assert_eq!(
        f.control
            .update_user(&f.admin, f.id, 1, Role::User, false)
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    assert!(f.control.delete_user(&f.admin, f.id, 1).await.is_err());
    let other = f
        .control
        .create_user(&f.admin, &name(), &password(), Role::Admin)
        .await
        .unwrap();
    let other_token = f.session(other.id, 1, ClientType::AdminWeb).await;
    let (one, two) = tokio::join!(
        f.control.update_user(&f.admin, f.id, 1, Role::User, false),
        f.control
            .update_user(&other_token, other.id, 1, Role::User, false)
    );
    assert_ne!(one.is_ok(), two.is_ok());
    assert_eq!(sqlx::query_scalar::<_,i64>("SELECT count(*) FROM pixels.users WHERE role='admin' AND NOT disabled AND deleted_at IS NULL").fetch_one(&f.owner).await.unwrap(),1);
    f.close().await;
}
#[tokio::test]
async fn roles_and_client_types_never_manufacture_management_privilege() {
    let f = Fixture::new().await;
    let user = f.identity.register(&name(), &password()).await.unwrap();
    assert_eq!(
        sqlx::query_scalar::<_, String>("SELECT role FROM pixels.users WHERE id=$1")
            .bind(user.id)
            .fetch_one(&f.owner)
            .await
            .unwrap(),
        "user"
    );
    let disguised = f.session(user.id, 1, ClientType::AdminWeb).await;
    assert!(f.control.list_users(&disguised, None, 100).await.is_err());
    assert!(f
        .groups
        .create(&disguised, "must-not-create", "")
        .await
        .is_err());
    let panel = f.session(f.id, 1, ClientType::Panel).await;
    assert!(f.control.list_users(&panel, None, 100).await.is_err());
    let viewer = f
        .control
        .create_user(&f.admin, &name(), &password(), Role::Viewer)
        .await
        .unwrap();
    let viewer = f.session(viewer.id, 1, ClientType::AdminWeb).await;
    assert!(!f
        .control
        .list_users(&viewer, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert!(f
        .control
        .update_user(&viewer, user.id, 1, Role::Admin, false)
        .await
        .is_err());
    assert!(f
        .groups
        .create(&viewer, "viewer-must-not-create", "")
        .await
        .is_err());
    let group = f
        .groups
        .create(&f.admin, &Uuid::new_v4().to_string(), "")
        .await
        .unwrap();
    assert!(f.groups.get(&viewer, group.id).await.is_ok());
    assert!(f
        .groups
        .replace_members(&viewer, group.id, 1, &[user.id])
        .await
        .is_err());
    assert!(f
        .groups
        .members(&f.admin, group.id)
        .await
        .unwrap()
        .is_empty());
    assert_eq!(
        sqlx::query_scalar::<_, i64>(
            "SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1"
        )
        .bind(user.id)
        .fetch_one(&f.owner)
        .await
        .unwrap(),
        0
    );
    f.close().await;
}
#[tokio::test]
async fn concurrent_user_cas_has_one_winner_and_one_durable_revocation() {
    let f = Fixture::new().await;
    let user = f.identity.register(&name(), &password()).await.unwrap();
    let old = f.session(user.id, 1, ClientType::Android).await;
    let barrier = Arc::new(tokio::sync::Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let control = f.control.clone();
        let admin = f.admin.clone();
        let barrier = barrier.clone();
        tasks.push(tokio::spawn(async move {
            barrier.wait().await;
            control
                .update_user(&admin, user.id, 1, Role::User, true)
                .await
        }));
    }
    let mut winners = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(user) => {
                winners += 1;
                assert_eq!(user.authorization_revision, 2);
            }
            Err(error) => assert_eq!(error, StoreError::Rejected),
        }
    }
    assert_eq!(winners, 1);
    assert!(f
        .identity
        .authenticate(&old, ClientType::Android)
        .await
        .is_err());
    assert_eq!(
        sqlx::query_scalar::<_, i64>(
            "SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1"
        )
        .bind(user.id)
        .fetch_one(&f.owner)
        .await
        .unwrap(),
        1
    );
    assert_eq!(
        sqlx::query_scalar::<_, i64>(
            "SELECT count(*) FROM pixels.authorization_audit WHERE subject_id=$1"
        )
        .bind(user.id)
        .fetch_one(&f.owner)
        .await
        .unwrap(),
        1
    );
    let enabled = f
        .control
        .update_user(&f.admin, user.id, 2, Role::User, false)
        .await
        .unwrap();
    assert_eq!(enabled.authorization_revision, 3);
    assert!(f
        .identity
        .authenticate(&old, ClientType::Android)
        .await
        .is_err());
    f.control.delete_user(&f.admin, user.id, 3).await.unwrap();
    assert!(f
        .identity
        .issue_session(
            user.id,
            4,
            &token(),
            ClientType::Android,
            Duration::from_secs(60)
        )
        .await
        .is_err());
    assert!(sqlx::query_scalar::<_, bool>(
        "SELECT deleted_at IS NOT NULL FROM pixels.users WHERE id=$1"
    )
    .bind(user.id)
    .fetch_one(&f.owner)
    .await
    .unwrap());
    f.close().await;
}
#[tokio::test]
async fn failed_outbox_insert_rolls_back_role_revision_and_audit() {
    let f = Fixture::new().await;
    let user = f.identity.register(&name(), &password()).await.unwrap();
    sqlx::query("REVOKE INSERT ON pixels.authorization_outbox FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let failed = f
        .control
        .update_user(&f.admin, user.id, 1, Role::Viewer, true)
        .await;
    sqlx::query("GRANT INSERT ON pixels.authorization_outbox TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert_eq!(
        failed.unwrap_err(),
        StoreError::Database(DatabaseError::Permission)
    );
    let state: (String, bool, i64, i64) = sqlx::query_as(
        "SELECT role,disabled,revision,authorization_revision FROM pixels.users WHERE id=$1",
    )
    .bind(user.id)
    .fetch_one(&f.owner)
    .await
    .unwrap();
    assert_eq!(state, ("user".into(), false, 1, 1));
    assert_eq!(
        sqlx::query_scalar::<_, i64>(
            "SELECT count(*) FROM pixels.authorization_audit WHERE subject_id=$1"
        )
        .bind(user.id)
        .fetch_one(&f.owner)
        .await
        .unwrap(),
        0
    );
    f.control
        .update_user(&f.admin, user.id, 1, Role::Viewer, true)
        .await
        .unwrap();
    f.close().await;
}
#[tokio::test]
async fn group_change_and_its_notification_audit_commit_or_rollback_together() {
    let f = Fixture::new().await;
    let a = f.identity.register(&name(), &password()).await.unwrap();
    let b = f.identity.register(&name(), &password()).await.unwrap();
    let group = f
        .groups
        .create(&f.admin, &Uuid::new_v4().to_string(), "")
        .await
        .unwrap();
    f.groups
        .replace_members(&f.admin, group.id, 1, &[a.id])
        .await
        .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.authorization_outbox FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let failed = f
        .groups
        .replace_members(&f.admin, group.id, 2, &[b.id])
        .await;
    sqlx::query("GRANT INSERT ON pixels.authorization_outbox TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    assert_eq!(
        f.groups.members(&f.admin, group.id).await.unwrap(),
        vec![a.id]
    );
    assert_eq!(f.groups.get(&f.admin, group.id).await.unwrap().revision, 2);
    assert_eq!(
        sqlx::query_scalar::<_, i64>("SELECT authorization_revision FROM pixels.users WHERE id=$1")
            .bind(b.id)
            .fetch_one(&f.owner)
            .await
            .unwrap(),
        1
    );
    assert_eq!(
        sqlx::query_scalar::<_, i64>(
            "SELECT count(*) FROM pixels.authorization_audit WHERE group_id=$1"
        )
        .bind(group.id)
        .fetch_one(&f.owner)
        .await
        .unwrap(),
        1
    );
    f.groups
        .replace_members(&f.admin, group.id, 2, &[b.id])
        .await
        .unwrap();
    f.groups.delete(&f.admin, group.id, 3).await.unwrap();
    assert_eq!(sqlx::query_scalar::<_,i64>("SELECT count(*) FROM pixels.authorization_audit WHERE group_id=$1 AND action='group_deleted'").bind(group.id).fetch_one(&f.owner).await.unwrap(),1);
    f.close().await;
}
#[tokio::test]
async fn logout_is_idempotent_without_revoking_other_sessions_and_password_changes_enqueue() {
    let f = Fixture::new().await;
    let user = f.identity.register(&name(), &password()).await.unwrap();
    let one = f.session(user.id, 1, ClientType::Android).await;
    let two = f.session(user.id, 1, ClientType::Panel).await;
    let session = f
        .identity
        .authenticate(&one, ClientType::Android)
        .await
        .unwrap();
    assert!(f
        .identity
        .revoke_session(f.id, session.session_id)
        .await
        .is_err());
    f.identity
        .revoke_session(user.id, session.session_id)
        .await
        .unwrap();
    f.identity
        .revoke_session(user.id, session.session_id)
        .await
        .unwrap();
    assert!(f
        .identity
        .authenticate(&one, ClientType::Android)
        .await
        .is_err());
    assert!(f
        .identity
        .authenticate(&two, ClientType::Panel)
        .await
        .is_ok());
    assert_eq!(sqlx::query_scalar::<_,i64>("SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1 AND reason='session_revoked'").bind(user.id).fetch_one(&f.owner).await.unwrap(),1);
    f.identity
        .change_password(&two, ClientType::Panel, 1, &password())
        .await
        .unwrap();
    assert!(f
        .identity
        .authenticate(&two, ClientType::Panel)
        .await
        .is_err());
    assert_eq!(sqlx::query_scalar::<_,i64>("SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1 AND reason='password_changed'").bind(user.id).fetch_one(&f.owner).await.unwrap(),1);
    f.close().await;
}
#[tokio::test]
async fn outbox_concurrent_claims_retry_and_stale_acks_do_not_lose_events() {
    let f = Fixture::new().await;
    f.drain().await;
    for _ in 0..20 {
        let user = f.identity.register(&name(), &password()).await.unwrap();
        f.control
            .update_user(&f.admin, user.id, 1, Role::User, true)
            .await
            .unwrap();
    }
    let barrier = Arc::new(tokio::sync::Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let control = f.control.clone();
        let barrier = barrier.clone();
        tasks.push(tokio::spawn(async move {
            barrier.wait().await;
            control.claim_events(1).await.unwrap()
        }));
    }
    let mut events = Vec::new();
    for task in tasks {
        events.extend(task.await.unwrap());
    }
    assert_eq!(events.len(), 20);
    assert_eq!(
        events
            .iter()
            .map(|event| event.id)
            .collect::<std::collections::HashSet<_>>()
            .len(),
        20
    );
    assert!(f.control.claim_events(100).await.unwrap().is_empty());
    let first = events.remove(0);
    for event in events {
        f.control
            .complete_event(event.id, event.lease_id)
            .await
            .unwrap();
    }
    sqlx::query("UPDATE pixels.authorization_outbox SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1").bind(first.id).execute(&f.owner).await.unwrap();
    assert!(f
        .control
        .complete_event(first.id, first.lease_id)
        .await
        .is_err());
    let renewed = f.control.claim_events(1).await.unwrap().remove(0);
    assert_eq!(renewed.id, first.id);
    assert_ne!(renewed.lease_id, first.lease_id);
    assert_eq!(renewed.attempts, 2);
    assert!(f
        .control
        .complete_event(first.id, first.lease_id)
        .await
        .is_err());
    f.control
        .retry_event(
            renewed.id,
            renewed.lease_id,
            30,
            DeliveryFailure::Unavailable,
        )
        .await
        .unwrap();
    assert!(f.control.claim_events(100).await.unwrap().is_empty());
    sqlx::query("UPDATE pixels.authorization_outbox SET available_at=clock_timestamp()-interval '1 second' WHERE id=$1").bind(first.id).execute(&f.owner).await.unwrap();
    let retry = f.control.claim_events(1).await.unwrap().remove(0);
    assert_eq!(retry.attempts, 3);
    f.control
        .complete_event(retry.id, retry.lease_id)
        .await
        .unwrap();
    assert!(f.control.claim_events(100).await.unwrap().is_empty());
    assert!(f.control.claim_events(0).await.is_err());
    f.close().await;
}
#[tokio::test]
async fn shared_admission_gate_serializes_revocation_and_lock_failure_is_bounded() {
    let f = Fixture::new().await;
    let user = f.identity.register(&name(), &password()).await.unwrap();
    let mut held = f.owner.begin().await.unwrap();
    sqlx::query("SELECT pg_advisory_xact_lock_shared(22091401)")
        .execute(&mut *held)
        .await
        .unwrap();
    let control = f.control.clone();
    let admin = f.admin.clone();
    let mut task = tokio::spawn(async move {
        control
            .update_user(&admin, user.id, 1, Role::User, true)
            .await
    });
    assert!(tokio::time::timeout(Duration::from_millis(100), &mut task)
        .await
        .is_err());
    held.commit().await.unwrap();
    task.await.unwrap().unwrap();
    let mut held = f.owner.begin().await.unwrap();
    sqlx::query("SELECT pg_advisory_xact_lock_shared(22091401)")
        .execute(&mut *held)
        .await
        .unwrap();
    let result = f
        .control
        .update_user(&f.admin, user.id, 2, Role::User, false)
        .await;
    held.rollback().await.unwrap();
    assert_eq!(
        result.unwrap_err(),
        StoreError::Database(DatabaseError::Unavailable)
    );
    assert!(
        sqlx::query_scalar::<_, bool>("SELECT disabled FROM pixels.users WHERE id=$1")
            .bind(user.id)
            .fetch_one(&f.owner)
            .await
            .unwrap()
    );
    f.control
        .update_user(&f.admin, user.id, 2, Role::User, false)
        .await
        .unwrap();
    f.close().await;
}
