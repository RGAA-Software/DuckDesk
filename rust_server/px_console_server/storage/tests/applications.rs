use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ApplicationAccess, ApplicationLaunch, ApplicationSpec, ApplicationStore, ClientType,
    ControlStore, DeliveryFailure, GroupStore, IdentityStore, PasswordDigest, Role, StoreError,
    TokenDigest, Username, VideoCodec, VideoSpec,
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
fn name() -> Username {
    Username::parse(&Uuid::new_v4().to_string()).unwrap()
}
fn password() -> PasswordDigest {
    static HASH: OnceLock<String> = OnceLock::new();
    PasswordDigest::parse(
        HASH.get_or_init(|| {
            Argon2::default()
                .hash_password(
                    b"synthetic-password",
                    &SaltString::encode_b64(&[72; 16]).unwrap(),
                )
                .unwrap()
                .to_string()
        })
        .clone(),
    )
    .unwrap()
}
fn spec() -> ApplicationSpec {
    ApplicationSpec {
        name: "云应用".into(),
        access: ApplicationAccess::Acl,
        launch: ApplicationLaunch::GameHook {
            executable_relative: "游戏 1\\启动器.exe".into(),
            arguments: "--name \"甲 乙\" --path \"C:\\有空格 的路径\"".into(),
            video: VideoSpec {
                codec: VideoCodec::H264,
                bitrate_kbps: 20_000,
            },
        },
        allow_observer: true,
        allow_takeover: true,
        disabled: false,
    }
}
struct Fixture {
    apps: ApplicationStore,
    identity: IdentityStore,
    control: ControlStore,
    groups: GroupStore,
    owner: sqlx::PgPool,
    admin: TokenDigest,
    admin_id: Uuid,
}
impl Fixture {
    async fn new() -> Self {
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let apps = ApplicationStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
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
            apps,
            identity,
            control,
            groups,
            owner,
            admin,
            admin_id: user.id,
        }
    }
    async fn user(&self) -> Uuid {
        self.identity
            .register(&name(), &password())
            .await
            .unwrap()
            .id
    }
    async fn session(&self, user: Uuid, client: ClientType) -> TokenDigest {
        let rev: i64 =
            sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
                .bind(user)
                .fetch_one(&self.owner)
                .await
                .unwrap();
        let key = token();
        self.identity
            .issue_session(user, rev, &key, client, Duration::from_secs(3600))
            .await
            .unwrap();
        key
    }
    async fn group(&self, user: Uuid) -> Uuid {
        let group = self
            .groups
            .create(&self.admin, &Uuid::new_v4().to_string(), "")
            .await
            .unwrap();
        self.groups
            .replace_members(&self.admin, group.id, 1, &[user])
            .await
            .unwrap();
        group.id
    }
    async fn events(&self, app: Uuid) -> i64 {
        sqlx::query_scalar("SELECT count(*) FROM pixels.application_events WHERE application_id=$1")
            .bind(app)
            .fetch_one(&self.owner)
            .await
            .unwrap()
    }
    async fn drain(&self) {
        for _ in 0..100 {
            let events = self.apps.claim_events(100).await.unwrap();
            if events.is_empty() {
                return;
            }
            for event in events {
                self.apps
                    .complete_event(event.id, event.lease_id)
                    .await
                    .unwrap();
            }
        }
        panic!("unexpected fixture volume");
    }
    async fn close(self) {
        self.apps.close().await;
        self.identity.close().await;
        self.control.close().await;
        self.groups.close().await;
        self.owner.close().await;
    }
}

#[tokio::test]
async fn all_three_modes_roundtrip_exact_launch_fields_and_only_return_safe_cards() {
    let f = Fixture::new().await;
    let user = f.user().await;
    let current = f.session(user, ClientType::Android).await;
    let mut game = spec();
    game.access = ApplicationAccess::Public;
    let mut web = game.clone();
    web.launch = ApplicationLaunch::Webview {
        entry_url: "https://example.test/?private=configuration#app".into(),
        video: VideoSpec {
            codec: VideoCodec::H265,
            bitrate_kbps: 128,
        },
    };
    let mut rdp = game.clone();
    rdp.launch = ApplicationLaunch::Rdp;
    rdp.allow_observer = false;
    rdp.allow_takeover = false;
    for definition in [game, web, rdp] {
        let app = f.apps.create(&f.admin, &definition).await.unwrap();
        assert_eq!(app.spec, definition);
        let card = f
            .apps
            .get_visible(&current, ClientType::Android, app.id)
            .await
            .unwrap();
        let debug = format!("{card:?}");
        assert!(!debug.contains("private="));
        assert!(!debug.contains("启动器.exe"));
        assert!(!debug.contains("--name"));
        assert_eq!(card.id, app.id);
        assert_eq!(card.access_revision, 1);
        assert_eq!(
            f.apps
                .update(&f.admin, app.id, 1, &definition)
                .await
                .unwrap(),
            app
        );
        assert_eq!(f.events(app.id).await, 1);
        let mut after = None;
        let mut found = false;
        for _ in 0..100 {
            let page = f.apps.list_managed(&f.admin, after, 100).await.unwrap();
            if page.contains(&app) {
                found = true;
                break;
            }
            let Some(last) = page.last() else {
                break;
            };
            after = Some(last.id);
        }
        assert!(
            found,
            "new application was absent from every management page"
        );
    }
    f.close().await;
}

