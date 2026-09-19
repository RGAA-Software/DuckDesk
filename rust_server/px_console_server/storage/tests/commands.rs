use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ApplicationAccess, ApplicationDefinition, ApplicationLaunch, ApplicationSpec, ApplicationStore,
    ClientType, DeploymentConfiguration, DeploymentObservation, DeploymentProfile, DeploymentStore,
    DeploymentTarget, DevicePlatform, DeviceStore, GpuResourceProfile, IdentityStore,
    NodeConnection, NodeGpuTelemetry, NodeProduct, NodeReport, NodeStore, NodeTelemetry,
    PasswordDigest, PreparationState, StoreError, TelemetryProbeState, TokenDigest, Username,
    VideoCodec, VideoSpec,
};
use px_console_store::{
    ApplicationInstance, CommandOutcome, CommandReceipt, NodeCommand, NodeCommandAction,
    ObservedRuntime, ObservedRuntimePhase, RuntimeInventory,
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
        gpu_profile: (target != DeploymentTarget::Rdp).then_some(test_gpu_profile()),
        target,
        capacity,
        gpu_key: None,
        disabled: false,
    }
}
fn test_gpu_profile() -> GpuResourceProfile {
    GpuResourceProfile {
        memory_bytes: 512 * 1024 * 1024,
        compute_per_mille: 100,
        encoder_per_mille: 100,
        memory_reserve_bytes: 512 * 1024 * 1024,
        compute_limit_per_mille: 900,
        encoder_limit_per_mille: 900,
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
        telemetry: test_telemetry(sequence),
    }
}
fn test_telemetry(inventory_revision: u64) -> NodeTelemetry {
    NodeTelemetry {
        sampled_at: chrono::Utc::now(),
        probe_state: TelemetryProbeState::Ready,
        logical_processors: Some(8),
        cpu_utilization_per_mille: Some(100),
        memory_total_bytes: Some(16 * 1024 * 1024 * 1024),
        memory_available_bytes: Some(12 * 1024 * 1024 * 1024),
        disk_total_bytes: Some(256 * 1024 * 1024 * 1024),
        disk_free_bytes: Some(200 * 1024 * 1024 * 1024),
        gpu_inventory_revision: Some(inventory_revision),
        gpus: vec![NodeGpuTelemetry {
            stable_key: "gpu-test-1".into(),
            name: "Test GPU".into(),
            runtime_binding_ready: true,
            dedicated_memory_bytes: Some(8 * 1024 * 1024 * 1024),
            used_memory_bytes: Some(1024 * 1024 * 1024),
            utilization_per_mille: Some(100),
            encoder_utilization_per_mille: Some(100),
        }],
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
        let mut fixture = Self {
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
        fixture.admin = fixture.session("admin", ClientType::AdminWeb).await;
        fixture
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
        let challenge = self
            .instances
            .begin_reconciliation(&connection)
            .await
            .unwrap();
        self.instances
            .reconcile(
                &connection,
                &RuntimeInventory {
                    challenge_id: challenge.id,
                    runtimes: Vec::new(),
                },
            )
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
    async fn started(
        &self,
        connection: &NodeConnection,
        app: Uuid,
    ) -> (TokenDigest, ApplicationInstance, NodeCommand) {
        let user = self.session("user", ClientType::Android).await;
        let instance = self
            .instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &request(app),
            )
            .await
            .unwrap();
        let command = self
            .instances
            .next_command(connection)
            .await
            .unwrap()
            .unwrap();
        (user, instance, command)
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

#[tokio::test]
async fn revoke_between_claim_and_ack_atomically_adds_one_exact_stop() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let (_, instance, start) = fixture.started(&connection, app.id).await;
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
    let ack = receipt(&start, CommandOutcome::Running { port: port(&start) });
    // Failure to persist the compensating Stop must also roll back the Running receipt.
    sqlx::query("REVOKE INSERT ON pixels.instance_commands FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let failed = fixture
        .instances
        .acknowledge_command(&connection, &ack)
        .await;
    sqlx::query("GRANT INSERT ON pixels.instance_commands TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(failed.is_err());
    assert_eq!(state(&fixture, instance.id).await.0, "starting");
    for _ in 0..2 {
        assert_eq!(
            fixture
                .instances
                .acknowledge_command(&connection, &ack)
                .await
                .unwrap()
                .state,
            "stopping"
        );
    }
    let count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.instance_commands WHERE instance_id=$1 AND kind='stop'",
    )
    .bind(instance.id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(count, 1);
    let stop = fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    assert!(matches!(stop.action, NodeCommandAction::Stop));
    assert_eq!(stop.launch_id, start.launch_id);
    assert!(stop.instance_revision > start.instance_revision);
    fixture.close().await;
}

#[tokio::test]
async fn config_change_or_drain_after_claim_reconciles_without_killing_existing_launch() {
    let fixture = Fixture::new().await;
    for drain in [false, true] {
        let (connection, mut app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
        let (user, instance, start) = fixture.started(&connection, app.id).await;
        if drain {
            fixture
                .nodes
                .configure(
                    &fixture.admin,
                    connection.id(),
                    2,
                    NodeConfiguration {
                        draining: true,
                        disabled: false,
                        max_instances: 1,
                    },
                )
                .await
                .unwrap();
        } else {
            app.spec.name = "new name".into();
            app.spec.launch = ApplicationLaunch::Webview {
                entry_url: "https://changed.example.test/".into(),
                video: VideoSpec {
                    codec: VideoCodec::H265,
                    bitrate_kbps: 16000,
                },
            };
            fixture
                .apps
                .update(&fixture.admin, app.id, app.revision, &app.spec)
                .await
                .unwrap();
        }
        sqlx::query("UPDATE pixels.instance_commands SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1")
            .bind(start.id).execute(&fixture.owner).await.unwrap();
        assert!(fixture
            .instances
            .next_command(&connection)
            .await
            .unwrap()
            .is_none());
        assert_eq!(state(&fixture, instance.id).await.0, "reconcile_required");
        assert!(state(&fixture, instance.id).await.1.is_none());
        let challenge = fixture
            .instances
            .begin_reconciliation(&connection)
            .await
            .unwrap();
        fixture
            .instances
            .reconcile(
                &connection,
                &RuntimeInventory {
                    challenge_id: challenge.id,
                    runtimes: vec![observed(&start)],
                },
            )
            .await
            .unwrap();
        assert_eq!(state(&fixture, instance.id).await.0, "running");
        let snapshot: (i64, String) = sqlx::query_as(
            "SELECT application_revision,entry_url FROM pixels.instances WHERE id=$1",
        )
        .bind(instance.id)
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
        assert_eq!(
            snapshot,
            (start.application_revision, "https://example.test/".into())
        );
        let stops: i64 = sqlx::query_scalar(
            "SELECT count(*) FROM pixels.instance_commands WHERE instance_id=$1 AND kind='stop'",
        )
        .bind(instance.id)
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
        assert_eq!(stops, 0);
        assert_eq!(
            fixture
                .instances
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
    }
    fixture.close().await;
}

#[tokio::test]
async fn revocation_during_inventory_cannot_restore_running_authorization() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let (guest, _) = fixture.guest().await;
    let instance = fixture
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let start = fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    let challenge = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    fixture
        .guests
        .logout(&guest, ClientType::Android)
        .await
        .unwrap();
    fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: challenge.id,
                runtimes: vec![observed(&start)],
            },
        )
        .await
        .unwrap();
    assert_eq!(state(&fixture, instance.id).await.0, "stopping");
    let stop = fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    assert!(matches!(stop.action, NodeCommandAction::Stop));
    assert!(stop.instance_revision > challenge.launches[0].reject_through_revision);
    assert_eq!(stop.launch_id, start.launch_id);
    fixture.close().await;
}

#[tokio::test]
async fn endpoint_changes_invalidate_inventory_and_require_new_deployment_report() {
    let fixture = Fixture::new().await;
    let (connection, app, deployment) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let old = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    let mut report = node_report(2);
    report.public_host = "new-node.example.test".into();
    let node = fixture.nodes.report(&connection, &report).await.unwrap();
    assert!(fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: old.id,
                runtimes: Vec::new(),
            }
        )
        .await
        .is_err());
    let current = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: current.id,
                runtimes: Vec::new(),
            },
        )
        .await
        .unwrap();
    assert_eq!(
        fixture
            .instances
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
    let mut prepared = observation(&deployment, 2);
    prepared.endpoint_revision = node.endpoint_revision;
    fixture
        .deployments
        .report(&connection, deployment.id, &prepared)
        .await
        .unwrap();
    assert_eq!(
        fixture
            .instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &request(app.id)
            )
            .await
            .unwrap()
            .state,
        "reserved"
    );
    fixture.close().await;
}

