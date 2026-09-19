use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ClientType, ControlStore, DeviceAccess, DevicePlatform, DeviceProfile, DeviceStore, GroupStore,
    IdentityStore, PasswordDigest, Role, RuntimeEntitlement, StoreError, TokenDigest, Username,
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

#[tokio::test]
async fn license_device_quota_serializes_the_last_available_slot() {
    let fixture = Fixture::new().await;
    let first_store = fixture.devices.clone();
    let second_store = fixture.devices.clone();
    let first_admin = fixture.admin.clone();
    let second_admin = fixture.admin.clone();
    let active_before: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.devices WHERE deleted_at IS NULL")
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    let final_slot = u32::try_from(active_before + 1).unwrap();
    let entitlement = RuntimeEntitlement::new(final_slot, 8, true, true, true).unwrap();
    let first = tokio::spawn(async move {
        first_store
            .create_with_entitlement(
                &first_admin,
                "quota-a",
                DevicePlatform::Windows,
                &token(),
                entitlement,
            )
            .await
    });
    let second = tokio::spawn(async move {
        second_store
            .create_with_entitlement(
                &second_admin,
                "quota-b",
                DevicePlatform::Linux,
                &token(),
                entitlement,
            )
            .await
    });
    let outcomes = [first.await.unwrap(), second.await.unwrap()];
    assert_eq!(outcomes.iter().filter(|outcome| outcome.is_ok()).count(), 1);
    assert_eq!(
        outcomes
            .iter()
            .filter(|outcome| **outcome == Err(StoreError::LicenseRestriction))
            .count(),
        1
    );
    let active: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.devices WHERE deleted_at IS NULL")
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(active, active_before + 1);
    fixture.close().await;
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
                    &SaltString::encode_b64(&[64; 16]).unwrap(),
                )
                .unwrap()
                .to_string()
        })
        .clone(),
    )
    .unwrap()
}
struct Fixture {
    devices: DeviceStore,
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
        let devices = DeviceStore::connect(&config("RUNTIME"), deployment)
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
            devices,
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
        let revision: i64 =
            sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
                .bind(user)
                .fetch_one(&self.owner)
                .await
                .unwrap();
        let key = token();
        self.identity
            .issue_session(user, revision, &key, client, Duration::from_secs(3600))
            .await
            .unwrap();
        key
    }
    async fn device(&self) -> DeviceProfile {
        self.devices
            .create(&self.admin, "云端 设备", DevicePlatform::Windows, &token())
            .await
            .unwrap()
    }
    async fn events(&self, user: Uuid) -> i64 {
        sqlx::query_scalar("SELECT count(*) FROM pixels.authorization_outbox WHERE user_id=$1")
            .bind(user)
            .fetch_one(&self.owner)
            .await
            .unwrap()
    }
    async fn audits(&self, device: Uuid) -> i64 {
        sqlx::query_scalar("SELECT count(*) FROM pixels.device_audit WHERE device_id=$1")
            .bind(device)
            .fetch_one(&self.owner)
            .await
            .unwrap()
    }
    async fn close(self) {
        self.devices.close().await;
        self.identity.close().await;
        self.control.close().await;
        self.groups.close().await;
        self.owner.close().await;
    }
}

#[tokio::test]
async fn directory_identity_is_separate_from_enrollment_and_no_secrets_are_returned() {
    let fixture = Fixture::new().await;
    let key = token();
    let device = fixture
        .devices
        .create(&fixture.admin, "办公室 一号", DevicePlatform::Windows, &key)
        .await
        .unwrap();
    assert_eq!(device.public_code.len(), 12);
    assert!(device.public_code.bytes().all(|byte| byte.is_ascii_digit()));
    assert_ne!(device.id.to_string(), device.public_code);
    assert_eq!(
        fixture
            .devices
            .authenticate_enrollment(&key)
            .await
            .unwrap()
            .id,
        device.id
    );
    assert_eq!(
        fixture
            .devices
            .authenticate_enrollment(&fixture.admin)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        fixture.devices.list_managed(&key, None, 1).await,
        Err(StoreError::Rejected)
    );
    assert!(!format!("{device:?}").contains("hash"));
    assert!(fixture
        .devices
        .create(&fixture.admin, "duplicate", DevicePlatform::Linux, &key)
        .await
        .is_err());
    for value in ["", " ", "bad\nname", "trailing "] {
        assert_eq!(
            fixture
                .devices
                .create(&fixture.admin, value, DevicePlatform::Windows, &token())
                .await,
            Err(StoreError::InvalidInput)
        );
    }
    assert_eq!(fixture.audits(device.id).await, 1);
    let secret_columns: i64=sqlx::query_scalar("SELECT count(*) FROM information_schema.columns WHERE table_schema='pixels' AND table_name='devices' AND (column_name LIKE '%password%' OR column_name LIKE '%ciphertext%' OR column_name='seed')").fetch_one(&fixture.owner).await.unwrap();
    assert_eq!(secret_columns, 0);
    fixture.close().await;
}

