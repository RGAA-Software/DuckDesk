use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ApplicationAccess, ApplicationDefinition, ApplicationLaunch, ApplicationSpec, ApplicationStore,
    ClientType, DeploymentConfiguration, DeploymentObservation, DeploymentProfile, DeploymentStore,
    DeploymentTarget, DevicePlatform, DeviceStore, IdentityStore, NodeConnection, NodeProduct,
    NodeReport, NodeStore, PasswordDigest, PreparationState, StoreError, TokenDigest, Username,
    VideoCodec, VideoSpec,
};
use px_console_store::{
    GuestStore, InstanceStore, NodeConfiguration, OriginFingerprint, ResourceCredential,
    ResourceOwner, StartApplication,
};
use px_pg::{DatabaseConfig, Transport};
use std::{env, sync::OnceLock, time::Duration};
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
fn password() -> PasswordDigest {
    static HASH: OnceLock<String> = OnceLock::new();
    PasswordDigest::parse(
        HASH.get_or_init(|| {
            Argon2::default()
                .hash_password(
                    b"synthetic-password",
                    &SaltString::encode_b64(&[66; 16]).unwrap(),
                )
                .unwrap()
                .to_string()
        })
        .clone(),
    )
    .unwrap()
}
fn settings(target: DeploymentTarget) -> DeploymentConfiguration {
    let capacity = if target == DeploymentTarget::Rdp {
        1
    } else {
        4
    };
    DeploymentConfiguration {
        target,
        capacity,
        gpu_key: None,
        disabled: false,
    }
}
fn node_report(sequence: u64) -> NodeReport {
    NodeReport {
        sequence,
        product_version_code: 1,
        public_host: "node.example.test".into(),
        desktop_port: 4601,
        application_port_start: 4613,
        application_port_end: 4998,
        game_hook: true,
        webview: true,
        rdp: true,
    }
}
fn observation(deployment: &DeploymentProfile, sequence: u64) -> DeploymentObservation {
    DeploymentObservation {
        deployment_revision: deployment.revision,
        application_revision: deployment.application_revision,
        endpoint_revision: 2,
        sequence,
        status: PreparationState::Ready,
    }
}
struct Fixture {
    instances: InstanceStore,
    guests: GuestStore,
    deployments: DeploymentStore,
    apps: ApplicationStore,
    nodes: NodeStore,
    devices: DeviceStore,
    identity: IdentityStore,
    owner: sqlx::PgPool,
    admin: TokenDigest,
}
impl Fixture {
    async fn new() -> Self {
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let mut f = Self {
            instances: InstanceStore::connect(&config("RUNTIME"), deployment)
                .await
                .unwrap(),
            guests: GuestStore::connect(&config("RUNTIME"), deployment)
                .await
                .unwrap(),
            deployments: DeploymentStore::connect(&config("RUNTIME"), deployment)
                .await
                .unwrap(),
            apps: ApplicationStore::connect(&config("RUNTIME"), deployment)
                .await
                .unwrap(),
            nodes: NodeStore::connect(&config("RUNTIME"), deployment)
                .await
                .unwrap(),
            devices: DeviceStore::connect(&config("RUNTIME"), deployment)
                .await
                .unwrap(),
            identity: IdentityStore::connect(&config("RUNTIME"), deployment)
                .await
                .unwrap(),
            owner: config("OWNER").connect().await.unwrap(),
            admin: token(),
        };
        f.admin = f.session("admin", ClientType::AdminWeb).await;
        f
    }
    async fn session(&self, role: &str, client: ClientType) -> TokenDigest {
        let user = self
            .identity
            .register(
                &Username::parse(&Uuid::new_v4().to_string()).unwrap(),
                &password(),
            )
            .await
            .unwrap();
        sqlx::query("UPDATE pixels.users SET role=$2 WHERE id=$1")
            .bind(user.id)
            .bind(role)
            .execute(&self.owner)
            .await
            .unwrap();
        let key = token();
        self.identity
            .issue_session(user.id, 1, &key, client, Duration::from_secs(3600))
            .await
            .unwrap();
        key
    }
    async fn node(&self) -> (Uuid, TokenDigest) {
        let device = self
            .devices
            .create(&self.admin, "部署节点", DevicePlatform::Windows, &token())
            .await
            .unwrap();
        let key = token();
        let node = self
            .nodes
            .create(&self.admin, device.id, NodeProduct::CloudNode, &key, 4)
            .await
            .unwrap();
        (node.id, key)
    }
    async fn app(&self, target: &DeploymentTarget) -> ApplicationDefinition {
        let video = VideoSpec {
            codec: VideoCodec::H264,
            bitrate_kbps: 8000,
        };
        let launch = match target {
            DeploymentTarget::GameHook { .. } => ApplicationLaunch::GameHook {
                executable_relative: r"子目录\Game.exe".into(),
                arguments: r#""含空格 参数""#.into(),
                video,
            },
            DeploymentTarget::Webview => ApplicationLaunch::Webview {
                entry_url: "https://example.test/".into(),
                video,
            },
            DeploymentTarget::Rdp => ApplicationLaunch::Rdp,
        };
        self.apps
            .create(
                &self.admin,
                &ApplicationSpec {
                    name: "应用".into(),
                    access: ApplicationAccess::Public,
                    launch,
                    allow_observer: false,
                    allow_takeover: false,
                    disabled: false,
                },
            )
            .await
            .unwrap()
    }
    async fn connected(&self) -> (NodeConnection, TokenDigest) {
        let (_, key) = self.node().await;
        let epoch = self.nodes.begin_runtime().await.unwrap();
        let connection = self
            .nodes
            .open_connection(epoch, &key, &token())
            .await
            .unwrap();
        self.nodes
            .report(&connection, &node_report(1))
            .await
            .unwrap();
        (connection, key)
    }
    async fn deployment(
        &self,
        node: Uuid,
        target: DeploymentTarget,
    ) -> (ApplicationDefinition, DeploymentProfile) {
        let app = self.app(&target).await;
        let deployment = self
            .deployments
            .create(&self.admin, app.id, node, &settings(target))
            .await
            .unwrap();
        (app, deployment)
    }
    async fn get(&self, id: Uuid) -> DeploymentProfile {
        let mut after = None;
        loop {
            let rows = self
                .deployments
                .list_managed(&self.admin, after, 100)
                .await
                .unwrap();
            assert!(!rows.is_empty());
            if let Some(row) = rows.iter().find(|row| row.id == id) {
                return row.clone();
            }
            after = rows.last().map(|row| row.id);
        }
    }
    async fn prepared(
        &self,
        target: DeploymentTarget,
        capacity: u32,
    ) -> (NodeConnection, ApplicationDefinition, DeploymentProfile) {
        let (connection, _) = self.connected().await;
        self.nodes
            .configure(
                &self.admin,
                connection.id(),
                1,
                NodeConfiguration {
                    draining: false,
                    disabled: false,
                    max_instances: capacity,
                },
            )
            .await
            .unwrap();
        let (app, deployment) = self.deployment(connection.id(), target.clone()).await;
        let mut configuration = settings(target);
        configuration.capacity = capacity;
        let deployment = self
            .deployments
            .configure(&self.admin, deployment.id, 1, &configuration)
            .await
            .unwrap();
        self.deployments
            .report(&connection, deployment.id, &observation(&deployment, 1))
            .await
            .unwrap();
        // Reservation-only fixture: this does NOT prove real inventory reconciliation.
        sqlx::query("UPDATE pixels.nodes SET state='ready' WHERE id=$1")
            .bind(connection.id())
            .execute(&self.owner)
            .await
            .unwrap();
        assert_eq!(self.get(deployment.id).await.observed_state, "ready");
        (connection, app, deployment)
    }
    async fn count(&self, node: Uuid) -> i64 {
        sqlx::query_scalar("SELECT count(*) FROM pixels.instances WHERE node_id=$1")
            .bind(node)
            .fetch_one(&self.owner)
            .await
            .unwrap()
    }
    async fn guest(&self) -> (TokenDigest, Uuid) {
        let mut source = [0; 32];
        source[..16].copy_from_slice(Uuid::new_v4().as_bytes());
        source[16..].copy_from_slice(Uuid::new_v4().as_bytes());
        let key = token();
        let guest = self
            .guests
            .issue(
                &OriginFingerprint::from_hmac_sha256(source),
                &key,
                ClientType::Android,
                Duration::from_secs(3600),
            )
            .await
            .unwrap();
        (key, guest.id)
    }
    async fn close(self) {
        self.instances.close().await;
        self.guests.close().await;
        self.deployments.close().await;
        self.apps.close().await;
        self.nodes.close().await;
        self.devices.close().await;
        self.identity.close().await;
        self.owner.close().await;
    }
}

