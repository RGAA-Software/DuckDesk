use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ApplicationAccess, ApplicationDefinition, ApplicationLaunch, ApplicationSpec, ApplicationStore,
    ClientType, DeploymentConfiguration, DeploymentObservation, DeploymentProfile, DeploymentStore,
    DeploymentTarget, DevicePlatform, DeviceStore, GpuResourceProfile, IdentityStore,
    NodeConnection, NodeGpuTelemetry, NodeProduct, NodeReport, NodeStore, NodeTelemetry,
    PasswordDigest, PlacementPreviewRequest, PlacementRejectionReason, PreparationState,
    RelayNodeConfiguration, RelayNodeProfile, RelayNodeReport, RelayNodeSpec, RelayNodeStore,
    RuntimeEntitlement, StoreError, TelemetryProbeState, TokenDigest, Username, VideoCodec,
    VideoSpec,
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
        rdp_domain: Some("RDP-NODE".into()),
        rdp_proxy_certificate_sha256: Some("b".repeat(64)),
        telemetry: test_telemetry(sequence),
    }
}
fn gpu_report(sequence: u64, gpus: Vec<NodeGpuTelemetry>) -> NodeReport {
    let mut report = node_report(sequence);
    report.telemetry.gpu_inventory_revision = Some(sequence);
    report.telemetry.gpus = gpus;
    report
}
fn gpu(
    stable_key: &str,
    utilization_per_mille: Option<u16>,
    encoder_utilization_per_mille: Option<u16>,
) -> NodeGpuTelemetry {
    NodeGpuTelemetry {
        stable_key: stable_key.into(),
        name: format!("Test GPU {stable_key}"),
        runtime_binding_ready: true,
        dedicated_memory_bytes: Some(8 * 1024 * 1024 * 1024),
        used_memory_bytes: Some(1024 * 1024 * 1024),
        utilization_per_mille,
        encoder_utilization_per_mille,
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

struct RelayCurrentLoad {
    connections: u32,
    rooms: u32,
}

async fn ready_relay(
    relay_nodes: &RelayNodeStore,
    administrator: &TokenDigest,
    node_connection: &NodeConnection,
    relay_spec: RelayNodeSpec,
    current_load: RelayCurrentLoad,
) -> RelayNodeProfile {
    let credential = token();
    let created = relay_nodes
        .create(administrator, &relay_spec, &credential)
        .await
        .unwrap();
    relay_nodes
        .configure(
            administrator,
            created.id,
            created.revision,
            RelayNodeConfiguration {
                draining: false,
                disabled: false,
            },
        )
        .await
        .unwrap();
    let relay_connection = relay_nodes
        .open_connection(node_connection.epoch(), &credential, &token())
        .await
        .unwrap();
    relay_nodes
        .report(
            &relay_connection,
            &RelayNodeReport {
                sequence: 1,
                product_version_code: 1,
                draining: false,
                max_connections: 100,
                current_connections: current_load.connections,
                max_rooms: 50,
                current_rooms: current_load.rooms,
                uploaded_bytes: 0,
                forwarded_bytes: 0,
            },
        )
        .await
        .unwrap()
}

#[tokio::test]
async fn two_nodes_and_two_relays_spread_new_work_without_migrating_existing_bindings() {
    let fixture = Fixture::new().await;
    let relay_nodes = RelayNodeStore::connect(
        &config("RUNTIME"),
        env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap(),
    )
    .await
    .unwrap();
    let (first_node, application, _) = fixture.prepared(DeploymentTarget::Webview, 2).await;
    let (second_node_id, second_node_credential) = fixture.node().await;
    let second_node = fixture
        .nodes
        .open_connection(first_node.epoch(), &second_node_credential, &token())
        .await
        .unwrap();
    fixture
        .nodes
        .report(&second_node, &node_report(1))
        .await
        .unwrap();
    fixture
        .nodes
        .configure(
            &fixture.admin,
            second_node_id,
            1,
            NodeConfiguration {
                draining: false,
                disabled: false,
                max_instances: 2,
            },
        )
        .await
        .unwrap();
    let second_deployment = fixture
        .deployments
        .create(
            &fixture.admin,
            application.id,
            second_node_id,
            &settings(DeploymentTarget::Webview),
        )
        .await
        .unwrap();
    let mut second_deployment_configuration = settings(DeploymentTarget::Webview);
    second_deployment_configuration.capacity = 2;
    let second_deployment = fixture
        .deployments
        .configure(
            &fixture.admin,
            second_deployment.id,
            second_deployment.revision,
            &second_deployment_configuration,
        )
        .await
        .unwrap();
    fixture
        .deployments
        .report(
            &second_node,
            second_deployment.id,
            &observation(&second_deployment, 1),
        )
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.nodes SET state='ready' WHERE id=$1")
        .bind(second_node_id)
        .execute(&fixture.owner)
        .await
        .unwrap();

    let busier_relay = ready_relay(
        &relay_nodes,
        &fixture.admin,
        &first_node,
        RelayNodeSpec {
            name: format!("busier-relay-{}", Uuid::new_v4()),
            public_host: "busier-relay.example.test".into(),
            public_port: 4710,
        },
        RelayCurrentLoad {
            connections: 40,
            rooms: 20,
        },
    )
    .await;
    let quieter_relay = ready_relay(
        &relay_nodes,
        &fixture.admin,
        &first_node,
        RelayNodeSpec {
            name: format!("quieter-relay-{}", Uuid::new_v4()),
            public_host: "quieter-relay.example.test".into(),
            public_port: 4711,
        },
        RelayCurrentLoad {
            connections: 2,
            rooms: 1,
        },
    )
    .await;
    let first_user = fixture.session("user", ClientType::Android).await;
    let first_start_request = request(application.id);
    let first_instance = fixture
        .instances
        .reserve(
            ResourceCredential::User(&first_user),
            ClientType::Android,
            first_node.epoch(),
            &first_start_request,
        )
        .await
        .unwrap();
    let retry = fixture
        .instances
        .reserve(
            ResourceCredential::User(&first_user),
            ClientType::Android,
            first_node.epoch(),
            &first_start_request,
        )
        .await
        .unwrap();
    assert_eq!(retry.id, first_instance.id);
    relay_nodes
        .configure(
            &fixture.admin,
            quieter_relay.id,
            quieter_relay.revision,
            RelayNodeConfiguration {
                draining: true,
                disabled: false,
            },
        )
        .await
        .unwrap();
    let second_user = fixture.session("user", ClientType::Android).await;
    let second_instance = fixture
        .instances
        .reserve(
            ResourceCredential::User(&second_user),
            ClientType::Android,
            first_node.epoch(),
            &request(application.id),
        )
        .await
        .unwrap();
    let mut commands = Vec::new();
    for node_connection in [&first_node, &second_node] {
        if let Some(command) = fixture
            .instances
            .next_command(node_connection)
            .await
            .unwrap()
        {
            commands.push(command);
        }
    }
    assert_eq!(commands.len(), 2);
    let first_command = commands
        .iter()
        .find(|command| command.instance_id == first_instance.id)
        .unwrap();
    let first_binding = first_command.relay.as_ref().unwrap();
    assert_eq!(first_binding.relay_node_id, quieter_relay.id);
    assert_eq!(first_binding.relay_generation, quieter_relay.generation);
    assert_eq!(first_binding.public_host, "quieter-relay.example.test");
    assert_eq!(first_binding.public_port, 4711);
    let second_command = commands
        .iter()
        .find(|command| command.instance_id == second_instance.id)
        .unwrap();
    let second_binding = second_command.relay.as_ref().unwrap();
    assert_eq!(second_binding.relay_node_id, busier_relay.id);
    assert_eq!(second_binding.public_host, "busier-relay.example.test");
    assert_eq!(second_binding.public_port, 4710);

    relay_nodes.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn licensed_services_reject_cloud_and_rdp_reservations_before_commands_exist() {
    let fixture = Fixture::new().await;
    let (cloud_node, cloud_app, _) = fixture.prepared(DeploymentTarget::Webview, 2).await;
    let cloud_user = fixture.session("user", ClientType::Android).await;
    let without_cloud = RuntimeEntitlement::new(8, false, true, true).unwrap();
    assert_eq!(
        fixture
            .instances
            .reserve_with_entitlement(
                ResourceCredential::User(&cloud_user),
                ClientType::Android,
                cloud_node.epoch(),
                &request(cloud_app.id),
                without_cloud,
            )
            .await,
        Err(StoreError::LicenseRestriction)
    );
    assert!(fixture
        .instances
        .next_command(&cloud_node)
        .await
        .unwrap()
        .is_none());

    let (rdp_node, rdp_app, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let rdp_user = fixture.session("user", ClientType::Android).await;
    let without_rdp = RuntimeEntitlement::new(8, true, true, false).unwrap();
    assert_eq!(
        fixture
            .instances
            .reserve_with_entitlement(
                ResourceCredential::User(&rdp_user),
                ClientType::Android,
                rdp_node.epoch(),
                &request(rdp_app.id),
                without_rdp,
            )
            .await,
        Err(StoreError::LicenseRestriction)
    );
    assert!(fixture
        .instances
        .next_command(&rdp_node)
        .await
        .unwrap()
        .is_none());
    fixture.close().await;
}

#[tokio::test]
async fn pinned_gpu_admission_accounts_for_measured_and_pending_pressure() {
    let fixture = Fixture::new().await;
    let (connection, app, deployment) = fixture.prepared(DeploymentTarget::Webview, 4).await;
    let mut configuration = settings(DeploymentTarget::Webview);
    configuration.gpu_key = Some("gpu-idle".into());
    configuration.gpu_profile = Some(GpuResourceProfile {
        memory_bytes: 512 * 1024 * 1024,
        compute_per_mille: 300,
        encoder_per_mille: 200,
        memory_reserve_bytes: 512 * 1024 * 1024,
        compute_limit_per_mille: 600,
        encoder_limit_per_mille: 600,
    });
    let deployment = fixture
        .deployments
        .configure(
            &fixture.admin,
            deployment.id,
            deployment.revision,
            &configuration,
        )
        .await
        .unwrap();
    fixture
        .nodes
        .report(
            &connection,
            &gpu_report(
                2,
                vec![
                    gpu("gpu-busy", Some(350), Some(100)),
                    gpu("gpu-idle", Some(100), Some(100)),
                ],
            ),
        )
        .await
        .unwrap();
    fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 2))
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.nodes SET state='ready' WHERE id=$1")
        .bind(connection.id())
        .execute(&fixture.owner)
        .await
        .unwrap();
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
    let reserved: (String, i64, i64, i16, i16) = sqlx::query_as(
        "SELECT gpu_key,gpu_inventory_revision,gpu_memory_reservation_bytes,gpu_compute_reservation_per_mille,gpu_encoder_reservation_per_mille FROM pixels.instances WHERE id=$1",
    )
    .bind(first.id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(
        reserved,
        ("gpu-idle".into(), 2, 512 * 1024 * 1024, 300, 200)
    );
    assert_eq!(
        fixture
            .instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &request(app.id),
            )
            .await
            .unwrap_err(),
        StoreError::NoCapacity
    );
    fixture.close().await;
}

#[tokio::test]
async fn unpinned_multi_gpu_inventory_selects_a_concrete_adapter_binding() {
    let fixture = Fixture::new().await;
    let (connection, app, deployment) = fixture.prepared(DeploymentTarget::Webview, 2).await;
    fixture
        .nodes
        .report(
            &connection,
            &gpu_report(
                2,
                vec![
                    gpu("gpu-first", Some(100), Some(100)),
                    gpu("gpu-second", Some(100), Some(100)),
                ],
            ),
        )
        .await
        .unwrap();
    fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 2))
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.nodes SET state='ready' WHERE id=$1")
        .bind(connection.id())
        .execute(&fixture.owner)
        .await
        .unwrap();
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
    let selected: String = sqlx::query_scalar("SELECT gpu_key FROM pixels.instances WHERE id=$1")
        .bind(instance.id)
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    assert_eq!(selected, "gpu-first");
    fixture.close().await;
}

