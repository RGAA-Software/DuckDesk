use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ClientType, GroupStore, IdentityStore, PasswordDigest, StoreError, TokenDigest, Username,
};
use px_pg::{DatabaseConfig, DatabaseError, Transport};
use sqlx::PgPool;
use std::{
    env,
    sync::{Arc, OnceLock},
    time::Duration,
};
use uuid::Uuid;

fn deployment() -> Uuid {
    Uuid::parse_str(&env::var("PIXELS_DEPLOYMENT_ID").expect("test deployment required")).unwrap()
}

fn config() -> DatabaseConfig {
    assert_eq!(
        env::var("PIXELS_PG_ISOLATED_TEST").as_deref(),
        Ok("1"),
        "isolated harness required"
    );
    DatabaseConfig::parse(
        &env::var("PIXELS_TEST_CONSOLE_RUNTIME_URL").expect("test database required, never skip"),
        Transport::LocalDevelopment,
    )
    .unwrap()
}

async fn store() -> IdentityStore {
    IdentityStore::connect(&config(), deployment())
        .await
        .unwrap()
}
async fn pool() -> PgPool {
    config().connect().await.unwrap()
}

fn password() -> PasswordDigest {
    static HASH: OnceLock<String> = OnceLock::new();
    PasswordDigest::parse(
        HASH.get_or_init(|| {
            let salt = SaltString::encode_b64(b"synthetic-salt!!").unwrap();
            Argon2::default()
                .hash_password(b"synthetic-test-password", &salt)
                .unwrap()
                .to_string()
        })
        .clone(),
    )
    .unwrap()
}

fn name() -> Username {
    Username::parse(&format!("fixture-{}", Uuid::new_v4())).unwrap()
}
fn token() -> TokenDigest {
    let mut bytes = [0_u8; 32];
    bytes[..16].copy_from_slice(Uuid::new_v4().as_bytes());
    bytes[16..].copy_from_slice(Uuid::new_v4().as_bytes());
    TokenDigest::from_sha256(bytes)
}

#[tokio::test]
async fn registration_lookup_commits_and_collision_does_not_overwrite() {
    let store = store().await;
    let name = name();
    let profile = store.register(&name, &password()).await.unwrap();
    let upper = Username::parse(&name.normalized().to_uppercase()).unwrap();
    let found = store.credential(&upper).await.unwrap();
    assert_eq!(found.user.id, profile.id);
    assert_eq!(found.user.authorization_revision, 1);
    assert_eq!(found.password.encoded(), password().encoded());
    assert_eq!(
        store.register(&upper, &password()).await.unwrap_err(),
        StoreError::Database(DatabaseError::Conflict)
    );
    let pool = pool().await;
    let rows: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.users WHERE username_normalized=$1")
            .bind(name.normalized())
            .fetch_one(&pool)
            .await
            .unwrap();
    assert_eq!(rows, 1);
    let columns: i64 = sqlx::query_scalar("SELECT count(*) FROM information_schema.columns WHERE table_schema='pixels' AND table_name IN ('users','login_sessions') AND column_name IN ('password','password_ciphertext','access_token')").fetch_one(&pool).await.unwrap();
    assert_eq!(columns, 0);
    pool.close().await;
    store.close().await;
}

#[tokio::test]
async fn concurrent_registration_has_exactly_one_committed_identity() {
    let store = store().await;
    let name = name();
    let barrier = Arc::new(tokio::sync::Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let (store, name, barrier) = (store.clone(), name.clone(), barrier.clone());
        let password = password();
        tasks.push(tokio::spawn(async move {
            barrier.wait().await;
            store.register(&name, &password).await
        }));
    }
    let mut winners = Vec::new();
    for task in tasks {
        match task.await.unwrap() {
            Ok(profile) => winners.push(profile.id),
            Err(error) => assert_eq!(error, StoreError::Database(DatabaseError::Conflict)),
        }
    }
    assert_eq!(winners.len(), 1);
    assert_eq!(store.credential(&name).await.unwrap().user.id, winners[0]);
    store.close().await;
}