fn request(app: Uuid) -> StartApplication {
    StartApplication {
        request_id: Uuid::new_v4(),
        application_id: app,
        deployment_id: None,
    }
}

struct Contender(std::process::Child);

#[tokio::test]
async fn origin_login_session_and_revision_are_persisted_with_owner_foreign_keys() {
    let f = Fixture::new().await;
    let (connection, app, _) = f.prepared(DeploymentTarget::Webview, 4).await;
    let user = f.session("user", ClientType::Android).await;
    let instance = f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let (owner, session, revision): (Uuid, Uuid, i64) = sqlx::query_as(
        "SELECT owner_user,login_session_id,owner_revision FROM pixels.instances WHERE id=$1",
    )
    .bind(instance.id)
    .fetch_one(&f.owner)
    .await
    .unwrap();
    let session_owner: Uuid =
        sqlx::query_scalar("SELECT user_id FROM pixels.login_sessions WHERE id=$1")
            .bind(session)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(owner, session_owner);
    assert_eq!(revision, 1);
    let unrelated: Uuid = sqlx::query_scalar(
        "SELECT id FROM pixels.login_sessions WHERE user_id<>$1 ORDER BY id LIMIT 1",
    )
    .bind(owner)
    .fetch_one(&f.owner)
    .await
    .unwrap();
    assert!(
        sqlx::query("UPDATE pixels.instances SET login_session_id=$2 WHERE id=$1")
            .bind(instance.id)
            .bind(unrelated)
            .execute(&f.owner)
            .await
            .is_err()
    );
    assert!(
        sqlx::query("UPDATE pixels.instances SET login_session_id=NULL WHERE id=$1")
            .bind(instance.id)
            .execute(&f.owner)
            .await
            .is_err()
    );
    let (guest, guest_id) = f.guest().await;
    let instance = f
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let stored: (Uuid, Option<Uuid>, i64) = sqlx::query_as(
        "SELECT owner_guest,login_session_id,owner_revision FROM pixels.instances WHERE id=$1",
    )
    .bind(instance.id)
    .fetch_one(&f.owner)
    .await
    .unwrap();
    assert_eq!(stored, (guest_id, None, 1));
    f.close().await;
}

