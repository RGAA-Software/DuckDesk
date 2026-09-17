use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ClientType, ControlStore, DeviceAccess, DevicePlatform, DeviceProfile, DeviceStore, GroupStore,
    IdentityStore, PasswordDigest, Role, StoreError, TokenDigest, Username,
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
    let f = Fixture::new().await;
    let key = token();
    let device = f
        .devices
        .create(&f.admin, "办公室 一号", DevicePlatform::Windows, &key)
        .await
        .unwrap();
    assert_eq!(device.public_code.len(), 12);
    assert!(device.public_code.bytes().all(|byte| byte.is_ascii_digit()));
    assert_ne!(device.id.to_string(), device.public_code);
    assert_eq!(
        f.devices.authenticate_enrollment(&key).await.unwrap().id,
        device.id
    );
    assert_eq!(
        f.devices.authenticate_enrollment(&f.admin).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.devices.list_managed(&key, None, 1).await,
        Err(StoreError::Rejected)
    );
    assert!(!format!("{device:?}").contains("hash"));
    assert!(f
        .devices
        .create(&f.admin, "duplicate", DevicePlatform::Linux, &key)
        .await
        .is_err());
    for value in ["", " ", "bad\nname", "trailing "] {
        assert_eq!(
            f.devices
                .create(&f.admin, value, DevicePlatform::Windows, &token())
                .await,
            Err(StoreError::InvalidInput)
        );
    }
    assert_eq!(f.audits(device.id).await, 1);
    let secret_columns: i64=sqlx::query_scalar("SELECT count(*) FROM information_schema.columns WHERE table_schema='pixels' AND table_name='devices' AND (column_name LIKE '%password%' OR column_name LIKE '%ciphertext%' OR column_name='seed')").fetch_one(&f.owner).await.unwrap();
    assert_eq!(secret_columns, 0);
    f.close().await;
}

