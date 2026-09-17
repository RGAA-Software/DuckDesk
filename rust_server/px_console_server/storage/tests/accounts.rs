#[path = "support/node_fixture.rs"]
mod fixture;
use fixture::{config, password, token, Fixture};
use px_console_store::*;
use px_pg::{DatabaseConfig, DatabaseError, Transport};
use std::{env, sync::Arc};
use uuid::Uuid;
use zeroize::Zeroizing;
fn deployment() -> Uuid {
    env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap()
}
fn name() -> Username {
    Username::parse(&Uuid::new_v4().to_string()).unwrap()
}
fn bootstrap_config(role: &str) -> DatabaseConfig {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let mut url =
        url::Url::parse(&env::var(format!("PIXELS_TEST_CONSOLE_{role}_URL")).unwrap()).unwrap();
    url.set_path(if cfg!(windows) {
        "/pixels_console_bootstrap_windows"
    } else {
        "/pixels_console_bootstrap_linux"
    });
    DatabaseConfig::parse(url.as_str(), Transport::LocalDevelopment).unwrap()
}
fn vault() -> Arc<WorkspaceVault> {
    let id = Uuid::new_v4();
    Arc::new(
        WorkspaceVault::new(
            id,
            vec![WorkspaceKey {
                id,
                bytes: Zeroizing::new([21; 32]),
            }],
        )
        .unwrap(),
    )
}
#[tokio::test]
async fn administrator_password_reset_is_revision_guarded_revokes_all_sessions_and_is_atomic() {
    let fixture = Fixture::new().await;
    let control = ControlStore::connect(&config("RUNTIME"), deployment())
        .await
        .unwrap();
    let key = fixture.session("user", ClientType::Android).await;
    let user = fixture
        .identity
        .profile(&key, ClientType::Android)
        .await
        .unwrap();
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    assert!(control
        .reset_password(&viewer, user.id, 1, &password())
        .await
        .is_err());
    assert!(control
        .reset_password(&fixture.admin, user.id, 2, &password())
        .await
        .is_err());
    sqlx::query("REVOKE INSERT ON pixels.authorization_audit FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let failed = control
        .reset_password(&fixture.admin, user.id, 1, &password())
        .await;
    sqlx::query("GRANT INSERT ON pixels.authorization_audit TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    assert_eq!(
        fixture
            .identity
            .profile(&key, ClientType::Android)
            .await
            .unwrap()
            .revision,
        1
    );
    let updated = control
        .reset_password(&fixture.admin, user.id, 1, &password())
        .await
        .unwrap();
    assert_eq!(updated.revision, 2);
    assert!(fixture
        .identity
        .profile(&key, ClientType::Android)
        .await
        .is_err());
    assert!(control
        .reset_password(&fixture.admin, user.id, 1, &password())
        .await
        .is_err());
    let audit:i64=sqlx::query_scalar("SELECT count(*) FROM pixels.authorization_audit WHERE subject_id=$1 AND action='password_reset'").bind(user.id).fetch_one(&fixture.owner).await.unwrap();
    let events:i64=sqlx::query_scalar("SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1 AND reason='password_changed'").bind(user.id).fetch_one(&fixture.owner).await.unwrap();
    assert_eq!((audit, events), (1, 1));
    control.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn empty_group_creation_and_deletion_are_audited_and_failed_audit_rolls_back() {
    let fixture = Fixture::new().await;
    let groups = GroupStore::connect(&config("RUNTIME"), deployment())
        .await
        .unwrap();
    let name = Uuid::new_v4().to_string();
    sqlx::query("REVOKE INSERT ON pixels.group_events FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let failed = groups.create(&fixture.admin, &name, "").await;
    sqlx::query("GRANT INSERT ON pixels.group_events TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    let group = groups.create(&fixture.admin, &name, "").await.unwrap();
    groups
        .replace_members(&fixture.admin, group.id, 1, &[])
        .await
        .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.group_events FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let failed = groups.delete(&fixture.admin, group.id, 1).await;
    sqlx::query("GRANT INSERT ON pixels.group_events TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    assert_eq!(
        groups.get(&fixture.admin, group.id).await.unwrap().revision,
        1
    );
    groups.delete(&fixture.admin, group.id, 1).await.unwrap();
    let events:Vec<(String,i64,i32)>=sqlx::query_as("SELECT action,revision,member_count FROM pixels.group_events WHERE group_id=$1 ORDER BY revision").bind(group.id).fetch_all(&fixture.owner).await.unwrap();
    assert_eq!(
        events,
        vec![("created".into(), 1, 0), ("deleted".into(), 2, 0)]
    );
    let runtime = config("RUNTIME").connect().await.unwrap();
    for sql in [
        "DELETE FROM pixels.group_events",
        "UPDATE pixels.group_events SET actor_id=gen_random_uuid()",
    ] {
        assert_eq!(
            DatabaseError::from(sqlx::query(sql).execute(&runtime).await.unwrap_err()),
            DatabaseError::Permission
        );
    }
    runtime.close().await;
    groups.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn bootstrap_is_owner_only_empty_only_atomic_and_twenty_contenders_create_one_administrator()
{
    let owner = bootstrap_config("OWNER").connect().await.unwrap();
    let db = ConsoleDatabase::connect(&bootstrap_config("RUNTIME"), deployment(), vault())
        .await
        .unwrap();
    assert!(!db.initialized().await.unwrap());
    assert!(initialize_administrator(
        &bootstrap_config("RUNTIME"),
        deployment(),
        &name(),
        &password()
    )
    .await
    .is_err());
    assert!(initialize_administrator(
        &bootstrap_config("OWNER"),
        Uuid::new_v4(),
        &name(),
        &password()
    )
    .await
    .is_err());
    sqlx::query("CREATE FUNCTION pixels.fail_bootstrap_audit() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'synthetic bootstrap audit failure'; END $$").execute(&owner).await.unwrap();
    sqlx::query("CREATE TRIGGER fail_bootstrap_audit BEFORE INSERT ON pixels.authorization_audit FOR EACH ROW EXECUTE FUNCTION pixels.fail_bootstrap_audit()").execute(&owner).await.unwrap();
    let failed = initialize_administrator(
        &bootstrap_config("OWNER"),
        deployment(),
        &name(),
        &password(),
    )
    .await;
    sqlx::query("DROP TRIGGER fail_bootstrap_audit ON pixels.authorization_audit")
        .execute(&owner)
        .await
        .unwrap();
    sqlx::query("DROP FUNCTION pixels.fail_bootstrap_audit()")
        .execute(&owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.users")
        .fetch_one(&owner)
        .await
        .unwrap();
    assert_eq!(count, 0);
    let mut tasks = tokio::task::JoinSet::new();
    let barrier = Arc::new(tokio::sync::Barrier::new(20));
    for _ in 0..20 {
        let barrier = barrier.clone();
        tasks.spawn(async move {
            barrier.wait().await;
            initialize_administrator(
                &bootstrap_config("OWNER"),
                deployment(),
                &name(),
                &password(),
            )
            .await
        });
    }
    let mut winners = Vec::new();
    while let Some(row) = tasks.join_next().await {
        match row.unwrap() {
            Ok(row) => winners.push(row),
            Err(store_error) => assert_eq!(store_error, StoreError::Rejected),
        }
    }
    assert_eq!(winners.len(), 1);
    assert_eq!(winners[0].role, "admin");
    assert!(db.initialized().await.unwrap());
    let count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.authorization_audit WHERE action='user_created'",
    )
    .fetch_one(&owner)
    .await
    .unwrap();
    assert_eq!(count, 1);
    assert!(initialize_administrator(
        &bootstrap_config("OWNER"),
        deployment(),
        &name(),
        &password()
    )
    .await
    .is_err());
    db.close().await;
    owner.close().await;
}
#[tokio::test]
async fn session_credentials_and_profiles_are_exactly_bound_and_never_serialize_passwords() {
    let fixture = Fixture::new().await;
    for role in ["user", "admin", "viewer"] {
        let key = fixture.session(role, ClientType::Android).await;
        let credential = fixture
            .identity
            .session_credential(&key, ClientType::Android)
            .await
            .unwrap();
        let profile = fixture
            .identity
            .profile(&key, ClientType::Android)
            .await
            .unwrap();
        assert_eq!(credential.user.id, profile.id);
        assert_eq!(credential.role.name(), role);
        assert!(fixture
            .identity
            .session_credential(&key, ClientType::Panel)
            .await
            .is_err());
        assert!(fixture
            .identity
            .profile(&key, ClientType::AdminWeb)
            .await
            .is_err());
        let encoded = serde_json::to_string(&profile).unwrap();
        for secret in [
            "password_hash",
            "password_ciphertext",
            "token_hash",
            "$argon2",
        ] {
            assert!(!encoded.contains(secret));
        }
        let by_name = fixture
            .identity
            .credential(&Username::parse(&profile.username).unwrap())
            .await
            .unwrap();
        assert_eq!(by_name.role, credential.role);
    }
    assert!(fixture
        .identity
        .profile(&token(), ClientType::Android)
        .await
        .is_err());
    fixture.close().await;
}
#[tokio::test]
async fn logout_after_password_verification_denies_the_late_password_write_without_side_effects() {
    let fixture = Fixture::new().await;
    let key = fixture.session("user", ClientType::Android).await;
    let checked = fixture
        .identity
        .session_credential(&key, ClientType::Android)
        .await
        .unwrap();
    let session = fixture
        .identity
        .authenticate(&key, ClientType::Android)
        .await
        .unwrap();
    fixture
        .identity
        .revoke_session(session.user_id, session.session_id)
        .await
        .unwrap();
    assert_eq!(
        fixture
            .identity
            .change_password(
                &key,
                ClientType::Android,
                checked.user.authorization_revision,
                &password()
            )
            .await,
        Err(StoreError::Rejected)
    );
    assert!(fixture
        .identity
        .profile(&key, ClientType::Android)
        .await
        .is_err());
    assert!(fixture
        .identity
        .session_credential(&key, ClientType::Android)
        .await
        .is_err());
    let current = fixture
        .identity
        .credential(&Username::parse(&checked.user.username).unwrap())
        .await
        .unwrap();
    assert_eq!(
        current.user.authorization_revision,
        checked.user.authorization_revision
    );
    assert_eq!(current.password.encoded(), checked.password.encoded());
    let count:i64=sqlx::query_scalar("SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1 AND reason='password_changed'")
        .bind(session.user_id).fetch_one(&fixture.owner).await.unwrap();
    assert_eq!(count, 0);
    fixture.close().await;
}
#[tokio::test]
async fn one_hundred_password_logout_races_have_a_single_authorized_order() {
    let fixture = Fixture::new().await;
    for _ in 0..100 {
        let key = fixture.session("user", ClientType::Panel).await;
        let session = fixture
            .identity
            .authenticate(&key, ClientType::Panel)
            .await
            .unwrap();
        let password = password();
        let gate = tokio::sync::Barrier::new(2);
        let change = async {
            gate.wait().await;
            fixture
                .identity
                .change_password(&key, ClientType::Panel, 1, &password)
                .await
        };
        let logout = async {
            gate.wait().await;
            fixture
                .identity
                .revoke_session(session.user_id, session.session_id)
                .await
        };
        let (changed, logged_out) = tokio::join!(change, logout);
        logged_out.unwrap();
        let revision: i64 =
            sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
                .bind(session.user_id)
                .fetch_one(&fixture.owner)
                .await
                .unwrap();
        match changed {
            Ok(next) => {
                assert_eq!(next, 2);
                assert_eq!(revision, 2)
            }
            Err(store_error) => {
                assert_eq!(store_error, StoreError::Rejected);
                assert_eq!(revision, 1)
            }
        }
        assert!(fixture
            .identity
            .authenticate(&key, ClientType::Panel)
            .await
            .is_err());
        let events:i64=sqlx::query_scalar("SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1 AND reason='password_changed'")
            .bind(session.user_id).fetch_one(&fixture.owner).await.unwrap();
        assert_eq!(events, revision - 1);
    }
    fixture.close().await;
}
#[tokio::test]
async fn wrong_client_stale_password_revision_and_failed_outbox_never_change_credentials() {
    let fixture = Fixture::new().await;
    let key = fixture.session("user", ClientType::Panel).await;
    let current = fixture
        .identity
        .session_credential(&key, ClientType::Panel)
        .await
        .unwrap();
    for (client, revision) in [(ClientType::Android, 1), (ClientType::Panel, 2)] {
        assert_eq!(
            fixture
                .identity
                .change_password(&key, client, revision, &password())
                .await,
            Err(StoreError::Rejected)
        );
    }
    sqlx::query("REVOKE INSERT ON pixels.authorization_outbox FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let failed = fixture
        .identity
        .change_password(&key, ClientType::Panel, 1, &password())
        .await;
    sqlx::query("GRANT INSERT ON pixels.authorization_outbox TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    assert_eq!(
        fixture
            .identity
            .session_credential(&key, ClientType::Panel)
            .await
            .unwrap()
            .user
            .authorization_revision,
        1
    );
    let other = fixture.session("user", ClientType::Panel).await;
    fixture
        .identity
        .change_password(&other, ClientType::Panel, 1, &password())
        .await
        .unwrap();
    assert_eq!(
        fixture
            .identity
            .profile(&key, ClientType::Panel)
            .await
            .unwrap()
            .id,
        current.user.id
    );
    fixture.close().await;
}
#[tokio::test]
async fn runtime_cannot_rebind_or_extend_logins_or_physically_delete_identity_history() {
    let fixture = Fixture::new().await;
    let runtime = config("RUNTIME").connect().await.unwrap();
    for sql in [
        "UPDATE pixels.users SET id=gen_random_uuid()",
        "DELETE FROM pixels.users",
        "UPDATE pixels.login_sessions SET client_type='admin_web'",
        "UPDATE pixels.login_sessions SET authorization_revision=authorization_revision+1",
        "UPDATE pixels.login_sessions SET expires_at=absolute_expires_at",
        "UPDATE pixels.login_sessions SET token_hash=decode(repeat('aa',32),'hex')",
        "DELETE FROM pixels.login_sessions",
        "UPDATE pixels.user_groups SET id=gen_random_uuid()",
        "DELETE FROM pixels.user_groups",
        "UPDATE pixels.group_members SET user_id=gen_random_uuid()",
    ] {
        let error = sqlx::query(sql).execute(&runtime).await.unwrap_err();
        assert_eq!(DatabaseError::from(error), DatabaseError::Permission);
    }
    runtime.close().await;
    fixture.close().await;
}
#[tokio::test]
async fn group_directory_is_management_only_bounded_and_does_not_return_deleted_groups() {
    let fixture = Fixture::new().await;
    let groups = GroupStore::connect(&config("RUNTIME"), deployment())
        .await
        .unwrap();
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    let ordinary = fixture.session("user", ClientType::Panel).await;
    let created_group = groups
        .create(&fixture.admin, &Uuid::new_v4().to_string(), "分组")
        .await
        .unwrap();
    assert!(groups.list(&ordinary, None, 1).await.is_err());
    for limit in [0, 101] {
        assert!(groups.list(&viewer, None, limit).await.is_err());
    }
    let mut after = None;
    let mut seen = std::collections::HashSet::new();
    loop {
        let page = groups.list(&viewer, after, 2).await.unwrap();
        if page.is_empty() {
            break;
        }
        for row in page {
            if let Some(previous) = after {
                assert!(row.id > previous);
            }
            assert!(seen.insert(row.id));
            after = Some(row.id);
        }
    }
    assert!(seen.contains(&created_group.id));
    groups
        .delete(&fixture.admin, created_group.id, 1)
        .await
        .unwrap();
    assert!(groups.get(&viewer, created_group.id).await.is_err());
    groups.close().await;
    assert!(groups.list(&viewer, None, 1).await.is_err());
    fixture.close().await;
}
