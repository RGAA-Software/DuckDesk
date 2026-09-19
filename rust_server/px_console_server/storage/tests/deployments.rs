use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ApplicationAccess, ApplicationDefinition, ApplicationLaunch, ApplicationSpec, ApplicationStore,
    ClientType, DeploymentConfiguration, DeploymentObservation, DeploymentProfile, DeploymentStore,
    DeploymentTarget, DevicePlatform, DeviceStore, GpuResourceProfile, IdentityStore,
    NodeConnection, NodeDeploymentPreparation, NodeProduct, NodeReport, NodeStore, NodeTelemetry,
    PasswordDigest, PreparationFailure, PreparationState, StoreError, TelemetryProbeState,
    TokenDigest, Username, VideoCodec, VideoSpec,
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
        telemetry: unavailable_telemetry(),
    }
}
fn unavailable_telemetry() -> NodeTelemetry {
    NodeTelemetry {
        sampled_at: chrono::Utc::now(),
        probe_state: TelemetryProbeState::Unavailable,
        logical_processors: None,
        cpu_utilization_per_mille: None,
        memory_total_bytes: None,
        memory_available_bytes: None,
        disk_total_bytes: None,
        disk_free_bytes: None,
        gpu_inventory_revision: None,
        gpus: Vec::new(),
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
    async fn close(self) {
        self.deployments.close().await;
        self.apps.close().await;
        self.nodes.close().await;
        self.devices.close().await;
        self.identity.close().await;
        self.owner.close().await;
    }
}

#[tokio::test]
async fn all_modes_have_explicit_fields_stable_identity_and_database_constraints() {
    let fixture = Fixture::new().await;
    let (node, node_key) = fixture.node().await;
    let mut created = Vec::new();
    for target in [
        DeploymentTarget::GameHook {
            install_root: r"D:\游戏 根目录".into(),
        },
        DeploymentTarget::Webview,
        DeploymentTarget::Rdp,
    ] {
        let (app, deployment) = fixture.deployment(node, target.clone()).await;
        created.push(deployment.id);
        assert_eq!(deployment.observed_state, "pending");
        assert_eq!(deployment.observed_sequence, 0);
        assert_eq!(fixture.get(deployment.id).await, deployment);
        assert!(fixture
            .deployments
            .create(&fixture.admin, app.id, node, &settings(target.clone()))
            .await
            .is_err());
        assert!(fixture
            .deployments
            .configure(&fixture.admin, deployment.id, 1, &settings(target))
            .await
            .is_ok());
        assert!(
            sqlx::query("UPDATE pixels.application_deployments SET capacity=0 WHERE id=$1")
                .bind(deployment.id)
                .execute(&fixture.owner)
                .await
                .is_err()
        );
    }
    let app = fixture.app(&DeploymentTarget::Rdp).await;
    assert!(fixture
        .deployments
        .create(
            &fixture.admin,
            app.id,
            node,
            &settings(DeploymentTarget::Webview)
        )
        .await
        .is_err());
    assert!(fixture
        .deployments
        .create(
            &fixture.admin,
            app.id,
            Uuid::new_v4(),
            &settings(DeploymentTarget::Rdp)
        )
        .await
        .is_err());
    let mut bad = settings(DeploymentTarget::Rdp);
    bad.capacity = 2;
    assert!(fixture
        .deployments
        .create(&fixture.admin, app.id, node, &bad)
        .await
        .is_err());
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let connection = fixture
        .nodes
        .open_connection(epoch, &node_key, &token())
        .await
        .unwrap();
    fixture
        .nodes
        .report(&connection, &node_report(1))
        .await
        .unwrap();
    let first = fixture
        .deployments
        .list_node(&connection, None, 2)
        .await
        .unwrap();
    assert_eq!(first.len(), 2);
    let second = fixture
        .deployments
        .list_node(&connection, first.last().map(|row| row.id), 2)
        .await
        .unwrap();
    assert_eq!(second.len(), 1);
    let mut assignments = first.into_iter().chain(second).collect::<Vec<_>>();
    assignments.sort_by_key(|row| row.id);
    created.sort();
    assert_eq!(
        assignments.iter().map(|row| row.id).collect::<Vec<_>>(),
        created
    );
    assert!(assignments
        .iter()
        .any(|row| matches!(row.preparation, NodeDeploymentPreparation::GameHook { .. })));
    assert!(assignments
        .iter()
        .any(|row| matches!(row.preparation, NodeDeploymentPreparation::Webview { .. })));
    assert!(assignments
        .iter()
        .any(|row| matches!(row.preparation, NodeDeploymentPreparation::Rdp { .. })));
    assert!(fixture
        .deployments
        .list_node(&connection, None, 0)
        .await
        .is_err());
    fixture.close().await;
}

#[tokio::test]
async fn roles_and_terminal_types_cannot_manufacture_deployment_authority() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    let (app, deployment) = fixture.deployment(node, DeploymentTarget::Webview).await;
    for denied in [
        fixture.session("user", ClientType::AdminWeb).await,
        fixture.session("admin", ClientType::Android).await,
        key,
    ] {
        assert!(matches!(
            fixture.deployments.list_managed(&denied, None, 10).await,
            Err(StoreError::Rejected)
        ));
        assert!(matches!(
            fixture
                .deployments
                .create(&denied, app.id, node, &settings(DeploymentTarget::Webview))
                .await,
            Err(StoreError::Rejected)
        ));
    }
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    assert!(!fixture
        .deployments
        .list_managed(&viewer, None, 10)
        .await
        .unwrap()
        .is_empty());
    assert!(fixture
        .deployments
        .configure(
            &viewer,
            deployment.id,
            1,
            &settings(DeploymentTarget::Webview)
        )
        .await
        .is_err());
    assert!(fixture
        .deployments
        .list_managed(&fixture.admin, None, 0)
        .await
        .is_err());
    let runtime = config("RUNTIME").connect().await.unwrap();
    assert!(sqlx::query("DELETE FROM pixels.deployment_audit")
        .execute(&runtime)
        .await
        .is_err());
    runtime.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn preparation_receipts_bind_node_generation_endpoint_and_order() {
    let fixture = Fixture::new().await;
    let (connection, key) = fixture.connected().await;
    let (_, deployment) = fixture
        .deployment(connection.id(), DeploymentTarget::Webview)
        .await;
    let ready = fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 1))
        .await
        .unwrap();
    assert_eq!(ready.observed_state, "ready");
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 1))
        .await
        .is_err());
    let mut failed = observation(&deployment, 2);
    failed.status = PreparationState::Failed {
        reason: PreparationFailure::BindingUnverified,
    };
    let result = fixture
        .deployments
        .report(&connection, deployment.id, &failed)
        .await
        .unwrap();
    assert_eq!(
        result.observed_reason.as_deref(),
        Some("binding_unverified")
    );
    let (other, other_key) = fixture.node().await;
    let other_connection = fixture
        .nodes
        .open_connection(connection.epoch(), &other_key, &token())
        .await
        .unwrap();
    fixture
        .nodes
        .report(&other_connection, &node_report(1))
        .await
        .unwrap();
    assert_ne!(other, connection.id());
    assert!(fixture
        .deployments
        .report(
            &other_connection,
            deployment.id,
            &observation(&deployment, 3)
        )
        .await
        .is_err());
    let current = fixture
        .nodes
        .open_connection(connection.epoch(), &key, &token())
        .await
        .unwrap();
    assert!(fixture
        .deployments
        .report(&current, deployment.id, &observation(&deployment, 1))
        .await
        .is_err()); // no endpoint report yet
    fixture
        .nodes
        .report(&current, &node_report(1))
        .await
        .unwrap();
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 3))
        .await
        .is_err());
    assert!(fixture
        .deployments
        .report(&current, deployment.id, &observation(&deployment, 1))
        .await
        .is_ok());
    let mut endpoint = node_report(2);
    endpoint.public_host = "changed.example.test".into();
    fixture.nodes.report(&current, &endpoint).await.unwrap();
    assert!(fixture
        .deployments
        .report(&current, deployment.id, &observation(&deployment, 2))
        .await
        .is_err());
    let mut newer = observation(&deployment, 2);
    newer.endpoint_revision = 3;
    assert!(fixture
        .deployments
        .report(&current, deployment.id, &newer)
        .await
        .is_ok());
    fixture.close().await;
}