#[tokio::test]
async fn unknown_gpu_pressure_is_not_treated_as_free_capacity() {
    let fixture = Fixture::new().await;
    let (connection, app, deployment) = fixture.prepared(DeploymentTarget::Webview, 2).await;
    fixture
        .nodes
        .report(
            &connection,
            &gpu_report(2, vec![gpu("gpu-unknown", Some(100), None)]),
        )
        .await
        .unwrap();
    fixture
        .deployments
        .report(&connection, deployment.id, &observation(&deployment, 2))
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.nodes SET state='ready' WHERE id=$1")
        .bind(connection.id())
        .execute(&fixture.owner)
        .await
        .unwrap();
    let user = fixture.session("user", ClientType::Android).await;
    assert_eq!(
        fixture
            .instances
            .reserve(
                ResourceCredential::User(&user),
                ClientType::Android,
                connection.epoch(),
                &request(app.id),
            )
            .await
            .unwrap_err(),
        StoreError::NoCapacity
    );
    fixture.close().await;
}

#[tokio::test]
async fn placement_preview_explains_current_candidates_without_reserving_capacity() {
    let fixture = Fixture::new().await;
    let (connection, app, deployment) = fixture.prepared(DeploymentTarget::Webview, 2).await;
    let request = PlacementPreviewRequest {
        application_id: app.id,
        deployment_id: None,
    };

    let ready = fixture
        .instances
        .preview_placement(&fixture.admin, connection.epoch(), &request)
        .await
        .unwrap();
    assert_eq!(ready.application_id, app.id);
    assert_eq!(ready.candidates.len(), 1);
    assert_eq!(ready.candidates[0].rank, Some(1));
    assert!(ready.candidates[0].eligible);
    assert_eq!(ready.candidates[0].deployment_id, deployment.id);
    assert_eq!(ready.candidates[0].gpu_key.as_deref(), Some("gpu-test-1"));
    assert!(ready.candidates[0].rejection_reasons.is_empty());
    assert_eq!(fixture.count(connection.id()).await, 0);

    sqlx::query("UPDATE pixels.nodes SET draining=true WHERE id=$1")
        .bind(connection.id())
        .execute(&fixture.owner)
        .await
        .unwrap();
    let draining = fixture
        .instances
        .preview_placement(&fixture.admin, connection.epoch(), &request)
        .await
        .unwrap();
    assert!(!draining.candidates[0].eligible);
    assert_eq!(draining.candidates[0].rank, None);
    assert_eq!(
        draining.candidates[0].rejection_reasons,
        vec![PlacementRejectionReason::NodeDraining]
    );

    sqlx::query("UPDATE pixels.nodes SET draining=false WHERE id=$1")
        .bind(connection.id())
        .execute(&fixture.owner)
        .await
        .unwrap();
    sqlx::query(
        "UPDATE pixels.node_gpu_latest SET encoder_utilization_per_mille=NULL WHERE node_id=$1",
    )
    .bind(connection.id())
    .execute(&fixture.owner)
    .await
    .unwrap();
    let unknown_metrics = fixture
        .instances
        .preview_placement(&fixture.admin, connection.epoch(), &request)
        .await
        .unwrap();
    assert_eq!(
        unknown_metrics.candidates[0].rejection_reasons,
        vec![PlacementRejectionReason::GpuMetricsUnknown]
    );
    assert_eq!(
        unknown_metrics.candidates[0].gpu_encoder_headroom_per_mille,
        None
    );
    assert_eq!(fixture.count(connection.id()).await, 0);

    fixture.close().await;
}