#[tokio::test]
async fn role_client_type_and_resource_identity_are_not_interchangeable() {
    let f = Fixture::new().await;
    let device = f.device().await;
    let viewer = f
        .control
        .create_user(&f.admin, &name(), &password(), Role::Viewer)
        .await
        .unwrap();
    let viewer_web = f.session(viewer.id, ClientType::AdminWeb).await;
    let viewer_panel = f.session(viewer.id, ClientType::Panel).await;
    let user = f.user().await;
    let user_web = f.session(user, ClientType::AdminWeb).await;
    let admin_panel = f.session(f.admin_id, ClientType::Panel).await;
    assert!(f.devices.list_managed(&viewer_web, None, 100).await.is_ok());
    for denied in [
        &viewer_web,
        &viewer_panel,
        &user_web,
        &admin_panel,
        &token(),
    ] {
        assert_eq!(
            f.devices
                .create(denied, "no", DevicePlatform::Windows, &token())
                .await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            f.devices.update(denied, device.id, 1, "no", true).await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            f.devices
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
            f.devices.delete(denied, device.id, 1).await,
            Err(StoreError::Rejected)
        );
        assert_eq!(
            f.devices.rotate_key(denied, device.id, 1, &token()).await,
            Err(StoreError::Rejected)
        );
    }
    assert_eq!(
        f.devices
            .list_visible(&viewer_panel, ClientType::Panel, None, 100)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.devices
            .list_visible(&f.admin, ClientType::AdminWeb, None, 100)
            .await,
        Err(StoreError::Rejected)
    );
    assert!(f
        .devices
        .list_visible(&admin_panel, ClientType::Panel, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert_eq!(f.events(user).await, 0);
    assert_eq!(f.audits(device.id).await, 1);
    f.close().await;
}

#[tokio::test]
async fn direct_and_group_access_share_policy_and_revoke_only_changed_effective_users() {
    let f = Fixture::new().await;
    let device = f.device().await;
    let direct = f.user().await;
    let grouped = f.user().await;
    let outsider = f.user().await;
    let group = f
        .groups
        .create(&f.admin, &Uuid::new_v4().to_string(), "")
        .await
        .unwrap();
    f.groups
        .replace_members(&f.admin, group.id, 1, &[grouped])
        .await
        .unwrap();
    let direct_old = f.session(direct, ClientType::Android).await;
    let old = f.session(grouped, ClientType::Panel).await;
    let stranger = f.session(outsider, ClientType::Android).await;
    let access = DeviceAccess {
        users: vec![direct],
        groups: vec![group.id],
    };
    let device = f
        .devices
        .replace_access(&f.admin, device.id, 1, &access)
        .await
        .unwrap();
    assert_eq!(device.revision, 2);
    assert_eq!(
        f.devices
            .get_visible(&direct_old, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.devices
            .get_visible(&old, ClientType::Panel, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    let direct_key = f.session(direct, ClientType::Android).await;
    let grouped_key = f.session(grouped, ClientType::Panel).await;
    assert_eq!(
        f.devices
            .get_visible(&direct_key, ClientType::Android, device.id)
            .await
            .unwrap()
            .id,
        device.id
    );
    assert_eq!(
        f.devices
            .get_visible(&direct_key, ClientType::Panel, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.devices
            .list_visible(&grouped_key, ClientType::Panel, None, 100)
            .await
            .unwrap(),
        vec![device.clone()]
    );
    assert_eq!(
        f.devices
            .get_visible(&stranger, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.devices
            .replace_access(&f.admin, device.id, 2, &access)
            .await
            .unwrap()
            .revision,
        2
    );
    let counts = (f.events(direct).await, f.events(grouped).await);
    // Changing how an unchanged effective permission is represented does not revoke that user.
    let device = f
        .devices
        .replace_access(
            &f.admin,
            device.id,
            2,
            &DeviceAccess {
                users: vec![direct, grouped],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    assert_eq!((f.events(direct).await, f.events(grouped).await), counts);
    assert!(f
        .devices
        .get_visible(&grouped_key, ClientType::Panel, device.id)
        .await
        .is_ok());
    f.devices
        .replace_access(
            &f.admin,
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
        f.devices
            .get_visible(&grouped_key, ClientType::Panel, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    let fresh = f.session(grouped, ClientType::Panel).await;
    assert!(f
        .devices
        .list_visible(&fresh, ClientType::Panel, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert!(f
        .devices
        .get_visible(&direct_key, ClientType::Android, device.id)
        .await
        .is_ok());
    assert_eq!(f.events(outsider).await, 0);
    f.close().await;
}

#[tokio::test]
async fn invalid_grants_and_mid_transaction_failure_preserve_all_previous_state() {
    let f = Fixture::new().await;
    let device = f.device().await;
    let user = f.user().await;
    let next = f.user().await;
    let device = f
        .devices
        .replace_access(
            &f.admin,
            device.id,
            1,
            &DeviceAccess {
                users: vec![user],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    let original = f.devices.access(&f.admin, device.id).await.unwrap();
    let before = f.events(user).await;
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
        assert!(f
            .devices
            .replace_access(&f.admin, device.id, 2, &access)
            .await
            .is_err());
        assert_eq!(
            f.devices.access(&f.admin, device.id).await.unwrap(),
            original
        );
    }
    sqlx::query("REVOKE INSERT ON pixels.authorization_outbox FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let failed = f
        .devices
        .replace_access(
            &f.admin,
            device.id,
            2,
            &DeviceAccess {
                users: vec![next],
                groups: vec![],
            },
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.authorization_outbox TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    assert_eq!(
        f.devices.access(&f.admin, device.id).await.unwrap(),
        original
    );
    assert_eq!(f.events(user).await, before);
    assert_eq!(f.events(next).await, 0);
    assert_eq!(f.audits(device.id).await, 2);
    let result = f
        .devices
        .replace_access(
            &f.admin,
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
    assert_eq!(f.events(user).await, before + 1);
    assert_eq!(f.events(next).await, 1);
    f.close().await;
}

#[tokio::test]
async fn twenty_competing_writes_have_one_revision_winner_and_one_audit() {
    let f = Fixture::new().await;
    let device = f.device().await;
    let mut tasks = Vec::new();
    for index in 0..20 {
        let store = f.devices.clone();
        let admin = f.admin.clone();
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
    assert_eq!(f.audits(device.id).await, 2);
    assert_eq!(
        f.devices.delete(&f.admin, device.id, 1).await,
        Err(StoreError::Rejected)
    );
    f.close().await;
}

#[tokio::test]
async fn disable_key_rotation_and_delete_never_resurrect_credentials_or_remove_relationships() {
    let f = Fixture::new().await;
    let key = token();
    let next = token();
    let device = f
        .devices
        .create(&f.admin, "Lifecycle", DevicePlatform::Windows, &key)
        .await
        .unwrap();
    let user = f.user().await;
    f.devices
        .replace_access(
            &f.admin,
            device.id,
            1,
            &DeviceAccess {
                users: vec![user],
                groups: vec![],
            },
        )
        .await
        .unwrap();
    let user_key = f.session(user, ClientType::Android).await;
    f.devices
        .update(&f.admin, device.id, 2, "Lifecycle", true)
        .await
        .unwrap();
    assert_eq!(
        f.devices.authenticate_enrollment(&key).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.devices
            .get_visible(&user_key, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    f.devices
        .update(&f.admin, device.id, 3, "Lifecycle", false)
        .await
        .unwrap();
    assert!(f.devices.authenticate_enrollment(&key).await.is_ok());
    assert_eq!(
        f.devices
            .get_visible(&user_key, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    let current = f.session(user, ClientType::Android).await;
    f.devices
        .rotate_key(&f.admin, device.id, 4, &next)
        .await
        .unwrap();
    assert_eq!(
        f.devices.authenticate_enrollment(&key).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.devices
            .authenticate_enrollment(&next)
            .await
            .unwrap()
            .revision,
        5
    );
    assert_eq!(
        f.devices
            .get_visible(&current, ClientType::Android, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    f.devices.delete(&f.admin, device.id, 5).await.unwrap();
    assert_eq!(
        f.devices.authenticate_enrollment(&next).await,
        Err(StoreError::Rejected)
    );
    assert_eq!(
        f.devices
            .update(&f.admin, device.id, 6, "revive", false)
            .await,
        Err(StoreError::Rejected)
    );
    let retained: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels.user_devices WHERE device_id=$1")
            .bind(device.id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(retained, 1);
    assert_eq!(f.audits(device.id).await, 6);
    f.close().await;
}

#[tokio::test]
async fn group_deletion_revokes_device_access_even_though_resource_grants_remain_for_audit() {
    let f = Fixture::new().await;
    let device = f.device().await;
    let user = f.user().await;
    let group = f
        .groups
        .create(&f.admin, &Uuid::new_v4().to_string(), "")
        .await
        .unwrap();
    f.groups
        .replace_members(&f.admin, group.id, 1, &[user])
        .await
        .unwrap();
    f.devices
        .replace_access(
            &f.admin,
            device.id,
            1,
            &DeviceAccess {
                users: vec![],
                groups: vec![group.id],
            },
        )
        .await
        .unwrap();
    let current = f.session(user, ClientType::Panel).await;
    assert!(f
        .devices
        .get_visible(&current, ClientType::Panel, device.id)
        .await
        .is_ok());
    f.groups.delete(&f.admin, group.id, 2).await.unwrap();
    assert_eq!(
        f.devices
            .get_visible(&current, ClientType::Panel, device.id)
            .await,
        Err(StoreError::Rejected)
    );
    let fresh = f.session(user, ClientType::Panel).await;
    assert!(f
        .devices
        .list_visible(&fresh, ClientType::Panel, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert_eq!(
        f.devices.access(&f.admin, device.id).await.unwrap().groups,
        vec![group.id]
    );
    f.close().await;
}

#[tokio::test]
async fn pagination_expiry_and_database_outage_are_bounded_and_fail_closed() {
    let f = Fixture::new().await;
    let user = f.user().await;
    let mut expected = BTreeSet::new();
    for _ in 0..5 {
        let device = f.device().await;
        f.devices
            .replace_access(
                &f.admin,
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
    let current = f.session(user, ClientType::Android).await;
    let mut seen = Vec::new();
    let mut after = None;
    for _ in 0..4 {
        let page = f
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
        f.devices
            .list_visible(&current, ClientType::Android, None, 101)
            .await,
        Err(StoreError::InvalidInput)
    );
    sqlx::query("UPDATE pixels.login_sessions SET expires_at=clock_timestamp()-interval '1 second',created_at=clock_timestamp()-interval '1 hour' WHERE user_id=$1").bind(user).execute(&f.owner).await.unwrap();
    assert_eq!(
        f.devices
            .list_visible(&current, ClientType::Android, None, 100)
            .await,
        Err(StoreError::Rejected)
    );
    f.devices.close().await;
    assert!(f.devices.list_managed(&f.admin, None, 100).await.is_err());
    assert!(f
        .devices
        .create(&f.admin, "offline", DevicePlatform::Windows, &token())
        .await
        .is_err());
    f.close().await;
}