#[tokio::test]
async fn concurrent_configuration_and_application_revisions_invalidate_preparation() {
    let fixture = Fixture::new().await;
    let (connection, _) = fixture.connected().await;
    let (mut app, deployment) = fixture
        .deployment(connection.id(), DeploymentTarget::Webview)
        .await;
    fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 1))
        .await
        .unwrap();
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = fixture.deployments.clone();
        let admin = fixture.admin.clone();
        tasks.push(tokio::spawn(async move {
            let mut change = settings(DeploymentTarget::Webview);
            change.capacity = 2;
            store.configure(&admin, deployment.id, 1, &change).await
        }));
    }
    let mut successes = 0;
    for task in tasks {
        successes += usize::from(task.await.unwrap().is_ok());
    }
    assert_eq!(successes, 1);
    let pending = fixture.get(deployment.id).await;
    assert_eq!(pending.revision, 2);
    assert_eq!(pending.observed_state, "pending");
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 2))
        .await
        .is_err());
    fixture
        .deployments
        .report(&connection, deployment.id, &observation(&pending, 1))
        .await
        .unwrap();
    app.spec.name = "新版本".into();
    fixture
        .apps
        .update(&fixture.admin, app.id, 1, &app.spec)
        .await
        .unwrap();
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&pending, 2))
        .await
        .is_err());
    let mut unchanged = settings(DeploymentTarget::Webview);
    unchanged.capacity = 2;
    let revised = fixture
        .deployments
        .configure(&fixture.admin, deployment.id, 2, &unchanged)
        .await
        .unwrap();
    assert_eq!(revised.application_revision, 2);
    assert_eq!(revised.revision, 3);
    assert_eq!(revised.observed_state, "pending");
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&revised, 1))
        .await
        .is_ok());
    fixture.close().await;
}