struct Contender(std::process::Child);

#[tokio::test]
async fn origin_login_session_and_revision_are_persisted_with_owner_foreign_keys() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 4).await;
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
    let (owner, session, revision): (Uuid, Uuid, i64) = sqlx::query_as(
        "SELECT owner_user,login_session_id,owner_revision FROM pixels.instances WHERE id=$1",
    )
    .bind(instance.id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    let session_owner: Uuid =
        sqlx::query_scalar("SELECT user_id FROM pixels.login_sessions WHERE id=$1")
            .bind(session)
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(owner, session_owner);
    assert_eq!(revision, 1);
    let unrelated: Uuid = sqlx::query_scalar(
        "SELECT id FROM pixels.login_sessions WHERE user_id<>$1 ORDER BY id LIMIT 1",
    )
    .bind(owner)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert!(
        sqlx::query("UPDATE pixels.instances SET login_session_id=$2 WHERE id=$1")
            .bind(instance.id)
            .bind(unrelated)
            .execute(&fixture.owner)
            .await
            .is_err()
    );
    assert!(
        sqlx::query("UPDATE pixels.instances SET login_session_id=NULL WHERE id=$1")
            .bind(instance.id)
            .execute(&fixture.owner)
            .await
            .is_err()
    );
    let (guest, guest_id) = fixture.guest().await;
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
    let stored: (Uuid, Option<Uuid>, i64) = sqlx::query_as(
        "SELECT owner_guest,login_session_id,owner_revision FROM pixels.instances WHERE id=$1",
    )
    .bind(instance.id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(stored, (guest_id, None, 1));
    fixture.close().await;
}

#[tokio::test]
async fn disconnect_invalidation_is_atomic_cancels_commands_and_preserves_occupancy() {
    let fixture = Fixture::new().await;
    let (connection, app, deployment) = fixture.prepared(DeploymentTarget::Webview, 1).await;
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
    let outcome = fixture.nodes.close_connection(&connection).await;
    sqlx::query("GRANT INSERT ON pixels.instance_events TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(outcome.is_err());
    assert_eq!(
        fixture
            .instances
            .get(
                ResourceCredential::User(&user),
                ClientType::Android,
                instance.id
            )
            .await
            .unwrap(),
        instance
    );
    assert!(fixture
        .nodes
        .report(&connection, &node_report(2))
        .await
        .is_ok());
    fixture.nodes.close_connection(&connection).await.unwrap();
    let unknown = fixture
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
    // Even a synthetic Ready reset cannot free the unknown reservation.
    sqlx::query("UPDATE pixels.nodes SET state='ready' WHERE id=$1")
        .bind(connection.id())
        .execute(&fixture.owner)
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
    let pending: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.instance_commands WHERE instance_id=$1 AND state IN ('pending','claimed')")
        .bind(instance.id).fetch_one(&fixture.owner).await.unwrap();
    assert_eq!(pending, 0);
    fixture.close().await;
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
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture
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
    fixture
        .identity
        .issue_session(
            user.id,
            1,
            &TokenDigest::from_sha256(bytes),
            ClientType::Android,
            Duration::from_secs(3600),
        )
        .await
        .unwrap();
    let mut barrier = fixture.owner.begin().await.unwrap();
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
                .fetch_one(&fixture.owner).await.unwrap();
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
    assert_eq!(fixture.count(connection.id()).await, 1);
    fixture.close().await;
}

#[tokio::test]
async fn twenty_competing_requests_reserve_last_slot_port_and_command_exactly_once() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = fixture.instances.clone();
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
    assert_eq!(fixture.count(connection.id()).await, 1);
    let counts: (i64, i64) = sqlx::query_as("SELECT (SELECT count(*) FROM pixels.instance_commands WHERE node_id=$1), (SELECT count(*) FROM pixels.instance_events e JOIN pixels.instances i ON i.id=e.instance_id WHERE i.node_id=$1)")
        .bind(connection.id()).fetch_one(&fixture.owner).await.unwrap();
    assert_eq!(counts, (1, 1));
    let port: i32 = sqlx::query_scalar("SELECT port FROM pixels.instances WHERE node_id=$1")
        .bind(connection.id())
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    assert_eq!(port, 4613);
    fixture.close().await;
}

#[tokio::test]
async fn response_loss_retry_is_owner_scoped_and_changed_body_never_reuses_result() {
    let fixture = Fixture::new().await;
    let (connection, app, deployment) = fixture.prepared(DeploymentTarget::Webview, 4).await;
    let user = fixture.session("user", ClientType::Android).await;
    let initial = request(app.id);
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = fixture.instances.clone();
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
    assert_eq!(fixture.count(connection.id()).await, 1);
    let mut changed = initial.clone();
    changed.deployment_id = Some(deployment.id);
    assert_eq!(
        fixture
            .instances
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
    let second = fixture.session("user", ClientType::Android).await;
    let other = fixture
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
    assert_eq!(fixture.count(connection.id()).await, 2);
    assert!(fixture
        .instances
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            other.id
        )
        .await
        .is_err());
    fixture.close().await;
}

#[tokio::test]
async fn owned_instance_listing_is_paginated_and_never_crosses_principals() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 4).await;
    let owner = fixture.session("user", ClientType::Android).await;
    let other_user = fixture.session("user", ClientType::Android).await;
    let (guest, _) = fixture.guest().await;
    let mut owned_ids = Vec::new();
    for _ in 0..2 {
        owned_ids.push(
            fixture
                .instances
                .reserve(
                    ResourceCredential::User(&owner),
                    ClientType::Android,
                    connection.epoch(),
                    &request(app.id),
                )
                .await
                .unwrap()
                .id,
        );
    }
    let other_instance = fixture
        .instances
        .reserve(
            ResourceCredential::User(&other_user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();
    let guest_instance = fixture
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            connection.epoch(),
            &request(app.id),
        )
        .await
        .unwrap();

    owned_ids.sort();
    let first_page = fixture
        .instances
        .list_owned(
            ResourceCredential::User(&owner),
            ClientType::Android,
            None,
            1,
        )
        .await
        .unwrap();
    assert_eq!(first_page.len(), 1);
    assert_eq!(first_page[0].id, owned_ids[0]);
    let second_page = fixture
        .instances
        .list_owned(
            ResourceCredential::User(&owner),
            ClientType::Android,
            Some(first_page[0].id),
            100,
        )
        .await
        .unwrap();
    assert_eq!(second_page.len(), 1);
    assert_eq!(second_page[0].id, owned_ids[1]);
    assert!(!owned_ids.contains(&other_instance.id));

    let guest_rows = fixture
        .instances
        .list_owned(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            None,
            100,
        )
        .await
        .unwrap();
    assert_eq!(guest_rows.len(), 1);
    assert_eq!(guest_rows[0].id, guest_instance.id);
    for limit in [0, 101] {
        assert_eq!(
            fixture
                .instances
                .list_owned(
                    ResourceCredential::User(&owner),
                    ClientType::Android,
                    None,
                    limit,
                )
                .await
                .unwrap_err(),
            StoreError::InvalidInput
        );
    }
    fixture.close().await;
}