#[tokio::test]
async fn android_session_is_distinct_and_cross_user_revoke_has_no_effect() {
    let store = store().await;
    let user = store.register(&name(), &password()).await.unwrap();
    let other = store.register(&name(), &password()).await.unwrap();
    let token = token();
    let session = store
        .issue_session(
            user.id,
            1,
            &token,
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let authorized = store
        .authenticate(&token, ClientType::Android)
        .await
        .unwrap();
    assert_eq!(authorized.client_type, "android");
    assert_eq!(authorized.user_id, user.id);
    assert_eq!(
        store
            .authenticate(&token, ClientType::Panel)
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    assert_eq!(
        store.revoke_session(other.id, session.session_id).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        store.revoke_session(user.id, Uuid::new_v4()).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        store
            .authenticate(&token, ClientType::Android)
            .await
            .unwrap()
            .session_id,
        session.session_id
    );
    store
        .revoke_session(user.id, session.session_id)
        .await
        .unwrap();
    store
        .revoke_session(user.id, session.session_id)
        .await
        .unwrap();
    assert_eq!(
        store
            .authenticate(&token, ClientType::Android)
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    store.close().await;
}

#[tokio::test]
async fn expired_session_rejected_while_row_is_still_present() {
    let store = store().await;
    let user = store.register(&name(), &password()).await.unwrap();
    let token = token();
    let session = store
        .issue_session(
            user.id,
            1,
            &token,
            ClientType::Panel,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    store.authenticate(&token, ClientType::Panel).await.unwrap();
    let pool = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_CONSOLE_OWNER_URL").unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
    .connect()
    .await
    .unwrap();
    // Controlled owner-only fixture; runtime cannot rewrite session identity or expiry.
    sqlx::query("UPDATE pixels.login_sessions SET created_at=clock_timestamp()-INTERVAL '2 hours',expires_at=clock_timestamp()-INTERVAL '1 second' WHERE id=$1").bind(session.session_id).execute(&pool).await.unwrap();
    assert_eq!(
        store
            .authenticate(&token, ClientType::Panel)
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    let rows: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.login_sessions WHERE id=$1")
        .bind(session.session_id)
        .fetch_one(&pool)
        .await
        .unwrap();
    assert_eq!(rows, 1);
    pool.close().await;
    store.close().await;
}

#[tokio::test]
async fn password_change_invalidates_old_sessions_and_inflight_password_verification() {
    let store = store().await;
    let user = store.register(&name(), &password()).await.unwrap();
    let first = token();
    store
        .issue_session(
            user.id,
            1,
            &first,
            ClientType::Panel,
            Duration::from_secs(60),
        )
        .await
        .unwrap();
    assert_eq!(
        store
            .change_password(&first, ClientType::Panel, 1, &password())
            .await
            .unwrap(),
        2
    );
    assert_eq!(
        store
            .authenticate(&first, ClientType::Panel)
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    assert_eq!(
        store
            .issue_session(
                user.id,
                1,
                &token(),
                ClientType::Panel,
                Duration::from_secs(60)
            )
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    assert_eq!(
        store
            .change_password(&first, ClientType::Panel, 1, &password())
            .await,
        Err(StoreError::Rejected)
    );
    let second = token();
    store
        .issue_session(
            user.id,
            2,
            &second,
            ClientType::Panel,
            Duration::from_secs(60),
        )
        .await
        .unwrap();
    store
        .authenticate(&second, ClientType::Panel)
        .await
        .unwrap();
    store.close().await;
}

#[tokio::test]
async fn login_and_revocation_race_never_leave_a_valid_old_revision() {
    let store = store().await;
    let user = store.register(&name(), &password()).await.unwrap();
    for revision in 1..=100 {
        let changing_session = token();
        store
            .issue_session(
                user.id,
                revision,
                &changing_session,
                ClientType::Panel,
                Duration::from_secs(60),
            )
            .await
            .unwrap();
        let token = token();
        let password = password();
        let barrier = Arc::new(tokio::sync::Barrier::new(2));
        let a = barrier.clone();
        let issue = async {
            a.wait().await;
            store
                .issue_session(
                    user.id,
                    revision,
                    &token,
                    ClientType::Android,
                    Duration::from_secs(3600),
                )
                .await
        };
        let revoke = async {
            barrier.wait().await;
            store
                .change_password(&changing_session, ClientType::Panel, revision, &password)
                .await
        };
        let (issue, revoke) = tokio::join!(issue, revoke);
        assert_eq!(revoke.unwrap(), revision + 1);
        if let Err(error) = issue {
            assert_eq!(error, StoreError::Rejected);
        }
        assert_eq!(
            store
                .authenticate(&token, ClientType::Android)
                .await
                .unwrap_err(),
            StoreError::Rejected
        );
    }
    store.close().await;
}

#[tokio::test]
async fn disabled_and_deleted_users_are_rejected_without_cleanup() {
    let store = store().await;
    let pool = pool().await;
    for state in ["disabled=TRUE", "deleted_at=clock_timestamp()"] {
        let name = name();
        let user = store.register(&name, &password()).await.unwrap();
        let token = token();
        store
            .issue_session(
                user.id,
                1,
                &token,
                ClientType::Panel,
                Duration::from_secs(60),
            )
            .await
            .unwrap();
        store.authenticate(&token, ClientType::Panel).await.unwrap();
        sqlx::query(&format!("UPDATE pixels.users SET {state} WHERE id=$1"))
            .bind(user.id)
            .execute(&pool)
            .await
            .unwrap();
        assert_eq!(
            store.credential(&name).await.unwrap_err(),
            StoreError::Rejected
        );
        assert_eq!(
            store
                .authenticate(&token, ClientType::Panel)
                .await
                .unwrap_err(),
            StoreError::Rejected
        );
        assert_eq!(
            store
                .issue_session(
                    user.id,
                    1,
                    &token,
                    ClientType::Panel,
                    Duration::from_secs(60)
                )
                .await
                .unwrap_err(),
            StoreError::Rejected
        );
    }
    pool.close().await;
    store.close().await;
}

#[tokio::test]
async fn invalid_sessions_have_no_persistent_effect_and_database_failure_is_not_success() {
    let store = store().await;
    let user = store.register(&name(), &password()).await.unwrap();
    for lifetime in [
        Duration::ZERO,
        Duration::from_millis(1),
        Duration::from_secs(366 * 86400),
    ] {
        assert_eq!(
            store
                .issue_session(user.id, 1, &token(), ClientType::Panel, lifetime)
                .await
                .unwrap_err(),
            StoreError::InvalidInput
        );
    }
    assert_eq!(
        store
            .issue_session(
                Uuid::new_v4(),
                1,
                &token(),
                ClientType::Panel,
                Duration::from_secs(60)
            )
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    let pool = pool().await;
    let rows: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.login_sessions WHERE user_id=$1")
            .bind(user.id)
            .fetch_one(&pool)
            .await
            .unwrap();
    assert_eq!(rows, 0);
    pool.close().await;
    store.close().await;
    assert_eq!(
        store.register(&name(), &password()).await.unwrap_err(),
        StoreError::Database(DatabaseError::Unavailable)
    );
    assert_eq!(
        store
            .authenticate(&token(), ClientType::Panel)
            .await
            .unwrap_err(),
        StoreError::Database(DatabaseError::Unavailable)
    );
    assert!(matches!(
        IdentityStore::connect(&config(), Uuid::new_v4()).await,
        Err(StoreError::Database(DatabaseError::Identity))
    ));
}

#[tokio::test]
async fn group_membership_changes_invalidate_only_affected_users() {
    let identities = store().await;
    let groups = GroupStore::connect(&config(), deployment()).await.unwrap();
    let admin = administrator().await;
    let a = identities.register(&name(), &password()).await.unwrap();
    let b = identities.register(&name(), &password()).await.unwrap();
    let group = groups
        .create(&admin, &format!("group-{}", Uuid::new_v4()), "fixture")
        .await
        .unwrap();
    let group = groups
        .replace_members(&admin, group.id, 1, &[a.id, b.id])
        .await
        .unwrap();
    assert_eq!(group.revision, 2);
    let unchanged = groups
        .replace_members(&admin, group.id, 2, &[b.id, a.id])
        .await
        .unwrap();
    assert_eq!(unchanged, group);
    let ta = token();
    let tb = token();
    identities
        .issue_session(a.id, 2, &ta, ClientType::Panel, Duration::from_secs(60))
        .await
        .unwrap();
    identities
        .issue_session(b.id, 2, &tb, ClientType::Panel, Duration::from_secs(60))
        .await
        .unwrap();
    let group = groups
        .replace_members(&admin, group.id, 2, &[b.id])
        .await
        .unwrap();
    assert_eq!(group.revision, 3);
    assert_eq!(
        identities
            .authenticate(&ta, ClientType::Panel)
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    identities
        .authenticate(&tb, ClientType::Panel)
        .await
        .unwrap();
    assert_eq!(groups.members(&admin, group.id).await.unwrap(), vec![b.id]);
    groups.delete(&admin, group.id, 3).await.unwrap();
    assert_eq!(
        identities
            .authenticate(&tb, ClientType::Panel)
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    assert_eq!(
        groups.get(&admin, group.id).await.unwrap_err(),
        StoreError::Rejected
    );
    assert!(groups.members(&admin, group.id).await.unwrap().is_empty());
    // Soft-deleted group names may be reused, without reusing their identity/membership.
    let new_group = groups.create(&admin, &group.name, "new").await.unwrap();
    assert_ne!(new_group.id, group.id);
    groups.close().await;
    identities.close().await;
}

#[tokio::test]
async fn invalid_group_replacement_preserves_revision_members_and_user_authorization() {
    let identities = store().await;
    let groups = GroupStore::connect(&config(), deployment()).await.unwrap();
    let admin = administrator().await;
    let user = identities.register(&name(), &password()).await.unwrap();
    let group = groups
        .create(&admin, &format!("group-{}", Uuid::new_v4()), "fixture")
        .await
        .unwrap();
    let group = groups
        .replace_members(&admin, group.id, 1, &[user.id])
        .await
        .unwrap();
    for ids in [vec![Uuid::new_v4()], vec![user.id, user.id]] {
        assert!(groups
            .replace_members(&admin, group.id, 2, &ids)
            .await
            .is_err());
        assert_eq!(groups.get(&admin, group.id).await.unwrap(), group);
        assert_eq!(
            groups.members(&admin, group.id).await.unwrap(),
            vec![user.id]
        );
    }
    assert_eq!(
        groups
            .replace_members(&admin, group.id, 1, &[])
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    let pool = pool().await;
    let revision: i64 =
        sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
            .bind(user.id)
            .fetch_one(&pool)
            .await
            .unwrap();
    assert_eq!(revision, 2);
    let bad_fk = sqlx::query("INSERT INTO pixels.group_members(group_id,user_id) VALUES($1,$2)")
        .bind(group.id)
        .bind(Uuid::new_v4())
        .execute(&pool)
        .await
        .unwrap_err();
    assert_eq!(DatabaseError::from(bad_fk), DatabaseError::Conflict);
    assert_eq!(
        groups
            .create(&admin, &group.name.to_uppercase(), "duplicate")
            .await
            .unwrap_err(),
        StoreError::Database(DatabaseError::Conflict)
    );
    for invalid in ["", " padded", "bad\nname"] {
        assert_eq!(
            groups.create(&admin, invalid, "").await.unwrap_err(),
            StoreError::InvalidInput
        );
    }
    pool.close().await;
    groups.close().await;
    identities.close().await;
}

#[tokio::test]
async fn failed_member_insert_rolls_back_prior_delete_and_all_revisions() {
    let identities = store().await;
    let groups = GroupStore::connect(&config(), deployment()).await.unwrap();
    let admin = administrator().await;
    let a = identities.register(&name(), &password()).await.unwrap();
    let b = identities.register(&name(), &password()).await.unwrap();
    let group = groups
        .create(&admin, &format!("fault-{}", Uuid::new_v4()), "fixture")
        .await
        .unwrap();
    let group = groups
        .replace_members(&admin, group.id, 1, &[a.id])
        .await
        .unwrap();
    let owner = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_CONSOLE_OWNER_URL").unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
    .connect()
    .await
    .unwrap();
    let ddl = format!("CREATE FUNCTION pixels.test_member_failure() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN IF NEW.group_id='{}'::uuid THEN RAISE EXCEPTION 'synthetic fault' USING ERRCODE='23514'; END IF; RETURN NEW; END $$; CREATE TRIGGER test_member_failure BEFORE INSERT ON pixels.group_members FOR EACH ROW EXECUTE FUNCTION pixels.test_member_failure();", group.id);
    sqlx::raw_sql(&ddl).execute(&owner).await.unwrap();
    let result = groups.replace_members(&admin, group.id, 2, &[b.id]).await;
    sqlx::raw_sql("DROP TRIGGER test_member_failure ON pixels.group_members; DROP FUNCTION pixels.test_member_failure();").execute(&owner).await.unwrap();
    assert_eq!(
        result.unwrap_err(),
        StoreError::Database(DatabaseError::Conflict)
    );
    assert_eq!(groups.get(&admin, group.id).await.unwrap(), group);
    assert_eq!(groups.members(&admin, group.id).await.unwrap(), vec![a.id]);
    let revisions: Vec<(Uuid, i64)> = sqlx::query_as(
        "SELECT id,authorization_revision FROM pixels.users WHERE id=ANY($1) ORDER BY id",
    )
    .bind(vec![a.id, b.id])
    .fetch_all(&owner)
    .await
    .unwrap();
    assert!(revisions.contains(&(a.id, 2)) && revisions.contains(&(b.id, 1)));
    groups
        .replace_members(&admin, group.id, 2, &[b.id])
        .await
        .unwrap();
    owner.close().await;
    groups.close().await;
    identities.close().await;
}

#[tokio::test]
async fn competing_membership_replacements_have_one_cas_winner() {
    let identities = store().await;
    let groups = GroupStore::connect(&config(), deployment()).await.unwrap();
    let admin = administrator().await;
    let a = identities.register(&name(), &password()).await.unwrap();
    let b = identities.register(&name(), &password()).await.unwrap();
    let group = groups
        .create(&admin, &format!("race-{}", Uuid::new_v4()), "fixture")
        .await
        .unwrap();
    let barrier = Arc::new(tokio::sync::Barrier::new(20));
    let mut tasks = Vec::new();
    for index in 0..20 {
        let groups = groups.clone();
        let admin = admin.clone();
        let barrier = barrier.clone();
        let id = if index % 2 == 0 { a.id } else { b.id };
        tasks.push(tokio::spawn(async move {
            barrier.wait().await;
            groups
                .replace_members(&admin, group.id, 1, &[id])
                .await
                .map(|_| id)
        }));
    }
    let mut winners = Vec::new();
    for task in tasks {
        match task.await.unwrap() {
            Ok(id) => winners.push(id),
            Err(error) => assert_eq!(error, StoreError::Rejected),
        }
    }
    assert_eq!(winners.len(), 1);
    assert_eq!(groups.members(&admin, group.id).await.unwrap(), winners);
    assert_eq!(groups.get(&admin, group.id).await.unwrap().revision, 2);
    groups.close().await;
    identities.close().await;
}
async fn administrator() -> TokenDigest {
    let identities = store().await;
    let user = identities.register(&name(), &password()).await.unwrap();
    let owner = DatabaseConfig::parse(
        &env::var("PIXELS_TEST_CONSOLE_OWNER_URL").unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
    .connect()
    .await
    .unwrap();
    sqlx::query("UPDATE pixels.users SET role='admin' WHERE id=$1")
        .bind(user.id)
        .execute(&owner)
        .await
        .unwrap();
    let token = token();
    identities
        .issue_session(
            user.id,
            1,
            &token,
            ClientType::AdminWeb,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    owner.close().await;
    identities.close().await;
    token
}