#[tokio::test]
async fn disconnect_invalidation_is_atomic_cancels_commands_and_preserves_occupancy() {
    let f = Fixture::new().await;
    let (connection, app, deployment) = f.prepared(DeploymentTarget::Webview, 1).await;
    let user = f.session("user", ClientType::Android).await;
    let instance = f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.instance_events FROM pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    let outcome = f.nodes.close_connection(&connection).await;
    sqlx::query("GRANT INSERT ON pixels.instance_events TO pixels_console_runtime")
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(outcome.is_err());
    assert_eq!(
        f.instances
            .get(
                ResourceCredential::User(&user),
                ClientType::Android,
                instance.id
            )
            .await
            .unwrap(),
        instance
    );
    assert!(f.nodes.report(&connection, &node_report(2)).await.is_ok());
    f.nodes.close_connection(&connection).await.unwrap();
    let unknown = f
        .instances
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            instance.id,
        )
        .await
        .unwrap();
    assert_eq!(unknown.state, "reconcile_required");
    assert!(unknown.ended_at.is_none());
    let key = token();
    let revision: i64 = sqlx::query_scalar("SELECT revision FROM pixels.nodes WHERE id=$1")
        .bind(connection.id())
        .fetch_one(&f.owner)
        .await
        .unwrap();
    f.nodes
        .rotate_key(&f.admin, connection.id(), revision, &key)
        .await
        .unwrap();
    let new = f
        .nodes
        .open_connection(connection.epoch(), &key, &token())
        .await
        .unwrap();
    f.nodes.report(&new, &node_report(1)).await.unwrap();
    f.deployments
        .report(&new, deployment.id, &observation(&deployment, 1))
        .await
        .unwrap();
    // Even a synthetic Ready reset cannot free the unknown reservation.
    sqlx::query("UPDATE pixels.nodes SET state='ready' WHERE id=$1")
        .bind(connection.id())
        .execute(&f.owner)
        .await
        .unwrap();
    assert_eq!(
        f.instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &request(app.id)
            )
            .await
            .unwrap_err(),
        StoreError::NoCapacity
    );
    let pending: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.instance_commands WHERE instance_id=$1 AND state IN ('pending','claimed')")
        .bind(instance.id).fetch_one(&f.owner).await.unwrap();
    assert_eq!(pending, 0);
    f.close().await;
}
impl Contender {
    fn start(key: [u8; 32], request: &StartApplication) -> Self {
        use std::io::Write;
        let mut child = Self(
            std::process::Command::new(env!("CARGO_BIN_EXE_px_instance_contender"))
                .stdin(std::process::Stdio::piped())
                .stdout(std::process::Stdio::piped())
                .stderr(std::process::Stdio::piped())
                .spawn()
                .unwrap(),
        );
        let bytes =
            serde_json::to_vec(&serde_json::json!({"token_hash":key,"request":request})).unwrap();
        child.0.stdin.take().unwrap().write_all(&bytes).unwrap();
        child
    }
    async fn finish(&mut self) -> String {
        use std::io::Read;
        tokio::time::timeout(Duration::from_secs(10), async {
            loop {
                if let Some(status) = self.0.try_wait().unwrap() {
                    assert!(status.success(), "independent reservation process failed");
                    break;
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        })
        .await
        .expect("reservation child exceeded deadline");
        let mut text = String::new();
        self.0
            .stdout
            .take()
            .unwrap()
            .read_to_string(&mut text)
            .unwrap();
        text
    }
}
impl Drop for Contender {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

#[tokio::test]
async fn two_os_processes_block_on_the_same_gate_then_compete_for_the_last_slot() {
    let f = Fixture::new().await;
    let (connection, app, _) = f.prepared(DeploymentTarget::Webview, 1).await;
    let user = f
        .identity
        .register(
            &Username::parse(&Uuid::new_v4().to_string()).unwrap(),
            &password(),
        )
        .await
        .unwrap();
    let mut bytes = [0; 32];
    bytes[..16].copy_from_slice(Uuid::new_v4().as_bytes());
    bytes[16..].copy_from_slice(Uuid::new_v4().as_bytes());
    f.identity
        .issue_session(
            user.id,
            1,
            &TokenDigest::from_sha256(bytes),
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let mut barrier = f.owner.begin().await.unwrap();
    sqlx::query("SELECT pg_advisory_xact_lock(22091401)")
        .execute(&mut *barrier)
        .await
        .unwrap();
    let mut first = Contender::start(bytes, &request(app.id));
    let mut second = Contender::start(bytes, &request(app.id));
    // Observe both real PostgreSQL waiters; do not infer concurrency from a sleep.
    tokio::time::timeout(Duration::from_secs(5), async {
        loop {
            let waiting: i64 = sqlx::query_scalar("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND objid=22091401 AND NOT granted AND database=(SELECT oid FROM pg_database WHERE datname=current_database())")
                .fetch_one(&f.owner).await.unwrap();
            if waiting==2 { break; }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    }).await.expect("both independent contenders must reach the database gate");
    barrier.commit().await.unwrap();
    let outcomes = [first.finish().await, second.finish().await];
    assert_eq!(
        outcomes
            .iter()
            .filter(|value| value.starts_with("RESERVED "))
            .count(),
        1
    );
    assert_eq!(
        outcomes
            .iter()
            .filter(|value| value.trim() == "NO_CAPACITY")
            .count(),
        1
    );
    assert_eq!(f.count(connection.id()).await, 1);
    f.close().await;
}

#[tokio::test]
async fn twenty_competing_requests_reserve_last_slot_port_and_command_exactly_once() {
    let f = Fixture::new().await;
    let (connection, app, _) = f.prepared(DeploymentTarget::Webview, 1).await;
    let user = f.session("user", ClientType::Android).await;
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = f.instances.clone();
        let key = user.clone();
        let epoch = connection.epoch();
        tasks.push(tokio::spawn(async move {
            store
                .reserve(
                    ResourceCredential::User(&key),
                    ClientType::Android,
                    epoch,
                    &request(app.id),
                )
                .await
        }));
    }
    let mut successes = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(instance) => {
                successes += 1;
                assert_eq!(instance.state, "reserved");
            }
            Err(error) => assert_eq!(error, StoreError::NoCapacity),
        }
    }
    assert_eq!(successes, 1);
    assert_eq!(f.count(connection.id()).await, 1);
    let counts: (i64, i64) = sqlx::query_as("SELECT (SELECT count(*) FROM pixels.instance_commands WHERE node_id=$1), (SELECT count(*) FROM pixels.instance_events e JOIN pixels.instances i ON i.id=e.instance_id WHERE i.node_id=$1)")
        .bind(connection.id()).fetch_one(&f.owner).await.unwrap();
    assert_eq!(counts, (1, 1));
    let port: i32 = sqlx::query_scalar("SELECT port FROM pixels.instances WHERE node_id=$1")
        .bind(connection.id())
        .fetch_one(&f.owner)
        .await
        .unwrap();
    assert_eq!(port, 4613);
    f.close().await;
}

#[tokio::test]
async fn response_loss_retry_is_owner_scoped_and_changed_body_never_reuses_result() {
    let f = Fixture::new().await;
    let (connection, app, deployment) = f.prepared(DeploymentTarget::Webview, 4).await;
    let user = f.session("user", ClientType::Android).await;
    let initial = request(app.id);
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = f.instances.clone();
        let key = user.clone();
        let req = initial.clone();
        let epoch = connection.epoch();
        tasks.push(tokio::spawn(async move {
            store
                .reserve(
                    ResourceCredential::User(&key),
                    ClientType::Android,
                    epoch,
                    &req,
                )
                .await
        }));
    }
    let mut ids = std::collections::BTreeSet::new();
    for task in tasks {
        ids.insert(task.await.unwrap().unwrap().id);
    }
    assert_eq!(ids.len(), 1);
    assert_eq!(f.count(connection.id()).await, 1);
    let mut changed = initial.clone();
    changed.deployment_id = Some(deployment.id);
    assert_eq!(
        f.instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &changed
            )
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    let second = f.session("user", ClientType::Android).await;
    let other = f
        .instances
        .reserve(
            ResourceCredential::User(&second),
            ClientType::Android,
            connection.epoch(),
            &initial,
        )
        .await
        .unwrap();
    assert!(!ids.contains(&other.id));
    assert_eq!(f.count(connection.id()).await, 2);
    assert!(f
        .instances
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            other.id
        )
        .await
        .is_err());
    f.close().await;
}

