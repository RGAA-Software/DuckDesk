use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ClientType, DevicePlatform, DeviceStore, IdentityStore, NodeConfiguration, NodeGpuTelemetry,
    NodeProduct, NodeProfile, NodeReport, NodeStore, NodeTelemetry, PasswordDigest, StoreError,
    TelemetryProbeState, TokenDigest, Username,
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
                    &SaltString::encode_b64(&[65; 16]).unwrap(),
                )
                .unwrap()
                .to_string()
        })
        .clone(),
    )
    .unwrap()
}
fn report(sequence: u64) -> NodeReport {
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
fn ready_telemetry() -> NodeTelemetry {
    NodeTelemetry {
        sampled_at: chrono::Utc::now(),
        probe_state: TelemetryProbeState::Ready,
        logical_processors: Some(16),
        cpu_utilization_per_mille: Some(375),
        memory_total_bytes: Some(64 * 1024 * 1024 * 1024),
        memory_available_bytes: Some(40 * 1024 * 1024 * 1024),
        disk_total_bytes: Some(2 * 1024 * 1024 * 1024 * 1024),
        disk_free_bytes: Some(1024 * 1024 * 1024 * 1024),
        gpu_inventory_revision: Some(7),
        gpus: vec![NodeGpuTelemetry {
            stable_key: "pnp-sha256:0123456789abcdef".into(),
            name: "Synthetic GPU".into(),
            dedicated_memory_bytes: Some(24 * 1024 * 1024 * 1024),
            used_memory_bytes: Some(8 * 1024 * 1024 * 1024),
            utilization_per_mille: Some(250),
            encoder_utilization_per_mille: Some(125),
        }],
    }
}
fn settings() -> NodeConfiguration {
    NodeConfiguration {
        draining: false,
        disabled: false,
        max_instances: 4,
    }
}
struct Fixture {
    nodes: NodeStore,
    devices: DeviceStore,
    identity: IdentityStore,
    owner: sqlx::PgPool,
    admin: TokenDigest,
}
impl Fixture {
    async fn new() -> Self {
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let nodes = NodeStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let devices = DeviceStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let identity = IdentityStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let owner = config("OWNER").connect().await.unwrap();
        let mut fixture = Self {
            nodes,
            devices,
            identity,
            owner,
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
    async fn node(&self) -> (NodeProfile, TokenDigest) {
        let device = self
            .devices
            .create(&self.admin, "节点", DevicePlatform::Windows, &token())
            .await
            .unwrap();
        let key = token();
        (
            self.nodes
                .create(&self.admin, device.id, NodeProduct::CloudNode, &key, 4)
                .await
                .unwrap(),
            key,
        )
    }
    async fn get(&self, id: Uuid) -> NodeProfile {
        // Keyset traversal deliberately covers rows left by previous tests.
        let mut after = None;
        loop {
            let rows = self
                .nodes
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
        self.nodes.close().await;
        self.devices.close().await;
        self.identity.close().await;
        self.owner.close().await;
    }
}

#[tokio::test]
async fn latest_machine_and_gpu_telemetry_is_generation_fenced_replaced_and_explicitly_unknown() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let connection = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    let mut first = report(1);
    first.telemetry = ready_telemetry();
    fixture.nodes.report(&connection, &first).await.unwrap();
    let first_history = fixture
        .nodes
        .list_telemetry_history(&fixture.admin, node.id, None, 1)
        .await
        .unwrap();
    assert_eq!(first_history.len(), 1);
    assert_eq!(first_history[0].telemetry.report_sequence, 1);
    assert_eq!(first_history[0].gpus.len(), 1);
    assert_eq!(
        first_history[0].gpus[0].stable_key,
        "pnp-sha256:0123456789abcdef"
    );
    let views = fixture
        .nodes
        .list_managed_views(&fixture.admin, None, 100)
        .await
        .unwrap();
    let view = views.iter().find(|view| view.node.id == node.id).unwrap();
    let telemetry = view.telemetry.as_ref().unwrap();
    assert_eq!(telemetry.node_generation, connection.generation());
    assert_eq!(telemetry.report_sequence, 1);
    assert_eq!(telemetry.cpu_utilization_per_mille, Some(375));
    assert_eq!(view.gpus.len(), 1);
    assert_eq!(view.gpus[0].inventory_revision, 7);
    assert_eq!(view.gpus[0].stable_key, "pnp-sha256:0123456789abcdef");

    fixture.nodes.report(&connection, &report(2)).await.unwrap();
    let history = fixture
        .nodes
        .list_telemetry_history(&fixture.admin, node.id, None, 2)
        .await
        .unwrap();
    assert_eq!(
        history
            .iter()
            .map(|sample| sample.telemetry.report_sequence)
            .collect::<Vec<_>>(),
        vec![2, 1]
    );
    assert!(history[0].gpus.is_empty());
    assert_eq!(history[1].gpus.len(), 1);
    let cursor = px_console_store::TelemetryHistoryCursor {
        received_at: history[0].telemetry.received_at,
        node_generation: history[0].telemetry.node_generation,
        report_sequence: history[0].telemetry.report_sequence,
    };
    let previous_page = fixture
        .nodes
        .list_telemetry_history(&fixture.admin, node.id, Some(cursor), 1)
        .await
        .unwrap();
    assert_eq!(previous_page.len(), 1);
    assert_eq!(previous_page[0].telemetry.report_sequence, 1);
    sqlx::query(
        "UPDATE pixels.node_telemetry_history SET received_at=clock_timestamp()-INTERVAL '8 days' \
         WHERE node_id=$1 AND node_generation=$2 AND report_sequence=1",
    )
    .bind(node.id)
    .bind(connection.generation())
    .execute(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(fixture.nodes.prune_telemetry_history().await.unwrap(), 1);
    let retained = fixture
        .nodes
        .list_telemetry_history(&fixture.admin, node.id, None, 100)
        .await
        .unwrap();
    assert_eq!(retained.len(), 1);
    assert_eq!(retained[0].telemetry.report_sequence, 2);
    let views = fixture
        .nodes
        .list_managed_views(&fixture.admin, None, 100)
        .await
        .unwrap();
    let view = views.iter().find(|view| view.node.id == node.id).unwrap();
    assert_eq!(view.telemetry.as_ref().unwrap().probe_state, "unavailable");
    assert!(view.gpus.is_empty());

    let mut invalid = report(3);
    invalid.telemetry = ready_telemetry();
    invalid.telemetry.gpus[0].used_memory_bytes = Some(25 * 1024 * 1024 * 1024);
    assert!(matches!(
        fixture.nodes.report(&connection, &invalid).await,
        Err(StoreError::InvalidInput)
    ));
    let views = fixture
        .nodes
        .list_managed_views(&fixture.admin, None, 100)
        .await
        .unwrap();
    let view = views.iter().find(|view| view.node.id == node.id).unwrap();
    assert_eq!(view.telemetry.as_ref().unwrap().report_sequence, 2);
    assert!(view.gpus.is_empty());

    let mut incomplete_memory = ready_telemetry();
    incomplete_memory.memory_total_bytes = None;
    let mut incomplete = report(3);
    incomplete.telemetry = incomplete_memory;
    assert!(matches!(
        fixture.nodes.report(&connection, &incomplete).await,
        Err(StoreError::InvalidInput)
    ));
    fixture.close().await;
}

#[tokio::test]
async fn node_credentials_and_management_roles_are_disjoint() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    assert_eq!(node.state, "offline");
    assert!(!node.fresh);
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    for session in [
        fixture.session("user", ClientType::AdminWeb).await,
        fixture.session("admin", ClientType::Android).await,
        key.clone(),
    ] {
        assert!(matches!(
            fixture.nodes.list_managed(&session, None, 100).await,
            Err(StoreError::Rejected)
        ));
        assert!(matches!(
            fixture
                .nodes
                .list_telemetry_history(&session, node.id, None, 100)
                .await,
            Err(StoreError::Rejected)
        ));
        assert!(matches!(
            fixture
                .nodes
                .configure(&session, node.id, 1, settings())
                .await,
            Err(StoreError::Rejected)
        ));
    }
    let viewer = fixture.session("viewer", ClientType::AdminWeb).await;
    assert!(!fixture
        .nodes
        .list_managed(&viewer, None, 100)
        .await
        .unwrap()
        .is_empty());
    assert!(matches!(
        fixture
            .nodes
            .rotate_key(&viewer, node.id, 1, &token())
            .await,
        Err(StoreError::Rejected)
    ));
    assert!(fixture
        .nodes
        .open_connection(epoch, &fixture.admin, &token())
        .await
        .is_err());
    assert!(fixture
        .nodes
        .create(
            &fixture.admin,
            node.device_id,
            NodeProduct::Remote,
            &token(),
            4
        )
        .await
        .is_err());
    assert!(fixture
        .nodes
        .create(
            &fixture.admin,
            Uuid::new_v4(),
            NodeProduct::Remote,
            &token(),
            4
        )
        .await
        .is_err());
    assert!(fixture
        .nodes
        .list_managed(&fixture.admin, None, 101)
        .await
        .is_err());
    fixture.close().await;
}

#[tokio::test]
async fn reconnect_and_out_of_order_reports_cannot_overwrite_current_connection() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let old = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    let first = fixture.nodes.report(&old, &report(1)).await.unwrap();
    assert!(first.fresh);
    assert_eq!(first.state, "reconciling");
    assert_eq!(first.endpoint_revision, 2);
    assert!(fixture.nodes.report(&old, &report(1)).await.is_err());
    assert!(fixture.nodes.report(&old, &report(0)).await.is_err());
    let current = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    assert!(current.generation() > old.generation());
    assert_eq!(current.id(), node.id);
    assert!(fixture.nodes.report(&old, &report(100)).await.is_err());
    assert!(fixture.nodes.close_connection(&old).await.is_err());
    let same = fixture.nodes.report(&current, &report(1)).await.unwrap();
    assert_eq!(same.endpoint_revision, first.endpoint_revision);
    let mut changed = report(2);
    changed.public_host = "other.example.test".into();
    assert_eq!(
        fixture
            .nodes
            .report(&current, &changed)
            .await
            .unwrap()
            .endpoint_revision,
        3
    );
    fixture.nodes.close_connection(&current).await.unwrap();
    assert!(!fixture.get(node.id).await.fresh);
    assert!(fixture.nodes.report(&current, &report(3)).await.is_err());
    fixture.close().await;
}

#[tokio::test]
async fn process_epoch_reset_requires_reauthentication_and_reconciliation() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let connection = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    fixture.nodes.report(&connection, &report(1)).await.unwrap();
    // Only the fixture owner may manufacture an old Ready snapshot; heartbeats cannot.
    sqlx::query("UPDATE pixels.nodes SET state='ready' WHERE id=$1")
        .bind(node.id)
        .execute(&fixture.owner)
        .await
        .unwrap();
    let next = fixture.nodes.begin_runtime().await.unwrap();
    assert!(next.value() > epoch.value());
    let snapshot = fixture.get(node.id).await;
    assert_eq!(snapshot.state, "offline");
    assert!(!snapshot.fresh);
    assert_eq!(snapshot.public_host.as_deref(), Some("node.example.test"));
    assert!(fixture.nodes.report(&connection, &report(2)).await.is_err());
    assert!(fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .is_err());
    let connection = fixture
        .nodes
        .open_connection(next, &key, &token())
        .await
        .unwrap();
    assert_eq!(
        fixture
            .nodes
            .report(&connection, &report(1))
            .await
            .unwrap()
            .state,
        "reconciling"
    );
    fixture.close().await;
}

#[tokio::test]
async fn concurrent_configuration_has_one_winner_and_rotation_fences_old_keys() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let connection = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let store = fixture.nodes.clone();
        let admin = fixture.admin.clone();
        tasks.push(tokio::spawn(async move {
            store
                .configure(
                    &admin,
                    node.id,
                    1,
                    NodeConfiguration {
                        draining: true,
                        ..settings()
                    },
                )
                .await
        }));
    }
    let mut successes = 0;
    for task in tasks {
        successes += usize::from(task.await.unwrap().is_ok());
    }
    assert_eq!(successes, 1);
    let current = fixture.nodes.report(&connection, &report(1)).await.unwrap();
    assert!(current.draining);
    assert_eq!(current.revision, 2);
    // Capacity reduction/draining must not sever existing node control connectivity.
    let small = fixture
        .nodes
        .configure(
            &fixture.admin,
            node.id,
            2,
            NodeConfiguration {
                draining: true,
                max_instances: 1,
                disabled: false,
            },
        )
        .await
        .unwrap();
    assert!(small.fresh);
    let replacement = token();
    let rotated = fixture
        .nodes
        .rotate_key(&fixture.admin, node.id, 3, &replacement)
        .await
        .unwrap();
    assert!(!rotated.fresh);
    assert!(fixture.nodes.report(&connection, &report(2)).await.is_err());
    assert!(fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .is_err());
    assert!(fixture
        .nodes
        .open_connection(epoch, &replacement, &token())
        .await
        .is_ok());
    fixture.close().await;
}