#[tokio::test]
async fn unsent_stop_is_immediate_but_unknown_ack_keeps_occupancy_and_denies_other_owners() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let other = fixture.session("user", ClientType::Android).await;
    let instance = fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    assert!(fixture
        .instances
        .stop(
            ResourceCredential::User(&other),
            ClientType::Android,
            connection.epoch(),
            instance.id,
            1
        )
        .await
        .is_err());
    let stopped = fixture
        .instances
        .stop(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            instance.id,
            1,
        )
        .await
        .unwrap();
    assert_eq!(stopped.state, "stopped");
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    let (_, uncertain, start) = fixture.started(&connection, app.id).await;
    let ack = receipt(&start, CommandOutcome::Unknown);
    let unknown = fixture
        .instances
        .acknowledge_command(&connection, &ack)
        .await
        .unwrap();
    assert_eq!(unknown.state, "reconcile_required");
    assert!(unknown.ended_at.is_none());
    assert_eq!(
        fixture
            .instances
            .acknowledge_command(&connection, &ack)
            .await
            .unwrap(),
        unknown
    );
    assert!(fixture
        .instances
        .acknowledge_command(&connection, &receipt(&start, CommandOutcome::Absent))
        .await
        .is_err());
    assert_eq!(
        fixture
            .instances
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
    assert!(state(&fixture, uncertain.id).await.1.is_none());
    fixture.close().await;
}
fn receipt(command: &NodeCommand, outcome: CommandOutcome) -> CommandReceipt {
    CommandReceipt {
        command_id: command.id,
        lease_id: command.lease_id,
        instance_id: command.instance_id,
        launch_id: command.launch_id,
        instance_revision: command.instance_revision,
        outcome,
    }
}
fn port(command: &NodeCommand) -> u16 {
    match command.action {
        NodeCommandAction::Start { port, .. } => port,
        _ => panic!("Start required"),
    }
}
fn observed(command: &NodeCommand) -> ObservedRuntime {
    ObservedRuntime {
        instance_id: command.instance_id,
        launch_id: command.launch_id,
        port: port(command),
        phase: ObservedRuntimePhase::Running,
    }
}
async fn state(fixture: &Fixture, id: Uuid) -> (String, Option<chrono::DateTime<chrono::Utc>>) {
    sqlx::query_as("SELECT state,ended_at FROM pixels.instances WHERE id=$1")
        .bind(id)
        .fetch_one(&fixture.owner)
        .await
        .unwrap()
}

