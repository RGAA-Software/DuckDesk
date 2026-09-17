use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ApplicationAccess, ApplicationLaunch, ApplicationSpec, ApplicationStore, ClientType,
    ControlStore, DeliveryFailure, GuestBlockReason, GuestStore, IdentityStore, PasswordDigest,
    StoreError, TokenDigest, Username,
};
use px_pg::{DatabaseConfig, Transport};
use std::{collections::BTreeSet, env, sync::OnceLock, time::Duration};
use uuid::Uuid;

fn config(role: &str) -> DatabaseConfig {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    DatabaseConfig::parse(
        &env::var(format!("PIXELS_TEST_CONSOLE_{role}_URL")).unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
}
fn token() -> TokenDigest {
    let mut bytes = [0; 32];
    bytes[..16].copy_from_slice(Uuid::new_v4().as_bytes());
    bytes[16..].copy_from_slice(Uuid::new_v4().as_bytes());
    TokenDigest::from_sha256(bytes)
}
fn source() -> px_console_store::OriginFingerprint {
    let mut bytes = [0; 32];
    bytes[..16].copy_from_slice(Uuid::new_v4().as_bytes());
    bytes[16..].copy_from_slice(Uuid::new_v4().as_bytes());
    px_console_store::OriginFingerprint::from_hmac_sha256(bytes)
}
fn password() -> PasswordDigest {
    static HASH: OnceLock<String> = OnceLock::new();
    PasswordDigest::parse(
        HASH.get_or_init(|| {
            Argon2::default()
                .hash_password(
                    b"synthetic-password",
                    &SaltString::encode_b64(&[82; 16]).unwrap(),
                )
                .unwrap()
                .to_string()
        })
        .clone(),
    )
    .unwrap()
}
struct Fixture {
    guests: GuestStore,
    apps: ApplicationStore,
    identity: IdentityStore,
    control: ControlStore,
    owner: sqlx::PgPool,
    admin: TokenDigest,
}
impl Fixture {
    async fn new() -> Self {
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let guests = GuestStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let apps = ApplicationStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let identity = IdentityStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let control = ControlStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let owner = config("OWNER").connect().await.unwrap();
        let user = identity
            .register(
                &Username::parse(&Uuid::new_v4().to_string()).unwrap(),
                &password(),
            )
            .await
            .unwrap();
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
            guests,
            apps,
            identity,
            control,
            owner,
            admin,
        }
    }
    async fn events(&self, id: Uuid) -> i64 {
        sqlx::query_scalar("SELECT count(*) FROM pixels.guest_events WHERE guest_id=$1")
            .bind(id)
            .fetch_one(&self.owner)
            .await
            .unwrap()
    }
    async fn close(self) {
        self.guests.close().await;
        self.apps.close().await;
        self.identity.close().await;
        self.control.close().await;
        self.owner.close().await;
    }
}