#[tokio::test]
async fn guest_user_client_and_rdp_busy_boundaries_are_explicit() {
    let f = Fixture::new().await;
    let (connection, app, _) = f.prepared(DeploymentTarget::Rdp, 1).await;
    let (guest, guest_id) = f.guest().await;
    let req = request(app.id);
    assert!(f
        .instances
        .reserve(
            ResourceCredential::User(&guest),
            ClientType::Android,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    assert!(f
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Panel,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    let instance = f
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            connection.epoch(),
            &req,
        )
        .await
        .unwrap();
    assert_eq!(instance.owner, ResourceOwner::Guest { guest_id });
    assert_eq!(instance.application_id, app.id);
    assert_eq!(instance.client_type, "android");
    let user = f.session("user", ClientType::Android).await;
    assert_eq!(
        f.instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &request(app.id)
            )
            .await
            .unwrap_err(),
        StoreError::NoCapacity
    );
    assert!(f
        .instances
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            instance.id
        )
        .await
        .is_err());
    f.guests.logout(&guest, ClientType::Android).await.unwrap();
    assert!(f
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    assert_eq!(f.count(connection.id()).await, 1); // no transport/Windows cleanup on logout
    f.close().await;
}

#[tokio::test]
async fn all_readiness_maintenance_capacity_and_acl_gates_are_checked_inside_reservation() {
    let f = Fixture::new().await;
    let (connection, mut app, deployment) = f.prepared(DeploymentTarget::Webview, 4).await;
    let user = f.session("user", ClientType::Android).await;
    let req = request(app.id);
    for (deny, restore) in [
        (
            "UPDATE pixels.nodes SET draining=true WHERE id=$1",
            "UPDATE pixels.nodes SET draining=false WHERE id=$1",
        ),
        (
            "UPDATE pixels.nodes SET state='reconciling' WHERE id=$1",
            "UPDATE pixels.nodes SET state='ready' WHERE id=$1",
        ),
        (
            "UPDATE pixels.nodes SET last_seen=clock_timestamp()-interval '31 seconds' WHERE id=$1",
            "UPDATE pixels.nodes SET last_seen=clock_timestamp() WHERE id=$1",
        ),
        (
            "UPDATE pixels.nodes SET webview=false WHERE id=$1",
            "UPDATE pixels.nodes SET webview=true WHERE id=$1",
        ),
    ] {
        sqlx::query(deny)
            .bind(connection.id())
            .execute(&f.owner)
            .await
            .unwrap();
        assert_eq!(
            f.instances
                .reserve(
                    ResourceCredential::User(&user),
                    ClientType::Android,
                    connection.epoch(),
                    &req
                )
                .await
                .unwrap_err(),
            StoreError::NoCapacity
        );
        sqlx::query(restore)
            .bind(connection.id())
            .execute(&f.owner)
            .await
            .unwrap();
    }
    sqlx::query("UPDATE pixels.application_deployments SET disabled=true WHERE id=$1")
        .bind(deployment.id)
        .execute(&f.owner)
        .await
        .unwrap();
    assert!(f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    sqlx::query("UPDATE pixels.application_deployments SET disabled=false,observed_state='pending' WHERE id=$1").bind(deployment.id).execute(&f.owner).await.unwrap();
    assert!(f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    sqlx::query("UPDATE pixels.application_deployments SET observed_state='ready' WHERE id=$1")
        .bind(deployment.id)
        .execute(&f.owner)
        .await
        .unwrap();
    app.spec.access = ApplicationAccess::Acl;
    f.apps.update(&f.admin, app.id, 1, &app.spec).await.unwrap();
    assert_eq!(
        f.instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &req
            )
            .await
            .unwrap_err(),
        StoreError::Rejected
    );
    assert_eq!(f.count(connection.id()).await, 0);
    f.close().await;
}

#[tokio::test]
async fn command_or_event_failure_rolls_back_reservation_and_does_not_consume_capacity() {
    let f = Fixture::new().await;
    let (connection, app, _) = f.prepared(DeploymentTarget::Webview, 1).await;
    let user = f.session("user", ClientType::Android).await;
    let req = request(app.id);
    for table in ["instance_commands", "instance_events"] {
        sqlx::query(&format!(
            "REVOKE INSERT ON pixels.{table} FROM pixels_console_runtime"
        ))
        .execute(&f.owner)
        .await
        .unwrap();
        let outcome = f
            .instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &req,
            )
            .await;
        sqlx::query(&format!(
            "GRANT INSERT ON pixels.{table} TO pixels_console_runtime"
        ))
        .execute(&f.owner)
        .await
        .unwrap();
        assert!(outcome.is_err());
        assert_eq!(f.count(connection.id()).await, 0);
    }
    let result = f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &req,
        )
        .await
        .unwrap();
    assert_eq!(result.state, "reserved");
    assert_eq!(f.count(connection.id()).await, 1);
    f.close().await;
}