#[tokio::test]
async fn start_ack_stop_and_duplicate_receipts_never_touch_reused_capacity() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture
        .prepared(
            DeploymentTarget::GameHook {
                install_root: r"D:\游戏 根目录".into(),
            },
            1,
        )
        .await;
    let (user, instance, start) = fixture.started(&connection, app.id).await;
    assert!(matches!(instance.owner, ResourceOwner::User { .. }));
    assert_eq!(start.instance_revision, 2);
    match &start.action {
        NodeCommandAction::Start {
            install_root,
            launch: ApplicationLaunch::GameHook { arguments, .. },
            ..
        } => {
            assert_eq!(install_root.as_deref(), Some(r"D:\游戏 根目录"));
            assert_eq!(arguments, r#""含空格 参数""#);
        }
        _ => panic!("typed game launch required"),
    }
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    let ack = receipt(&start, CommandOutcome::Running { port: port(&start) });
    let running = fixture
        .instances
        .acknowledge_command(&connection, &ack)
        .await
        .unwrap();
    assert_eq!(running.state, "running");
    assert_eq!(
        fixture
            .instances
            .acknowledge_command(&connection, &ack)
            .await
            .unwrap(),
        running
    );
    let mut wrong = ack.clone();
    wrong.lease_id = Uuid::new_v4();
    assert!(fixture
        .instances
        .acknowledge_command(&connection, &wrong)
        .await
        .is_err());
    wrong = ack.clone();
    wrong.outcome = CommandOutcome::Running {
        port: port(&start) + 1,
    };
    assert!(fixture
        .instances
        .acknowledge_command(&connection, &wrong)
        .await
        .is_err());
    let stopping = fixture
        .instances
        .stop(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            instance.id,
            running.revision,
        )
        .await
        .unwrap();
    assert_eq!(stopping.state, "stopping");
    // A replay of an already-completed Start receipt returns current state, never Running again.
    assert_eq!(
        fixture
            .instances
            .acknowledge_command(&connection, &ack)
            .await
            .unwrap(),
        stopping
    );
    let stop = fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    assert!(matches!(stop.action, NodeCommandAction::Stop));
    let stop_ack = receipt(&stop, CommandOutcome::Absent);
    let stopped = fixture
        .instances
        .acknowledge_command(&connection, &stop_ack)
        .await
        .unwrap();
    assert_eq!(stopped.state, "stopped");
    assert!(stopped.ended_at.is_some());
    assert_eq!(
        fixture
            .instances
            .acknowledge_command(&connection, &stop_ack)
            .await
            .unwrap(),
        stopped
    );
    let new = fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    assert_eq!(
        fixture
            .instances
            .stop(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                instance.id,
                1
            )
            .await
            .unwrap(),
        stopped
    );
    assert_eq!(state(&fixture, new.id).await.0, "reserved");
    assert_eq!(fixture.count(connection.id()).await, 2);
    fixture.close().await;
}

