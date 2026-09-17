#![allow(dead_code, unused_imports)] // Shared isolated PG fixture; suites use different capabilities.
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

pub fn config(role: &str) -> DatabaseConfig {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    DatabaseConfig::parse(
        &env::var(format!("PIXELS_TEST_CONSOLE_{role}_URL")).unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
}
pub fn token() -> TokenDigest {
    let mut bytes = [0; 32];
    bytes[..16].copy_from_slice(Uuid::new_v4().as_bytes());
    bytes[16..].copy_from_slice(Uuid::new_v4().as_bytes());
    TokenDigest::from_sha256(bytes)
}
pub fn password() -> PasswordDigest {
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
pub fn settings(target: DeploymentTarget) -> DeploymentConfiguration {
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
pub fn node_report(sequence: u64) -> NodeReport {
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
pub fn observation(deployment: &DeploymentProfile, sequence: u64) -> DeploymentObservation {
    DeploymentObservation {
        deployment_revision: deployment.revision,
        application_revision: deployment.application_revision,
        endpoint_revision: 2,
        sequence,
        status: PreparationState::Ready,
    }
}
pub struct Fixture {
    pub instances: InstanceStore,
    pub guests: GuestStore,
    pub deployments: DeploymentStore,
    pub apps: ApplicationStore,
    pub nodes: NodeStore,
    pub devices: DeviceStore,
    pub identity: IdentityStore,
    pub owner: sqlx::PgPool,
    pub admin: TokenDigest,
}
impl Fixture {
    pub async fn new() -> Self {
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
    pub async fn session(&self, role: &str, client: ClientType) -> TokenDigest {
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
    pub async fn node(&self) -> (Uuid, TokenDigest) {
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
    pub async fn app(&self, target: &DeploymentTarget) -> ApplicationDefinition {
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
    pub async fn connected(&self) -> (NodeConnection, TokenDigest) {
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
    pub async fn deployment(
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
    pub async fn get(&self, id: Uuid) -> DeploymentProfile {
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
    pub async fn prepared(
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
    pub async fn count(&self, node: Uuid) -> i64 {
        sqlx::query_scalar("SELECT count(*) FROM pixels.instances WHERE node_id=$1")
            .bind(node)
            .fetch_one(&self.owner)
            .await
            .unwrap()
    }
    pub async fn guest(&self) -> (TokenDigest, Uuid) {
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
    pub async fn started(
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
    pub async fn close(self) {
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
pub fn request(app: Uuid) -> StartApplication {
    StartApplication {
        request_id: Uuid::new_v4(),
        application_id: app,
        deployment_id: None,
    }
}