#[tokio::test]
async fn launch_snapshot_and_ownership_survive_configuration_changes_and_restart() {
    let f = Fixture::new().await;
    let (connection, mut app, _) = f
        .prepared(
            DeploymentTarget::GameHook {
                install_root: r"D:\游戏 根目录".into(),
            },
            1,
        )
        .await;
    let user = f.session("user", ClientType::Android).await;
    let req = request(app.id);
    let instance = f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &req,
        )
        .await
        .unwrap();
    let snapshot: (String, String, String) = sqlx::query_as(
        "SELECT install_root,executable_relative,arguments FROM pixels.instances WHERE id=$1",
    )
    .bind(instance.id)
    .fetch_one(&f.owner)
    .await
    .unwrap();
    assert_eq!(
        snapshot,
        (
            r"D:\游戏 根目录".into(),
            r"子目录\Game.exe".into(),
            r#""含空格 参数""#.into()
        )
    );
    if let ApplicationLaunch::GameHook { arguments, .. } = &mut app.spec.launch {
        *arguments = "changed".into();
    }
    f.apps.update(&f.admin, app.id, 1, &app.spec).await.unwrap();
    let replay = f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &req,
        )
        .await
        .unwrap();
    assert_eq!(replay, instance);
    f.nodes.begin_runtime().await.unwrap();
    assert!(f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id)
        )
        .await
        .is_err());
    assert_eq!(f.count(connection.id()).await, 1);
    f.instances.close().await;
    assert!(f
        .instances
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            instance.id
        )
        .await
        .is_err());
    let reopened = InstanceStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap();
    let restored = reopened
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            instance.id,
        )
        .await
        .unwrap();
    assert_eq!(restored.id, instance.id);
    assert_eq!(restored.owner, instance.owner);
    assert_eq!(restored.state, "reconcile_required");
    assert_eq!(restored.revision, instance.revision + 1);
    assert!(restored.ended_at.is_none());
    let command_state: String =
        sqlx::query_scalar("SELECT state FROM pixels.instance_commands WHERE instance_id=$1")
            .bind(instance.id)
            .fetch_one(&f.owner)
            .await
            .unwrap();
    assert_eq!(command_state, "cancelled");
    reopened.close().await;
    f.close().await;
}

#[tokio::test]
async fn port_budget_and_unknown_occupancy_are_not_released_by_timeout() {
    let f = Fixture::new().await;
    let (connection, app, _) = f.prepared(DeploymentTarget::Webview, 4).await;
    // The synthetic node advertises one application port despite four CPU/GPU capacity slots.
    sqlx::query("UPDATE pixels.nodes SET application_port_end=application_port_start WHERE id=$1")
        .bind(connection.id())
        .execute(&f.owner)
        .await
        .unwrap();
    let user = f.session("user", ClientType::Android).await;
    let first = f
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.instances SET state='reconcile_required' WHERE id=$1")
        .bind(first.id)
        .execute(&f.owner)
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.instance_commands SET deadline=clock_timestamp()-interval '1 second' WHERE instance_id=$1").bind(first.id).execute(&f.owner).await.unwrap();
    assert_eq!(
        f.instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &request(app.id)
            )
            .await
            .unwrap_err(),
        StoreError::NoCapacity
    );
    assert_eq!(f.count(connection.id()).await, 1);
    f.close().await;
}