#[tokio::test]
async fn concurrent_claim_and_lease_reclaim_keep_command_id_and_reject_old_lease_and_node() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = fixture.instances.clone();
        let node = connection.clone();
        tasks.push(tokio::spawn(async move { store.next_command(&node).await }));
    }
    let mut commands = Vec::new();
    for task in tasks {
        if let Some(command) = task.await.unwrap().unwrap() {
            commands.push(command);
        }
    }
    assert_eq!(commands.len(), 1);
    let old = commands.remove(0);
    sqlx::query("UPDATE pixels.instance_commands SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1")
        .bind(old.id).execute(&fixture.owner).await.unwrap();
    let current = fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    assert_eq!(current.id, old.id);
    assert_eq!(current.instance_revision, old.instance_revision);
    assert_ne!(current.lease_id, old.lease_id);
    assert!(fixture
        .instances
        .acknowledge_command(
            &connection,
            &receipt(&old, CommandOutcome::Running { port: port(&old) })
        )
        .await
        .is_err());
    let (_, key) = fixture.node().await;
    let other = fixture
        .nodes
        .open_connection(connection.epoch(), &key, &token())
        .await
        .unwrap();
    fixture.nodes.report(&other, &node_report(1)).await.unwrap();
    assert!(fixture
        .instances
        .acknowledge_command(
            &other,
            &receipt(
                &current,
                CommandOutcome::Running {
                    port: port(&current)
                }
            )
        )
        .await
        .is_err());
    assert_eq!(
        fixture
            .instances
            .acknowledge_command(
                &connection,
                &receipt(
                    &current,
                    CommandOutcome::Running {
                        port: port(&current)
                    }
                )
            )
            .await
            .unwrap()
            .state,
        "running"
    );
    fixture.close().await;
}

#[tokio::test]
async fn original_login_logout_before_dispatch_cancels_without_network_effect() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let instance = fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
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
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    let result = state(&fixture, instance.id).await;
    assert_eq!(result.0, "failed");
    assert!(result.1.is_some());
    let attempts: i32 =
        sqlx::query_scalar("SELECT attempts FROM pixels.instance_commands WHERE instance_id=$1")
            .bind(instance.id)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(attempts, 0);
    fixture.close().await;
}