#[tokio::test]
async fn disable_enable_and_delete_never_restore_old_connection() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let old = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    fixture
        .nodes
        .configure(
            &fixture.admin,
            node.id,
            1,
            NodeConfiguration {
                disabled: true,
                ..settings()
            },
        )
        .await
        .unwrap();
    assert!(fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .is_err());
    fixture
        .nodes
        .configure(&fixture.admin, node.id, 2, settings())
        .await
        .unwrap();
    assert!(fixture.nodes.report(&old, &report(1)).await.is_err());
    assert_eq!(fixture.get(node.id).await.state, "offline");
    let new = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    fixture
        .nodes
        .delete(&fixture.admin, node.id, 3)
        .await
        .unwrap();
    assert!(fixture.nodes.report(&new, &report(1)).await.is_err());
    assert!(fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .is_err());
    let device_exists: bool = sqlx::query_scalar(
        "SELECT EXISTS(SELECT 1 FROM pixels.devices WHERE id=$1 AND deleted_at IS NULL)",
    )
    .bind(node.device_id)
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert!(device_exists);
    let audits: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.node_audit WHERE node_id=$1")
        .bind(node.id)
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    assert_eq!(audits, 4);
    fixture.close().await;
}

#[tokio::test]
async fn database_clock_expiry_and_device_revocation_deny_live_reports() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let connection = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    fixture.nodes.report(&connection, &report(1)).await.unwrap();
    sqlx::query(
        "UPDATE pixels.nodes SET last_seen=clock_timestamp()-interval '31 seconds' WHERE id=$1",
    )
    .bind(node.id)
    .execute(&fixture.owner)
    .await
    .unwrap();
    assert!(!fixture.get(node.id).await.fresh);
    assert!(
        fixture
            .nodes
            .report(&connection, &report(2))
            .await
            .unwrap()
            .fresh
    );
    fixture
        .devices
        .update(&fixture.admin, node.device_id, 1, "节点", true)
        .await
        .unwrap();
    assert!(!fixture.get(node.id).await.fresh);
    assert!(fixture.nodes.report(&connection, &report(3)).await.is_err());
    assert!(fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .is_err());
    fixture
        .devices
        .update(&fixture.admin, node.device_id, 2, "节点", false)
        .await
        .unwrap();
    assert!(fixture.nodes.report(&connection, &report(3)).await.is_err());
    let new = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    fixture
        .devices
        .delete(&fixture.admin, node.device_id, 3)
        .await
        .unwrap();
    assert!(fixture.nodes.report(&new, &report(1)).await.is_err());
    let (other, _) = fixture.node().await;
    sqlx::query("UPDATE pixels.devices SET disabled=true WHERE id=$1")
        .bind(other.device_id)
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(fixture
        .nodes
        .create(
            &fixture.admin,
            other.device_id,
            NodeProduct::Remote,
            &token(),
            4
        )
        .await
        .is_err());
    fixture.nodes.close().await;
    assert!(fixture
        .nodes
        .list_managed(&fixture.admin, None, 10)
        .await
        .is_err());
    fixture.close().await;
}