#[tokio::test]
async fn role_client_type_and_resource_identity_are_not_interchangeable() {
    let fixture = Fixture::new().await;
    let device = fixture.device().await;
    let viewer = fixture
        .control
        .create_user(&fixture.admin, &name(), &password(), Role::Viewer)
        .await
        .unwrap();
    let viewer_web = fixture.session(viewer.id, ClientType::AdminWeb).await;
    let viewer_panel = fixture.session(viewer.id, ClientType::Panel).await;
    let user = fixture.user().await;
    let user_web = fixture.session(user, ClientType::AdminWeb).await;
    let admin_panel = fixture.session(fixture.admin_id, ClientType::Panel).await;
    assert!(fixture
        .devices
        .list_managed(&viewer_web, None, 100)
        .await
        .is_ok());
    for denied in [
        &viewer_web,
        &viewer_panel,
        &user_web,
        &admin_panel,
        &token(),
    ] {
        assert_eq!(
            fixture
                .devices
                .create(denied, "no", DevicePlatform::Windows, &token())
                .await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            fixture
                .devices
                .update(denied, device.id, 1, "no", true)
                .await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            fixture
                .devices
                .replace_access(
                    denied,
                    device.id,
                    1,
                    &DeviceAccess {
                        users: vec![user],
                        groups: vec![]
                    }
                )
                .await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            fixture.devices.delete(denied, device.id, 1).await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            fixture
                .devices
                .rotate_key(denied, device.id, 1, &token())
                .await,
            Err(StoreError::Rejected)
        );
    }
    assert_eq!(
        fixture
            .devices
            .list_visible(&viewer_panel, ClientType::Panel, None, 100)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        fixture
            .devices
            .list_visible(&fixture.admin, ClientType::AdminWeb, None, 100)
            .await,
        Err(StoreError::Rejected)
    );
    assert!(fixture
        .devices
        .list_visible(&admin_panel, ClientType::Panel, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert_eq!(fixture.events(user).await, 0);
    assert_eq!(fixture.audits(device.id).await, 1);
    fixture.close().await;
}

#[tokio::test]
async fn direct_and_group_access_share_policy_and_revoke_only_changed_effective_users() {
    let fixture = Fixture::new().await;
    let device = fixture.device().await;
    let direct = fixture.user().await;
    let grouped = fixture.user().await;
    let outsider = fixture.user().await;
    let group = fixture
        .groups
        .create(&fixture.admin, &Uuid::new_v4().to_string(), "")
        .await
        .unwrap();
    fixture
        .groups
        .replace_members(&fixture.admin, group.id, 1, &[grouped])
        .await
        .unwrap();
    let direct_old = fixture.session(direct, ClientType::Android).await;
    let old = fixture.session(grouped, ClientType::Panel).await;
    let stranger = fixture.session(outsider, ClientType::Android).await;
    let access = DeviceAccess {
        users: vec![direct],
        groups: vec![group.id],
    };
    let device = fixture
        .devices
        .replace_access(&fixture.admin, device.id, 1, &access)
        .await
        .unwrap();
    assert_eq!(device.revision, 2);
    assert_eq!(
        fixture
            .devices
            .get_visible(&direct_old, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        fixture
            .devices
            .get_visible(&old, ClientType::Panel, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    let direct_key = fixture.session(direct, ClientType::Android).await;
    let grouped_key = fixture.session(grouped, ClientType::Panel).await;
    assert_eq!(
        fixture
            .devices
            .get_visible(&direct_key, ClientType::Android, device.id)
            .await
            .unwrap()
            .id,
        device.id
    );
    assert_eq!(
        fixture
            .devices
            .get_visible(&direct_key, ClientType::Panel, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        fixture
            .devices
            .list_visible(&grouped_key, ClientType::Panel, None, 100)
            .await
            .unwrap(),
        vec![device.clone()]
    );
    assert_eq!(
        fixture
            .devices
            .get_visible(&stranger, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        fixture
            .devices
            .replace_access(&fixture.admin, device.id, 2, &access)
            .await
            .unwrap()
            .revision,
        2
    );
    let counts = (fixture.events(direct).await, fixture.events(grouped).await);
    // Changing how an unchanged effective permission is represented does not revoke that user.
    let device = fixture
        .devices
        .replace_access(
            &fixture.admin,
            device.id,
            2,
            &DeviceAccess {
                users: vec![direct, grouped],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    assert_eq!(
        (fixture.events(direct).await, fixture.events(grouped).await),
        counts
    );
    assert!(fixture
        .devices
        .get_visible(&grouped_key, ClientType::Panel, device.id)
        .await
        .is_ok());
    fixture
        .devices
        .replace_access(
            &fixture.admin,
            device.id,
            device.revision,
            &DeviceAccess {
                users: vec![direct],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    assert_eq!(
        fixture
            .devices
            .get_visible(&grouped_key, ClientType::Panel, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    let fresh = fixture.session(grouped, ClientType::Panel).await;
    assert!(fixture
        .devices
        .list_visible(&fresh, ClientType::Panel, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert!(fixture
        .devices
        .get_visible(&direct_key, ClientType::Android, device.id)
        .await
        .is_ok());
    assert_eq!(fixture.events(outsider).await, 0);
    fixture.close().await;
}

#[tokio::test]
async fn invalid_grants_and_mid_transaction_failure_preserve_all_previous_state() {
    let fixture = Fixture::new().await;
    let device = fixture.device().await;
    let user = fixture.user().await;
    let next = fixture.user().await;
    let device = fixture
        .devices
        .replace_access(
            &fixture.admin,
            device.id,
            1,
            &DeviceAccess {
                users: vec![user],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    let original = fixture
        .devices
        .access(&fixture.admin, device.id)
        .await
        .unwrap();
    let before = fixture.events(user).await;
    for access in [
        DeviceAccess {
            users: vec![next, next],
            groups: vec![],
        },
        DeviceAccess {
            users: vec![Uuid::new_v4()],
            groups: vec![],
        },
        DeviceAccess {
            users: vec![next],
            groups: vec![Uuid::new_v4()],
        },
    ] {
        assert!(fixture
            .devices
            .replace_access(&fixture.admin, device.id, 2, &access)
            .await
            .is_err());
        assert_eq!(
            fixture
                .devices
                .access(&fixture.admin, device.id)
                .await
                .unwrap(),
            original
        );
    }
    sqlx::query("REVOKE INSERT ON pixels.authorization_outbox FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let failed = fixture
        .devices
        .replace_access(
            &fixture.admin,
            device.id,
            2,
            &DeviceAccess {
                users: vec![next],
                groups: vec![],
            },
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.authorization_outbox TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    assert_eq!(
        fixture
            .devices
            .access(&fixture.admin, device.id)
            .await
            .unwrap(),
        original
    );
    assert_eq!(fixture.events(user).await, before);
    assert_eq!(fixture.events(next).await, 0);
    assert_eq!(fixture.audits(device.id).await, 2);
    let result = fixture
        .devices
        .replace_access(
            &fixture.admin,
            device.id,
            2,
            &DeviceAccess {
                users: vec![next],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    assert_eq!(result.revision, 3);
    assert_eq!(fixture.events(user).await, before + 1);
    assert_eq!(fixture.events(next).await, 1);
    fixture.close().await;
}

#[tokio::test]
async fn twenty_competing_writes_have_one_revision_winner_and_one_audit() {
    let fixture = Fixture::new().await;
    let device = fixture.device().await;
    let mut tasks = Vec::new();
    for index in 0..20 {
        let store = fixture.devices.clone();
        let admin = fixture.admin.clone();
        tasks.push(tokio::spawn(async move {
            store
                .update(&admin, device.id, 1, &format!("winner {index}"), false)
                .await
        }));
    }
    let mut winners = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(result) => {
                assert_eq!(result.revision, 2);
                winners += 1
            }
            Err(error) => assert_eq!(error, StoreError::Rejected),
        }
    }
    assert_eq!(winners, 1);
    assert_eq!(fixture.audits(device.id).await, 2);
    assert_eq!(
        fixture.devices.delete(&fixture.admin, device.id, 1).await,
        Err(StoreError::Rejected)
    );
    fixture.close().await;
}

#[tokio::test]
async fn disable_key_rotation_and_delete_never_resurrect_credentials_or_remove_relationships() {
    let fixture = Fixture::new().await;
    let key = token();
    let next = token();
    let device = fixture
        .devices
        .create(&fixture.admin, "Lifecycle", DevicePlatform::Windows, &key)
        .await
        .unwrap();
    let user = fixture.user().await;
    fixture
        .devices
        .replace_access(
            &fixture.admin,
            device.id,
            1,
            &DeviceAccess {
                users: vec![user],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    let user_key = fixture.session(user, ClientType::Android).await;
    fixture
        .devices
        .update(&fixture.admin, device.id, 2, "Lifecycle", true)
        .await
        .unwrap();
    assert_eq!(
        fixture.devices.authenticate_enrollment(&key).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        fixture
            .devices
            .get_visible(&user_key, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    fixture
        .devices
        .update(&fixture.admin, device.id, 3, "Lifecycle", false)
        .await
        .unwrap();
    assert!(fixture.devices.authenticate_enrollment(&key).await.is_ok());
    assert_eq!(
        fixture
            .devices
            .get_visible(&user_key, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    let current = fixture.session(user, ClientType::Android).await;
    fixture
        .devices
        .rotate_key(&fixture.admin, device.id, 4, &next)
        .await
        .unwrap();
    assert_eq!(
        fixture.devices.authenticate_enrollment(&key).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        fixture
            .devices
            .authenticate_enrollment(&next)
            .await
            .unwrap()
            .revision,
        5
    );
    assert_eq!(
        fixture
            .devices
            .get_visible(&current, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    fixture
        .devices
        .delete(&fixture.admin, device.id, 5)
        .await
        .unwrap();
    assert_eq!(
        fixture.devices.authenticate_enrollment(&next).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        fixture
            .devices
            .update(&fixture.admin, device.id, 6, "revive", false)
            .await,
        Err(StoreError::Rejected)
    );
    let retained: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.user_devices WHERE device_id=$1")
            .bind(device.id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(retained, 1);
    assert_eq!(fixture.audits(device.id).await, 6);
    fixture.close().await;
}

#[tokio::test]
async fn group_deletion_revokes_device_access_even_though_resource_grants_remain_for_audit() {
    let fixture = Fixture::new().await;
    let device = fixture.device().await;
    let user = fixture.user().await;
    let group = fixture
        .groups
        .create(&fixture.admin, &Uuid::new_v4().to_string(), "")
        .await
        .unwrap();
    fixture
        .groups
        .replace_members(&fixture.admin, group.id, 1, &[user])
        .await
        .unwrap();
    fixture
        .devices
        .replace_access(
            &fixture.admin,
            device.id,
            1,
            &DeviceAccess {
                users: vec![],
                groups: vec![group.id],
            },
        )
        .await
        .unwrap();
    let current = fixture.session(user, ClientType::Panel).await;
    assert!(fixture
        .devices
        .get_visible(&current, ClientType::Panel, device.id)
        .await
        .is_ok());
    fixture
        .groups
        .delete(&fixture.admin, group.id, 2)
        .await
        .unwrap();
    assert_eq!(
        fixture
            .devices
            .get_visible(&current, ClientType::Panel, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    let fresh = fixture.session(user, ClientType::Panel).await;
    assert!(fixture
        .devices
        .list_visible(&fresh, ClientType::Panel, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert_eq!(
        fixture
            .devices
            .access(&fixture.admin, device.id)
            .await
            .unwrap()
            .groups,
        vec![group.id]
    );
    fixture.close().await;
}

#[tokio::test]
async fn pagination_expiry_and_database_outage_are_bounded_and_fail_closed() {
    let fixture = Fixture::new().await;
    let user = fixture.user().await;
    let mut expected = BTreeSet::new();
    for _ in 0..5 {
        let device = fixture.device().await;
        fixture
            .devices
            .replace_access(
                &fixture.admin,
                device.id,
                1,
                &DeviceAccess {
                    users: vec![user],
                    groups: vec![],
                },
            )
            .await
            .unwrap();
        expected.insert(device.id);
    }
    let current = fixture.session(user, ClientType::Android).await;
    let mut seen = Vec::new();
    let mut after = None;
    for _ in 0..4 {
        let page = fixture
            .devices
            .list_visible(&current, ClientType::Android, after, 2)
            .await
            .unwrap();
        after = page.last().map(|device| device.id);
        seen.extend(page.iter().map(|device| device.id));
        if page.is_empty() {
            break;
        }
    }
    assert_eq!(seen, expected.into_iter().collect::<Vec<_>>());
    assert_eq!(
        fixture
            .devices
            .list_visible(&current, ClientType::Android, None, 101)
            .await,
        Err(StoreError::InvalidInput)
    );
    sqlx::query("UPDATE pixels.login_sessions SET expires_at=clock_timestamp()-interval '1 second',created_at=clock_timestamp()-interval '1 hour' WHERE user_id=$1").bind(user).execute(&fixture.owner).await.unwrap();
    assert_eq!(
        fixture
            .devices
            .list_visible(&current, ClientType::Android, None, 100)
            .await,
        Err(StoreError::Rejected)
    );
    fixture.devices.close().await;
    assert!(fixture
        .devices
        .list_managed(&fixture.admin, None, 100)
        .await
        .is_err());
    assert!(fixture
        .devices
        .create(&fixture.admin, "offline", DevicePlatform::Windows, &token())
        .await
        .is_err());
    fixture.close().await;
}