#[tokio::test]
async fn revoked_guest_after_claim_gets_higher_revision_stop_and_late_start_is_rejected() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let (guest, _) = fixture.guest().await;
    let instance = fixture
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let start = fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    fixture
        .guests
        .logout(&guest, ClientType::Android)
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.instance_commands SET lease_until=clock_timestamp()-interval '1 second' WHERE id=$1")
        .bind(start.id).execute(&fixture.owner).await.unwrap();
    let stop = fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    assert!(matches!(stop.action, NodeCommandAction::Stop));
    assert!(stop.instance_revision > start.instance_revision);
    assert!(fixture
        .instances
        .acknowledge_command(
            &connection,
            &receipt(&start, CommandOutcome::Running { port: port(&start) })
        )
        .await
        .is_err());
    assert_eq!(
        fixture
            .instances
            .acknowledge_command(&connection, &receipt(&stop, CommandOutcome::Absent))
            .await
            .unwrap()
            .state,
        "stopped"
    );
    assert_eq!(fixture.count(connection.id()).await, 1);
    assert!(state(&fixture, instance.id).await.1.is_some());
    fixture.close().await;
}

#[tokio::test]
async fn claim_ack_and_administrator_audit_failures_roll_back_every_state_change() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let instance = fixture
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
        .execute(&fixture.owner)
        .await
        .unwrap();
    let claim = fixture.instances.next_command(&connection).await;
    sqlx::query("GRANT INSERT ON pixels.instance_events TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(claim.is_err());
    assert_eq!(state(&fixture, instance.id).await.0, "reserved");
    let start = fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    let ack = receipt(&start, CommandOutcome::Running { port: port(&start) });
    sqlx::query("REVOKE INSERT ON pixels.instance_events FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let outcome = fixture
        .instances
        .acknowledge_command(&connection, &ack)
        .await;
    sqlx::query("GRANT INSERT ON pixels.instance_events TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(outcome.is_err());
    assert_eq!(state(&fixture, instance.id).await.0, "starting");
    let running = fixture
        .instances
        .acknowledge_command(&connection, &ack)
        .await
        .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.instance_admin_actions FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let outcome = fixture
        .instances
        .stop_managed(
            &fixture.admin,
            connection.epoch(),
            instance.id,
            running.revision,
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.instance_admin_actions TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(outcome.is_err());
    assert_eq!(state(&fixture, instance.id).await.0, "running");
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    assert!(fixture
        .instances
        .stop_managed(&viewer, connection.epoch(), instance.id, running.revision)
        .await
        .is_err());
    let stopping = fixture
        .instances
        .stop_managed(
            &fixture.admin,
            connection.epoch(),
            instance.id,
            running.revision,
        )
        .await
        .unwrap();
    assert_eq!(stopping.state, "stopping");
    let audits: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.instance_admin_actions WHERE instance_id=$1",
    )
    .bind(instance.id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(audits, 1);
    fixture.close().await;
}

#[tokio::test]
async fn timeout_releases_only_never_claimed_work_and_keeps_uncertain_occupancy() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let unsent = fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.instance_commands SET deadline=clock_timestamp()-interval '1 second' WHERE instance_id=$1")
        .bind(unsent.id).execute(&fixture.owner).await.unwrap();
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    assert_eq!(state(&fixture, unsent.id).await.0, "failed");
    assert!(state(&fixture, unsent.id).await.1.is_some());
    let (_, uncertain, start) = fixture.started(&connection, app.id).await;
    sqlx::query("UPDATE pixels.instance_commands SET deadline=clock_timestamp()-interval '1 second',lease_until=clock_timestamp()-interval '1 second' WHERE id=$1")
        .bind(start.id).execute(&fixture.owner).await.unwrap();
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    assert_eq!(state(&fixture, uncertain.id).await.0, "reconcile_required");
    assert!(state(&fixture, uncertain.id).await.1.is_none());
    assert!(fixture
        .instances
        .acknowledge_command(
            &connection,
            &receipt(&start, CommandOutcome::Running { port: port(&start) })
        )
        .await
        .is_err());
    assert_eq!(
        fixture
            .instances
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
    fixture.close().await;
}

#[tokio::test]
async fn inventory_challenges_reject_unknown_old_duplicate_expired_and_mismatched_reports() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let old = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    let unknown = ObservedRuntime {
        instance_id: Uuid::new_v4(),
        launch_id: Uuid::new_v4(),
        port: 4613,
        phase: ObservedRuntimePhase::Running,
    };
    assert!(fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: old.id,
                runtimes: vec![unknown]
            }
        )
        .await
        .is_err());
    assert_eq!(
        fixture
            .instances
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
    let current = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    assert!(fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: old.id,
                runtimes: Vec::new()
            }
        )
        .await
        .is_err());
    fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: current.id,
                runtimes: Vec::new(),
            },
        )
        .await
        .unwrap();
    let (_, instance, start) = fixture.started(&connection, app.id).await;
    // A consumed empty inventory must not erase a later Start.
    assert!(fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: current.id,
                runtimes: Vec::new()
            }
        )
        .await
        .is_err());
    assert_eq!(state(&fixture, instance.id).await.0, "starting");
    let expired = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.nodes SET reconciliation_deadline=clock_timestamp()-interval '1 second' WHERE id=$1")
        .bind(connection.id()).execute(&fixture.owner).await.unwrap();
    assert!(fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: expired.id,
                runtimes: vec![observed(&start)]
            }
        )
        .await
        .is_err());
    let current = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    let mut wrong = observed(&start);
    wrong.launch_id = Uuid::new_v4();
    assert!(fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: current.id,
                runtimes: vec![wrong]
            }
        )
        .await
        .is_err());
    let mut wrong = observed(&start);
    wrong.port += 1;
    assert!(fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: current.id,
                runtimes: vec![wrong]
            }
        )
        .await
        .is_err());
    assert!(fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: current.id,
                runtimes: vec![observed(&start), observed(&start)]
            }
        )
        .await
        .is_err());
    sqlx::query("REVOKE INSERT ON pixels.instance_events FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let outcome = fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: current.id,
                runtimes: vec![observed(&start)],
            },
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.instance_events TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(outcome.is_err());
    assert_eq!(state(&fixture, instance.id).await.0, "reconcile_required");
    fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: current.id,
                runtimes: vec![observed(&start)],
            },
        )
        .await
        .unwrap();
    assert_eq!(state(&fixture, instance.id).await.0, "running");
    fixture.close().await;
}