#[tokio::test]
async fn failed_audit_and_epoch_reset_roll_back_all_state() {
    let fixture = Fixture::new().await;
    let (node, key) = fixture.node().await;
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let connection = fixture
        .nodes
        .open_connection(epoch, &key, &token())
        .await
        .unwrap();
    sqlx::query("REVOKE INSERT ON pixels.node_audit FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let result = fixture
        .nodes
        .configure(
            &fixture.admin,
            node.id,
            1,
            NodeConfiguration {
                disabled: true,
                ..settings()
            },
        )
        .await;
    sqlx::query("GRANT INSERT ON pixels.node_audit TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(result.is_err());
    assert_eq!(fixture.get(node.id).await.revision, 1);
    assert!(fixture.nodes.report(&connection, &report(1)).await.is_ok());
    sqlx::query("REVOKE INSERT ON pixels.control_runs FROM pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    let reset = fixture.nodes.begin_runtime().await;
    sqlx::query("GRANT INSERT ON pixels.control_runs TO pixels_console_runtime")
        .execute(&fixture.owner)
        .await
        .unwrap();
    assert!(reset.is_err());
    assert!(fixture.nodes.report(&connection, &report(2)).await.is_ok());
    let current: i64 = sqlx::query_scalar("SELECT epoch FROM pixels.control_runtime")
        .fetch_one(&fixture.owner)
        .await
        .unwrap();
    assert_eq!(current, epoch.value());
    fixture.close().await;
}