#[tokio::test]
async fn guest_user_client_and_rdp_busy_boundaries_are_explicit() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Rdp, 1).await;
    let (guest, guest_id) = fixture.guest().await;
    let req = request(app.id);
    assert!(fixture
        .instances
        .reserve(
            ResourceCredential::User(&guest),
            ClientType::Android,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    assert!(fixture
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Panel,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    let instance = fixture
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
    let user = fixture.session("user", ClientType::Android).await;
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
    assert!(fixture
        .instances
        .get(
            ResourceCredential::User(&user),
            ClientType::Android,
            instance.id
        )
        .await
        .is_err());
    fixture
        .guests
        .logout(&guest, ClientType::Android)
        .await
        .unwrap();
    assert!(fixture
        .instances
        .reserve(
            ResourceCredential::Guest(&guest),
            ClientType::Android,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    assert_eq!(fixture.count(connection.id()).await, 1); // no transport/Windows cleanup on logout
    fixture.close().await;
}

#[tokio::test]
async fn all_readiness_maintenance_capacity_and_acl_gates_are_checked_inside_reservation() {
    let fixture = Fixture::new().await;
    let (connection, mut app, deployment) = fixture.prepared(DeploymentTarget::Webview, 4).await;
    let user = fixture.session("user", ClientType::Android).await;
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
            .execute(&fixture.owner)
            .await
            .unwrap();
        assert_eq!(
            fixture
                .instances
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
            .execute(&fixture.owner)
            .await
            .unwrap();
    }
    sqlx::query("UPDATE pixels.application_deployments SET disabled=true WHERE id=$1")
        .bind(deployment.id)
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &req
        )
        .await
        .is_err());
    sqlx::query("UPDATE pixels.application_deployments SET disabled=false,observed_state='pending' WHERE id=$1").bind(deployment.id).execute(&fixture.owner).await.unwrap();
    assert!(fixture
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
        .execute(&fixture.owner)
        .await
        .unwrap();
    app.spec.access = ApplicationAccess::Acl;
    fixture
        .apps
        .update(&fixture.admin, app.id, 1, &app.spec)
        .await
        .unwrap();
    assert_eq!(
        fixture
            .instances
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
    assert_eq!(fixture.count(connection.id()).await, 0);
    fixture.close().await;
}

#[tokio::test]
async fn command_or_event_failure_rolls_back_reservation_and_does_not_consume_capacity() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 1).await;
    let user = fixture.session("user", ClientType::Android).await;
    let req = request(app.id);
    for table in ["instance_commands", "instance_events"] {
        sqlx::query(&format!(
            "REVOKE INSERT ON pixels.{table} FROM pixels_console_runtime"
        ))
        .execute(&fixture.owner)
        .await
        .unwrap();
        let outcome = fixture
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
        .execute(&fixture.owner)
        .await
        .unwrap();
        assert!(outcome.is_err());
        assert_eq!(fixture.count(connection.id()).await, 0);
    }
    let result = fixture
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
    assert_eq!(fixture.count(connection.id()).await, 1);
    fixture.close().await;
}

#[tokio::test]
async fn launch_snapshot_and_ownership_survive_configuration_changes_and_restart() {
    let fixture = Fixture::new().await;
    let (connection, mut app, _) = fixture
        .prepared(
            DeploymentTarget::GameHook {
                install_root: r"D:\游戏 根目录".into(),
            },
            1,
        )
        .await;
    let user = fixture.session("user", ClientType::Android).await;
    let req = request(app.id);
    let instance = fixture
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
    .fetch_one(&fixture.owner)
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
    fixture
        .apps
        .update(&fixture.admin, app.id, 1, &app.spec)
        .await
        .unwrap();
    let replay = fixture
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
    fixture.nodes.begin_runtime().await.unwrap();
    assert!(fixture
        .instances
        .reserve(
            ResourceCredential::User(&user),
            ClientType::Android,
            connection.epoch(),
            &request(app.id)
        )
        .await
        .is_err());
    assert_eq!(fixture.count(connection.id()).await, 1);
    fixture.instances.close().await;
    assert!(fixture
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
            .fetch_one(&fixture.owner)
            .await
            .unwrap();
    assert_eq!(command_state, "cancelled");
    reopened.close().await;
    fixture.close().await;
}

#[tokio::test]
async fn port_budget_and_unknown_occupancy_are_not_released_by_timeout() {
    let fixture = Fixture::new().await;
    let (connection, app, _) = fixture.prepared(DeploymentTarget::Webview, 4).await;
    // The synthetic node advertises one application port despite four CPU/GPU capacity slots.
    sqlx::query("UPDATE pixels.nodes SET application_port_end=application_port_start WHERE id=$1")
        .bind(connection.id())
        .execute(&fixture.owner)
        .await
        .unwrap();
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
    sqlx::query("UPDATE pixels.instances SET state='reconcile_required' WHERE id=$1")
        .bind(first.id)
        .execute(&fixture.owner)
        .await
        .unwrap();
    sqlx::query("UPDATE pixels.instance_commands SET deadline=clock_timestamp()-interval '1 second' WHERE instance_id=$1").bind(first.id).execute(&fixture.owner).await.unwrap();
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
    assert_eq!(fixture.count(connection.id()).await, 1);
    fixture.close().await;
}