#[tokio::test]
async fn reconnect_reconciles_exact_rdp_runtime_without_replaying_start_or_changing_owner() {
    let fixture = Fixture::new().await;
    let (connection, app, deployment) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let (user, instance, start) = fixture.started(&connection, app.id).await;
    fixture.nodes.close_connection(&connection).await.unwrap();
    let key = token();
    let revision: i64 = sqlx::query_scalar("SELECT revision FROM pixels.nodes WHERE id=$1")
        .bind(connection.id())
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    fixture
        .nodes
        .rotate_key(&fixture.admin, connection.id(), revision, &key)
        .await
        .unwrap();
    let new = fixture
        .nodes
        .open_connection(connection.epoch(), &key, &token())
        .await
        .unwrap();
    fixture.nodes.report(&new, &node_report(1)).await.unwrap();
    fixture
        .deployments
        .report(&new, deployment.id, &observation(&deployment, 1))
        .await
        .unwrap();
    let challenge = fixture.instances.begin_reconciliation(&new).await.unwrap();
    assert_eq!(challenge.launches.len(), 1);
    assert!(challenge.launches[0].reject_through_revision > start.instance_revision);
    assert_eq!(challenge.launches[0].launch_id, start.launch_id);
    assert!(fixture
        .instances
        .acknowledge_command(
            &connection,
            &receipt(&start, CommandOutcome::Running { port: port(&start) })
        )
        .await
        .is_err());
    fixture
        .instances
        .reconcile(
            &new,
            &RuntimeInventory {
                challenge_id: challenge.id,
                runtimes: vec![observed(&start)],
            },
        )
        .await
        .unwrap();
    let running = fixture
        .instances
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            instance.id,
        )
        .await
        .unwrap();
    assert_eq!(running.state, "running");
    assert_eq!(running.owner, instance.owner);
    assert!(fixture
        .instances
        .next_command(&new)
        .await
        .unwrap()
        .is_none());
    let count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels.instance_commands WHERE instance_id=$1 AND kind='start'",
    )
    .bind(instance.id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(count, 1);
    fixture
        .instances
        .stop_managed(&fixture.admin, new.epoch(), instance.id, running.revision)
        .await
        .unwrap();
    let stop = fixture.instances.next_command(&new).await.unwrap().unwrap();
    assert_eq!(stop.node_generation, new.generation());
    assert!(matches!(stop.action, NodeCommandAction::Stop));
    assert_eq!(
        fixture
            .instances
            .acknowledge_command(&new, &receipt(&stop, CommandOutcome::Absent))
            .await
            .unwrap()
            .state,
        "stopped"
    );
    fixture.close().await;
}