#[tokio::test]
async fn guest_user_and_client_types_are_disjoint_and_public_is_not_anonymous_authority() {
    let f = Fixture::new().await;
    let key = token();
    let guest = f
        .guests
        .issue(
            &source(),
            &key,
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    assert_eq!(guest.client_type, "android");
    assert_eq!(
        f.guests
            .authenticate(&key, ClientType::Android)
            .await
            .unwrap()
            .id,
        guest.id
    );
    assert_eq!(
        f.guests.authenticate(&key, ClientType::Panel).await,
        Err(StoreError::Rejected)
    );
    assert!(f
        .identity
        .authenticate(&key, ClientType::Android)
        .await
        .is_err());
    assert_eq!(
        f.guests.authenticate(&f.admin, ClientType::AdminWeb).await,
        Err(StoreError::Rejected)
    );
    assert!(f.control.list_users(&key, None, 100).await.is_err());
    assert!(f
        .guests
        .issue(
            &source(),
            &key,
            ClientType::Android,
            Duration::from_secs(3600)
        )
        .await
        .is_err());
    let mut spec = ApplicationSpec {
        name: "Guest public RDP".into(),
        access: ApplicationAccess::Public,
        launch: ApplicationLaunch::Rdp,
        allow_observer: false,
        allow_takeover: false,
        disabled: false,
    };
    let public = f.apps.create(&f.admin, &spec).await.unwrap();
    spec.access = ApplicationAccess::Acl;
    let private = f.apps.create(&f.admin, &spec).await.unwrap();
    let card = f
        .apps
        .get_visible_guest(&key, ClientType::Android, public.id)
        .await
        .unwrap();
    assert_eq!(card.kind, "rdp");
    assert!(f
        .apps
        .list_visible_guest(&key, ClientType::Android, None, 100)
        .await
        .unwrap()
        .contains(&card));
    assert_eq!(
        f.apps
            .get_visible_guest(&key, ClientType::Android, private.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.apps
            .get_visible_guest(&token(), ClientType::Android, public.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.apps
            .get_visible(&key, ClientType::Android, public.id)
            .await,
        Err(StoreError::Rejected)
    );
    f.close().await;
}

#[tokio::test]
async fn expiry_logout_and_invalid_issuance_never_refresh_or_resurrect_a_guest() {
    let f = Fixture::new().await;
    let key = token();
    let other = token();
    let guest = f
        .guests
        .issue(
            &source(),
            &key,
            ClientType::UserWeb,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    f.guests
        .issue(
            &source(),
            &other,
            ClientType::UserWeb,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    for lifetime in [
        Duration::ZERO,
        Duration::from_millis(1500),
        Duration::from_secs(86401),
    ] {
        assert_eq!(
            f.guests
                .issue(&source(), &token(), ClientType::UserWeb, lifetime)
                .await,
            Err(StoreError::InvalidInput)
        );
    }
    assert_eq!(
        f.guests
            .issue(
                &source(),
                &token(),
                ClientType::AdminWeb,
                Duration::from_secs(3600)
            )
            .await,
        Err(StoreError::InvalidInput)
    );
    assert_eq!(
        f.guests.logout(&key, ClientType::Android).await,
        Err(StoreError::Rejected)
    );
    f.guests.logout(&key, ClientType::UserWeb).await.unwrap();
    f.guests.logout(&key, ClientType::UserWeb).await.unwrap();
    assert_eq!(f.events(guest.id).await, 1);
    assert_eq!(
        f.guests.authenticate(&key, ClientType::UserWeb).await,
        Err(StoreError::Rejected)
    );
    assert!(f
        .guests
        .authenticate(&other, ClientType::UserWeb)
        .await
        .is_ok());
    assert!(f
        .guests
        .issue(
            &source(),
            &key,
            ClientType::UserWeb,
            Duration::from_secs(3600)
        )
        .await
        .is_err());
    let active = f
        .guests
        .authenticate(&other, ClientType::UserWeb)
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.guest_sessions SET created_at=clock_timestamp()-interval '2 hours',expires_at=clock_timestamp()-interval '1 second' WHERE id=$1").bind(active.id).execute(&f.owner).await.unwrap();
    assert_eq!(
        f.guests.authenticate(&other, ClientType::UserWeb).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.apps
            .list_visible_guest(&other, ClientType::UserWeb, None, 100)
            .await,
        Err(StoreError::Rejected)
    );
    f.close().await;
}

#[tokio::test]
async fn block_is_administrator_cas_and_event_failure_rolls_back_the_block() {
    let f = Fixture::new().await;
    let key = token();
    let guest = f
        .guests
        .issue(
            &source(),
            &key,
            ClientType::Panel,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    assert_eq!(
        f.guests
            .block(&key, guest.id, 1, GuestBlockReason::Abuse)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.guests
            .block(&f.admin, Uuid::new_v4(), 1, GuestBlockReason::Abuse)
            .await,
        Err(StoreError::Rejected)
    );
    sqlx::query("REVOKE INSERT ON pixels.guest_events FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let blocked = f
        .guests
        .block(&f.admin, guest.id, 1, GuestBlockReason::Operator)
        .await;
    let logout = f.guests.logout(&key, ClientType::Panel).await;
    sqlx::query("GRANT INSERT ON pixels.guest_events TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(blocked.is_err());
    assert!(logout.is_err());
    assert_eq!(
        f.guests
            .authenticate(&key, ClientType::Panel)
            .await
            .unwrap()
            .revision,
        1
    );
    assert_eq!(f.events(guest.id).await, 0);
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = f.guests.clone();
        let admin = f.admin.clone();
        tasks.push(tokio::spawn(async move {
            store
                .block(&admin, guest.id, 1, GuestBlockReason::Abuse)
                .await
        }));
    }
    let mut winners = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(result) => {
                assert_eq!(result.revision, 2);
                winners += 1;
            }
            Err(error) => assert_eq!(error, StoreError::Rejected),
        }
    }
    assert_eq!(winners, 1);
    assert_eq!(f.events(guest.id).await, 1);
    assert_eq!(
        f.guests.authenticate(&key, ClientType::Panel).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.guests
            .block(&f.admin, guest.id, 2, GuestBlockReason::Abuse)
            .await
            .unwrap()
            .revision,
        2
    );
    f.guests.logout(&key, ClientType::Panel).await.unwrap();
    assert_eq!(f.events(guest.id).await, 1);
    let reopened = GuestStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap();
    assert_eq!(
        reopened.authenticate(&key, ClientType::Panel).await,
        Err(StoreError::Rejected)
    );
    reopened.close().await;
    f.close().await;
}

#[tokio::test]
async fn guest_events_have_unique_claims_stale_lease_rejection_and_durable_audit() {
    let f = Fixture::new().await;
    for _ in 0..20 {
        let events = f.guests.claim_events(100).await.unwrap();
        if events.is_empty() {
            break;
        }
        for event in events {
            f.guests
                .complete_event(event.id, event.lease_id)
                .await
                .unwrap();
        }
    }
    for _ in 0..20 {
        let key = token();
        f.guests
            .issue(
                &source(),
                &key,
                ClientType::Android,
                Duration::from_secs(3600),
            )
            .await
            .unwrap();
        f.guests.logout(&key, ClientType::Android).await.unwrap();
    }
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let guests = f.guests.clone();
        tasks.push(tokio::spawn(async move {
            guests.claim_events(1).await.unwrap()
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
            .collect::<BTreeSet<_>>()
            .len(),
        20
    );
    let event = events.pop().unwrap();
    for completed in events {
        f.guests
            .complete_event(completed.id, completed.lease_id)
            .await
            .unwrap();
    }
    sqlx::query("UPDATE pixels.guest_events SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1").bind(event.id).execute(&f.owner).await.unwrap();
    assert_eq!(
        f.guests.complete_event(event.id, event.lease_id).await,
        Err(StoreError::Rejected)
    );
    let again = f.guests.claim_events(1).await.unwrap().pop().unwrap();
    assert_ne!(again.lease_id, event.lease_id);
    assert_eq!(again.attempts, 2);
    f.guests
        .retry_event(again.id, again.lease_id, 30, DeliveryFailure::Rejected)
        .await
        .unwrap();
    assert!(f.guests.claim_events(1).await.unwrap().is_empty());
    sqlx::query("UPDATE pixels.guest_events SET available_at=clock_timestamp()-interval '1 second' WHERE id=$1").bind(event.id).execute(&f.owner).await.unwrap();
    let again = f.guests.claim_events(1).await.unwrap().pop().unwrap();
    f.guests
        .complete_event(again.id, again.lease_id)
        .await
        .unwrap();
    assert_eq!(f.events(event.guest_id).await, 1);
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(sqlx::query("DELETE FROM pixels.guest_events WHERE id=$1")
        .bind(event.id)
        .execute(&runtime)
        .await
        .is_err());
    assert!(
        sqlx::query("UPDATE pixels.guest_events SET reason='blocked' WHERE id=$1")
            .bind(event.id)
            .execute(&runtime)
            .await
            .is_err()
    );
    runtime.close().await;
    f.close().await;
}

#[tokio::test]
async fn concurrent_duplicate_issuance_and_database_unavailability_never_fake_success() {
    let f = Fixture::new().await;
    let key = token();
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = f.guests.clone();
        let key = key.clone();
        tasks.push(tokio::spawn(async move {
            store
                .issue(
                    &source(),
                    &key,
                    ClientType::Android,
                    Duration::from_secs(86400),
                )
                .await
        }));
    }
    let mut winners = 0;
    for task in tasks {
        if task.await.unwrap().is_ok() {
            winners += 1;
        }
    }
    assert_eq!(winners, 1);
    let guest = f
        .guests
        .authenticate(&key, ClientType::Android)
        .await
        .unwrap();
    assert_eq!((guest.expires_at - guest.created_at).num_seconds(), 86400);
    f.guests.close().await;
    assert!(f
        .guests
        .issue(
            &source(),
            &token(),
            ClientType::Android,
            Duration::from_secs(3600)
        )
        .await
        .is_err());
    assert!(f
        .guests
        .authenticate(&key, ClientType::Android)
        .await
        .is_err());
    f.close().await;
}

#[tokio::test]
async fn source_block_revokes_all_matching_guests_and_expiry_only_allows_new_identities() {
    let f = Fixture::new().await;
    let origin = source();
    let first = token();
    let second = token();
    let other = token();
    let selected = f
        .guests
        .issue(
            &origin,
            &first,
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let sibling = f
        .guests
        .issue(
            &origin,
            &second,
            ClientType::UserWeb,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    f.guests
        .issue(
            &source(),
            &other,
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    assert_eq!(
        f.guests
            .block_source(
                &first,
                selected.id,
                1,
                Duration::from_secs(3600),
                GuestBlockReason::Abuse
            )
            .await,
        Err(StoreError::Rejected)
    );
    let result = f
        .guests
        .block_source(
            &f.admin,
            selected.id,
            1,
            Duration::from_secs(3600),
            GuestBlockReason::Abuse,
        )
        .await
        .unwrap();
    assert_eq!(result.revision, 2);
    assert_eq!(
        f.guests.authenticate(&first, ClientType::Android).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.guests.authenticate(&second, ClientType::UserWeb).await,
        Err(StoreError::Rejected)
    );
    assert!(f
        .guests
        .authenticate(&other, ClientType::Android)
        .await
        .is_ok());
    assert!(f.control.list_users(&f.admin, None, 1).await.is_ok());
    assert_eq!(
        f.guests
            .issue(
                &origin,
                &token(),
                ClientType::Android,
                Duration::from_secs(3600)
            )
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(f.events(selected.id).await, 1);
    assert_eq!(f.events(sibling.id).await, 1);
    sqlx::query("UPDATE pixels.guest_source_blocks SET created_at=clock_timestamp()-interval '2 hours',expires_at=clock_timestamp()-interval '1 second' WHERE origin_guest_id=$1").bind(selected.id).execute(&f.owner).await.unwrap();
    let fresh = f
        .guests
        .issue(
            &origin,
            &token(),
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    assert_ne!(fresh.id, selected.id);
    assert_ne!(fresh.id, sibling.id);
    assert_eq!(
        f.guests.authenticate(&first, ClientType::Android).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.guests.authenticate(&second, ClientType::UserWeb).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(format!("{origin:?}"), "OriginFingerprint(<redacted>)");
    f.close().await;
}

#[tokio::test]
async fn source_block_event_failure_rolls_back_every_session_and_issuance_barrier() {
    let f = Fixture::new().await;
    let origin = source();
    let keys = [token(), token()];
    let first = f
        .guests
        .issue(
            &origin,
            &keys[0],
            ClientType::Panel,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let second = f
        .guests
        .issue(
            &origin,
            &keys[1],
            ClientType::Panel,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.guest_events FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let failed = f
        .guests
        .block_source(
            &f.admin,
            first.id,
            1,
            Duration::from_secs(60),
            GuestBlockReason::Operator,
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.guest_events TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    for key in &keys {
        assert_eq!(
            f.guests
                .authenticate(key, ClientType::Panel)
                .await
                .unwrap()
                .revision,
            1
        );
    }
    assert_eq!(f.events(first.id).await, 0);
    assert_eq!(f.events(second.id).await, 0);
    let blocks: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.guest_source_blocks WHERE origin_guest_id=$1",
    )
    .bind(first.id)
    .fetch_one(&f.owner)
    .await
    .unwrap();
    assert_eq!(blocks, 0);
    assert!(f
        .guests
        .issue(
            &origin,
            &token(),
            ClientType::Panel,
            Duration::from_secs(3600)
        )
        .await
        .is_ok());
    assert_eq!(
        f.guests
            .block_source(
                &f.admin,
                first.id,
                1,
                Duration::from_secs(86401),
                GuestBlockReason::Operator
            )
            .await,
        Err(StoreError::InvalidInput)
    );
    f.guests
        .block_source(
            &f.admin,
            first.id,
            1,
            Duration::from_secs(60),
            GuestBlockReason::Operator,
        )
        .await
        .unwrap();
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(
        sqlx::query("DELETE FROM pixels.guest_source_blocks WHERE origin_guest_id=$1")
            .bind(first.id)
            .execute(&runtime)
            .await
            .is_err()
    );
    runtime.close().await;
    f.close().await;
}

#[tokio::test]
async fn source_ban_racing_twenty_issuers_never_leaves_a_valid_pre_ban_guest() {
    let f = Fixture::new().await;
    for _ in 0..10 {
        let origin = source();
        let key = token();
        let first = f
            .guests
            .issue(
                &origin,
                &key,
                ClientType::Android,
                Duration::from_secs(3600),
            )
            .await
            .unwrap();
        let barrier = std::sync::Arc::new(tokio::sync::Barrier::new(21));
        let mut tasks = Vec::new();
        let mut keys = Vec::new();
        for _ in 0..20 {
            let key = token();
            keys.push(key.clone());
            let guests = f.guests.clone();
            let origin = origin.clone();
            let ready = barrier.clone();
            tasks.push(tokio::spawn(async move {
                ready.wait().await;
                guests
                    .issue(
                        &origin,
                        &key,
                        ClientType::Android,
                        Duration::from_secs(3600),
                    )
                    .await
            }));
        }
        barrier.wait().await;
        f.guests
            .block_source(
                &f.admin,
                first.id,
                1,
                Duration::from_secs(60),
                GuestBlockReason::Abuse,
            )
            .await
            .unwrap();
        for task in tasks {
            if let Err(error) = task.await.unwrap() {
                assert_eq!(error, StoreError::Rejected);
            }
        }
        for key in keys {
            assert_eq!(
                f.guests.authenticate(&key, ClientType::Android).await,
                Err(StoreError::Rejected)
            );
        }
        let live: i64=sqlx::query_scalar("SELECT count(*) FROM pixels.guest_sessions g JOIN pixels.guest_source_blocks b ON b.source_hash=g.source_hash WHERE b.origin_guest_id=$1 AND g.revoked_at IS NULL AND g.expires_at>clock_timestamp()").bind(first.id).fetch_one(&f.owner).await.unwrap();
        assert_eq!(live, 0);
    }
    f.close().await;
}

#[tokio::test]
async fn management_is_bounded_read_only_for_viewers_and_never_discloses_source_or_token_hashes() {
    let f = Fixture::new().await;
    let key = token();
    let guest = f
        .guests
        .issue(
            &source(),
            &key,
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let viewer = f
        .control
        .create_user(
            &f.admin,
            &Username::parse(&Uuid::new_v4().to_string()).unwrap(),
            &password(),
            px_console_store::Role::Viewer,
        )
        .await
        .unwrap();
    let view = token();
    f.identity
        .issue_session(
            viewer.id,
            1,
            &view,
            ClientType::AdminWeb,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    assert_eq!(
        f.guests.list_managed(&key, None, 1).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.guests
            .block_source(
                &view,
                guest.id,
                1,
                Duration::from_secs(60),
                GuestBlockReason::Abuse
            )
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.guests.list_managed(&view, None, 101).await,
        Err(StoreError::InvalidInput)
    );
    let mut after = None;
    let mut seen = BTreeSet::new();
    let mut finished = false;
    for _ in 0..100 {
        let page = f.guests.list_managed(&view, after, 100).await.unwrap();
        if page.is_empty() {
            finished = true;
            break;
        }
        assert!(page.len() <= 100);
        after = page.last().map(|guest| guest.id);
        for guest in page {
            assert!(seen.insert(guest.id));
            assert!(!format!("{guest:?}").contains("hash"));
        }
    }
    assert!(finished);
    assert!(seen.contains(&guest.id));
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.guest_sessions")
        .fetch_one(&f.owner)
        .await
        .unwrap();
    assert_eq!(i64::try_from(seen.len()).unwrap(), count);
    f.close().await;
}