#[tokio::test]
async fn acl_directory_and_direct_access_agree_and_group_revocation_cannot_be_bypassed() {
    let f = Fixture::new().await;
    let app = f.apps.create(&f.admin, &spec()).await.unwrap();
    let user = f.user().await;
    let other = f.user().await;
    let group = f.group(user).await;
    let old = f.session(user, ClientType::Android).await;
    let other_key = f.session(other, ClientType::Android).await;
    assert_eq!(
        f.apps.get_visible(&old, ClientType::Android, app.id).await,
        Err(StoreError::Rejected)
    );
    let app = f
        .apps
        .replace_groups(&f.admin, app.id, 1, &[group])
        .await
        .unwrap();
    assert_eq!(app.access_revision, 2);
    assert_eq!(
        f.apps.get_visible(&old, ClientType::Android, app.id).await,
        Err(StoreError::Rejected)
    );
    let current = f.session(user, ClientType::Android).await;
    let card = f
        .apps
        .get_visible(&current, ClientType::Android, app.id)
        .await
        .unwrap();
    assert!(f
        .apps
        .list_visible(&current, ClientType::Android, None, 100)
        .await
        .unwrap()
        .contains(&card));
    assert_eq!(
        f.apps
            .get_visible(&current, ClientType::Panel, app.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.apps
            .get_visible(&other_key, ClientType::Android, app.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.apps
            .replace_groups(&f.admin, app.id, 2, &[group])
            .await
            .unwrap()
            .revision,
        2
    );
    f.groups.delete(&f.admin, group, 2).await.unwrap();
    let new_session = f.session(user, ClientType::Android).await;
    assert_eq!(
        f.apps
            .get_visible(&new_session, ClientType::Android, app.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(f.apps.groups(&f.admin, app.id).await.unwrap(), vec![group]);
    f.close().await;
}

#[tokio::test]
async fn public_acl_disable_and_configuration_versions_are_distinct() {
    let f = Fixture::new().await;
    let mut input = spec();
    input.access = ApplicationAccess::Public;
    let app = f.apps.create(&f.admin, &input).await.unwrap();
    let user = f.user().await;
    let current = f.session(user, ClientType::Panel).await;
    input.name = "renamed".into();
    let app = f.apps.update(&f.admin, app.id, 1, &input).await.unwrap();
    assert_eq!((app.revision, app.access_revision), (2, 1));
    assert!(f
        .apps
        .get_visible(&current, ClientType::Panel, app.id)
        .await
        .is_ok());
    input.access = ApplicationAccess::Acl;
    let app = f.apps.update(&f.admin, app.id, 2, &input).await.unwrap();
    assert_eq!((app.revision, app.access_revision), (3, 2));
    assert_eq!(
        f.apps
            .get_visible(&current, ClientType::Panel, app.id)
            .await,
        Err(StoreError::Rejected)
    );
    input.access = ApplicationAccess::Public;
    input.disabled = true;
    let app = f.apps.update(&f.admin, app.id, 3, &input).await.unwrap();
    assert_eq!(
        f.apps
            .get_visible(&current, ClientType::Panel, app.id)
            .await,
        Err(StoreError::Rejected)
    );
    input.disabled = false;
    let app = f
        .apps
        .update(&f.admin, app.id, app.revision, &input)
        .await
        .unwrap();
    assert_eq!(app.access_revision, 4);
    assert!(f
        .apps
        .get_visible(&current, ClientType::Panel, app.id)
        .await
        .is_ok());
    // Access is rechecked against current policy; an old connection descriptor must use its
    // old access_revision and is not revalidated merely because the application reopens.
    assert_eq!(f.events(app.id).await, 5);
    f.close().await;
}

#[tokio::test]
async fn roles_and_invalid_models_have_no_hidden_persistent_effect() {
    let f = Fixture::new().await;
    let app = f.apps.create(&f.admin, &spec()).await.unwrap();
    let viewer = f
        .control
        .create_user(&f.admin, &name(), &password(), Role::Viewer)
        .await
        .unwrap();
    let view = f.session(viewer.id, ClientType::AdminWeb).await;
    let view_panel = f.session(viewer.id, ClientType::Panel).await;
    let admin_panel = f.session(f.admin_id, ClientType::Panel).await;
    assert!(f.apps.list_managed(&view, None, 100).await.is_ok());
    for denied in [&view, &admin_panel, &token()] {
        assert_eq!(
            f.apps.create(denied, &spec()).await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            f.apps.update(denied, app.id, 1, &spec()).await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            f.apps.replace_groups(denied, app.id, 1, &[]).await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            f.apps.delete(denied, app.id, 1).await,
            Err(StoreError::Rejected)
        );
    }
    assert_eq!(
        f.apps
            .list_visible(&view_panel, ClientType::Panel, None, 100)
            .await,
        Err(StoreError::Rejected)
    );
    let mut invalid = spec();
    invalid.launch = ApplicationLaunch::Rdp;
    assert_eq!(
        f.apps.create(&f.admin, &invalid).await,
        Err(StoreError::InvalidInput)
    );
    invalid.allow_observer = false;
    invalid.allow_takeover = false;
    assert_eq!(
        f.apps.update(&f.admin, app.id, 1, &invalid).await,
        Err(StoreError::Rejected)
    );
    assert!(f
        .apps
        .replace_groups(&f.admin, app.id, 1, &[Uuid::new_v4()])
        .await
        .is_err());
    assert_eq!(f.events(app.id).await, 1);
    let malformed=sqlx::query("INSERT INTO pixels.applications(id,name,kind,access_mode,allow_observer,allow_takeover,disabled) VALUES($1,'invalid','game_hook','acl',false,false,false)").bind(Uuid::new_v4()).execute(&f.owner).await;
    assert!(malformed.is_err());
    f.close().await;
}

#[tokio::test]
async fn event_failure_rolls_back_application_groups_user_revisions_and_outbox() {
    let f = Fixture::new().await;
    let app = f.apps.create(&f.admin, &spec()).await.unwrap();
    let user = f.user().await;
    let group = f.group(user).await;
    let current = f.session(user, ClientType::Android).await;
    let before: (i64,i64)=sqlx::query_as("SELECT authorization_revision,(SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1) FROM pixels.users WHERE id=$1").bind(user).fetch_one(&f.owner).await.unwrap();
    sqlx::query("REVOKE INSERT ON pixels.application_events FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let failed = f.apps.replace_groups(&f.admin, app.id, 1, &[group]).await;
    let mut input = spec();
    input.access = ApplicationAccess::Public;
    let failed_update = f.apps.update(&f.admin, app.id, 1, &input).await;
    sqlx::query("GRANT INSERT ON pixels.application_events TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    assert!(failed_update.is_err());
    assert!(f.apps.groups(&f.admin, app.id).await.unwrap().is_empty());
    let after: (i64,i64)=sqlx::query_as("SELECT authorization_revision,(SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1) FROM pixels.users WHERE id=$1").bind(user).fetch_one(&f.owner).await.unwrap();
    assert_eq!(before, after);
    assert_eq!(f.events(app.id).await, 1);
    assert!(f
        .identity
        .authenticate(&current, ClientType::Android)
        .await
        .is_ok());
    assert_eq!(
        f.apps
            .get_visible(&current, ClientType::Android, app.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.apps
            .replace_groups(&f.admin, app.id, 1, &[group])
            .await
            .unwrap()
            .revision,
        2
    );
    f.close().await;
}

#[tokio::test]
async fn concurrent_configuration_and_grant_updates_have_one_cas_winner() {
    let f = Fixture::new().await;
    let app = f.apps.create(&f.admin, &spec()).await.unwrap();
    let user = f.user().await;
    let group = f.group(user).await;
    let mut tasks = Vec::new();
    for index in 0..20 {
        let apps = f.apps.clone();
        let admin = f.admin.clone();
        tasks.push(tokio::spawn(async move {
            if index % 2 == 0 {
                let mut input = spec();
                input.name = format!("winner {index}");
                apps.update(&admin, app.id, 1, &input).await
            } else {
                apps.replace_groups(&admin, app.id, 1, &[group]).await
            }
        }));
    }
    let mut winners = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(app) => {
                winners += 1;
                assert_eq!(app.revision, 2);
            }
            Err(error) => assert_eq!(error, StoreError::Rejected),
        }
    }
    assert_eq!(winners, 1);
    assert_eq!(f.events(app.id).await, 2);
    f.close().await;
}

#[tokio::test]
async fn application_event_claims_reject_late_ack_and_preserve_immutable_audit() {
    let f = Fixture::new().await;
    f.drain().await;
    for _ in 0..20 {
        f.apps.create(&f.admin, &spec()).await.unwrap();
    }
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let apps = f.apps.clone();
        tasks.push(tokio::spawn(
            async move { apps.claim_events(1).await.unwrap() },
        ));
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
        f.apps
            .complete_event(completed.id, completed.lease_id)
            .await
            .unwrap();
    }
    sqlx::query("UPDATE pixels.application_events SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1").bind(event.id).execute(&f.owner).await.unwrap();
    assert_eq!(
        f.apps.complete_event(event.id, event.lease_id).await,
        Err(StoreError::Rejected)
    );
    let again = f.apps.claim_events(1).await.unwrap().pop().unwrap();
    assert_eq!(again.id, event.id);
    assert_ne!(again.lease_id, event.lease_id);
    assert_eq!(again.attempts, 2);
    assert_eq!(
        f.apps.complete_event(event.id, event.lease_id).await,
        Err(StoreError::Rejected)
    );
    f.apps
        .retry_event(again.id, again.lease_id, 30, DeliveryFailure::Unavailable)
        .await
        .unwrap();
    assert!(f.apps.claim_events(1).await.unwrap().is_empty());
    sqlx::query("UPDATE pixels.application_events SET available_at=clock_timestamp()-interval '1 second' WHERE id=$1").bind(event.id).execute(&f.owner).await.unwrap();
    let final_event = f.apps.claim_events(1).await.unwrap().pop().unwrap();
    assert_eq!(final_event.attempts, 3);
    f.apps
        .complete_event(final_event.id, final_event.lease_id)
        .await
        .unwrap();
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(
        sqlx::query("UPDATE pixels.application_events SET kind='deleted' WHERE id=$1")
            .bind(event.id)
            .execute(&runtime)
            .await
            .is_err()
    );
    assert!(
        sqlx::query("DELETE FROM pixels.application_events WHERE id=$1")
            .bind(event.id)
            .execute(&runtime)
            .await
            .is_err()
    );
    assert_eq!(f.events(event.application_id).await, 1);
    runtime.close().await;
    f.close().await;
}

#[tokio::test]
async fn deletion_is_soft_and_pagination_reconnect_and_outage_keep_database_authority() {
    let f = Fixture::new().await;
    let user = f.user().await;
    let group = f.group(user).await;
    let mut expected = BTreeSet::new();
    for _ in 0..5 {
        let app = f.apps.create(&f.admin, &spec()).await.unwrap();
        f.apps
            .replace_groups(&f.admin, app.id, 1, &[group])
            .await
            .unwrap();
        expected.insert(app.id);
    }
    let current = f.session(user, ClientType::Panel).await;
    let mut after = None;
    let mut seen = BTreeSet::new();
    for _ in 0..4 {
        let page = f
            .apps
            .list_visible(&current, ClientType::Panel, after, 2)
            .await
            .unwrap();
        if page.is_empty() {
            break;
        }
        after = page.last().map(|app| app.id);
        for app in page {
            if expected.contains(&app.id) {
                assert!(seen.insert(app.id));
            }
        }
    }
    // Other tests have public applications in this shared DB. Walk the complete keyset instead of
    // assuming only this test's rows exist, and still require all private rows exactly once.
    while after.is_some() {
        let page = f
            .apps
            .list_visible(&current, ClientType::Panel, after, 100)
            .await
            .unwrap();
        after = page.last().map(|app| app.id);
        for app in page {
            if expected.contains(&app.id) {
                assert!(seen.insert(app.id));
            }
        }
    }
    assert_eq!(seen, expected);
    let id = *expected.first().unwrap();
    f.apps.delete(&f.admin, id, 2).await.unwrap();
    assert_eq!(
        f.apps.get_visible(&current, ClientType::Panel, id).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.apps.update(&f.admin, id, 3, &spec()).await,
        Err(StoreError::Rejected)
    );
    let retained: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.group_app_grants WHERE application_id=$1")
            .bind(id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(retained, 1);
    let reopened = ApplicationStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap();
    assert_eq!(
        reopened.get_visible(&current, ClientType::Panel, id).await,
        Err(StoreError::Rejected)
    );
    reopened.close().await;
    f.apps.close().await;
    assert!(f.apps.create(&f.admin, &spec()).await.is_err());
    f.close().await;
}