#[tokio::test]
async fn fenced_absence_releases_resources_and_interleaved_stop_is_not_downgraded_to_failed() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let (user, instance, start) = fixture.started(&connection, app.id).await;
    let challenge = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: challenge.id,
                runtimes: Vec::new(),
            },
        )
        .await
        .unwrap();
    assert_eq!(state(&fixture, instance.id).await.0, "failed");
    assert!(state(&fixture, instance.id).await.1.is_some());
    assert!(fixture
        .instances
        .acknowledge_command(
            &connection,
            &receipt(&start, CommandOutcome::Running { port: port(&start) })
        )
        .await
        .is_err());
    let second = fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .unwrap();
    let challenge = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    let current = fixture
        .instances
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            second.id,
        )
        .await
        .unwrap();
    fixture
        .instances
        .stop(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            second.id,
            current.revision,
        )
        .await
        .unwrap();
    fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: challenge.id,
                runtimes: Vec::new(),
            },
        )
        .await
        .unwrap();
    assert_eq!(state(&fixture, second.id).await.0, "stopped");
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    fixture.close().await;
}

#[tokio::test]
async fn spec_changes_and_draining_reject_unsent_start_without_spawning() {
    let fixture = Fixture::new().await;
    let (connection, mut app, deployment) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let first = fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    app.spec.name = "new spec".into();
    fixture
        .apps
        .update(&fixture.admin, app.id, 1, &app.spec)
        .await
        .unwrap();
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    assert_eq!(state(&fixture, first.id).await.0, "failed");
    let mut changed = settings(DeploymentTarget::Webview);
    changed.capacity = 1;
    let deployment = fixture
        .deployments
        .configure(&fixture.admin, deployment.id, deployment.revision, &changed)
        .await
        .unwrap();
    fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 1))
        .await
        .unwrap();
    let second = fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    fixture
        .nodes
        .configure(
            &fixture.admin,
            connection.id(),
            2,
            NodeConfiguration {
                draining: true,
                disabled: false,
                max_instances: 1,
            },
        )
        .await
        .unwrap();
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    assert_eq!(state(&fixture, second.id).await.0, "failed");
    let attempts: i64 = sqlx::query_scalar(
        "SELECT sum(attempts)::bigint FROM pixels.instance_commands WHERE node_id=$1",
    )
    .bind(connection.id())
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(attempts, 0);
    fixture.close().await;
}

#[tokio::test]
async fn heartbeat_after_a_stale_gap_requires_new_inventory_and_cancels_old_commands() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let (_, instance, start) = fixture.started(&connection, app.id).await;
    sqlx::query(
        "UPDATE pixels.nodes SET last_seen=clock_timestamp()-interval '31 seconds' WHERE id=$1",
    )
    .bind(connection.id())
    .execute(&fixture.owner)
    .await
    .unwrap();
    let report = fixture
        .nodes
        .report(&connection, &node_report(2))
        .await
        .unwrap();
    assert!(report.fresh);
    assert_eq!(report.state, "reconciling");
    assert_eq!(state(&fixture, instance.id).await.0, "reconcile_required");
    assert!(fixture
        .instances
        .next_command(&connection)
        .await
        .unwrap()
        .is_none());
    let challenge = fixture
        .instances
        .begin_reconciliation(&connection)
        .await
        .unwrap();
    fixture
        .instances
        .reconcile(
            &connection,
            &RuntimeInventory {
                challenge_id: challenge.id,
                runtimes: vec![observed(&start)],
            },
        )
        .await
        .unwrap();
    assert_eq!(state(&fixture, instance.id).await.0, "running");
    assert!(fixture
        .instances
        .acknowledge_command(
            &connection,
            &receipt(&start, CommandOutcome::Running { port: port(&start) })
        )
        .await
        .is_err());
    fixture.close().await;
}