#[tokio::test]
async fn audit_failure_preserves_configuration_and_restart_preserves_only_history() {
    let fixture = Fixture::new().await;
    let (connection, _) = fixture.connected().await;
    let (_, deployment) = fixture
        .deployment(connection.id(), DeploymentTarget::Webview)
        .await;
    let ready = fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 1))
        .await
        .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.deployment_audit FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let mut changed = settings(DeploymentTarget::Webview);
    changed.disabled = true;
    let outcome = fixture
        .deployments
        .configure(&fixture.admin, deployment.id, 1, &changed)
        .await;
    sqlx::query("GRANT INSERT ON pixels.deployment_audit TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(outcome.is_err());
    assert_eq!(fixture.get(deployment.id).await, ready);
    fixture.nodes.begin_runtime().await.unwrap();
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 2))
        .await
        .is_err());
    // A historical Ready receipt is preserved, not promoted to a live admission decision.
    assert_eq!(fixture.get(deployment.id).await, ready);
    fixture.deployments.close().await;
    assert!(fixture
        .deployments
        .list_managed(&fixture.admin, None, 10)
        .await
        .is_err());
    let reopened = DeploymentStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap();
    assert!(!reopened
        .list_managed(&fixture.admin, None, 100)
        .await
        .unwrap()
        .is_empty());
    reopened.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn expired_contact_deleted_application_and_invalid_observations_never_authorize_preparation()
{
    let fixture = Fixture::new().await;
    let (connection, _) = fixture.connected().await;
    let (app, deployment) = fixture
        .deployment(connection.id(), DeploymentTarget::Rdp)
        .await;
    let mut invalid = observation(&deployment, 1);
    invalid.sequence = u64::MAX;
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &invalid)
        .await
        .is_err());
    sqlx::query(
        "UPDATE pixels.nodes SET last_seen=clock_timestamp()-interval '31 seconds' WHERE id=$1",
    )
    .bind(connection.id())
    .execute(&fixture.owner)
    .await
    .unwrap();
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 1))
        .await
        .is_err());
    fixture
        .nodes
        .report(&connection, &node_report(2))
        .await
        .unwrap();
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 1))
        .await
        .is_ok());
    fixture
        .apps
        .delete(&fixture.admin, app.id, 1)
        .await
        .unwrap();
    assert!(fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 2))
        .await
        .is_err());
    assert!(fixture
        .deployments
        .configure(
            &fixture.admin,
            deployment.id,
            1,
            &settings(DeploymentTarget::Rdp)
        )
        .await
        .is_err());
    fixture.close().await;
}
