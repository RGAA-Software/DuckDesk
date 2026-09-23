use argon2::{
    password_hash::{PasswordHasher, SaltString},
    Argon2,
};
use px_console_store::{
    ClientType, IdentityStore, NodeStore, PasswordDigest, RelayNodeConfiguration, RelayNodeProfile,
    RelayNodeReport, RelayNodeSpec, RelayNodeStore, StoreError, TokenDigest, Username,
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
                    &SaltString::encode_b64(&[67; 16]).unwrap(),
                )
                .unwrap()
                .to_string()
        })
        .clone(),
    )
    .unwrap()
}

fn relay_spec() -> RelayNodeSpec {
    let relay_identity = Uuid::new_v4().simple().to_string();
    RelayNodeSpec {
        name: format!("relay-{relay_identity}"),
        public_host: format!("relay-{relay_identity}.example.test"),
        public_port: 4602,
    }
}

fn report(sequence: u64, draining: bool) -> RelayNodeReport {
    RelayNodeReport {
        sequence,
        product_version_code: 300_079,
        draining,
        max_connections: 4_096,
        current_connections: 8,
        max_rooms: 2_048,
        current_rooms: 4,
        uploaded_bytes: 1_024,
        forwarded_bytes: 2_048,
    }
}

struct Fixture {
    identity: IdentityStore,
    nodes: NodeStore,
    relay_nodes: RelayNodeStore,
    owner: sqlx::PgPool,
    admin: TokenDigest,
}

impl Fixture {
    async fn new() -> Self {
        let deployment = env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap();
        let identity = IdentityStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let nodes = NodeStore::connect(&config("RUNTIME"), deployment)
            .await
            .unwrap();
        let relay_nodes = RelayNodeStore::connect(&config("RUNTIME"), deployment)
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
                Duration::from_secs(3_600),
            )
            .await
            .unwrap();
        Self {
            identity,
            nodes,
            relay_nodes,
            owner,
            admin,
        }
    }

    async fn create_relay(&self) -> (RelayNodeProfile, TokenDigest) {
        let credential = token();
        let relay_node = self
            .relay_nodes
            .create(&self.admin, &relay_spec(), &credential)
            .await
            .unwrap();
        (relay_node, credential)
    }

    async fn get(&self, relay_node_id: Uuid) -> RelayNodeProfile {
        let mut after = None;
        loop {
            let relay_nodes = self
                .relay_nodes
                .list_managed(&self.admin, after, 100)
                .await
                .unwrap();
            assert!(!relay_nodes.is_empty());
            if let Some(relay_node) = relay_nodes
                .iter()
                .find(|relay_node| relay_node.id == relay_node_id)
            {
                return relay_node.clone();
            }
            after = relay_nodes.last().map(|relay_node| relay_node.id);
        }
    }

    async fn close(self) {
        self.relay_nodes.close().await;
        self.nodes.close().await;
        self.identity.close().await;
        self.owner.close().await;
    }
}

#[tokio::test]
async fn relay_inventory_starts_draining_and_never_exposes_the_credential() {
    let fixture = Fixture::new().await;
    let (relay_node, _) = fixture.create_relay().await;
    assert_eq!(relay_node.state, "offline");
    assert!(relay_node.desired_draining);
    assert!(!relay_node.fresh);
    assert_eq!(relay_node.revision, 1);
    assert!(relay_node.reported_draining.is_none());
    let listed = fixture.get(relay_node.id).await;
    assert_eq!(listed, relay_node);
    let credential_columns: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM information_schema.columns WHERE table_schema='pixels' AND table_name='relay_nodes' AND column_name IN ('credential','credential_token')",
    )
    .fetch_one(&fixture.owner)
    .await
    .unwrap();
    assert_eq!(credential_columns, 0);
    fixture.close().await;
}

#[tokio::test]
async fn authenticated_reports_are_ordered_bounded_and_fenced_by_connection_generation() {
    let fixture = Fixture::new().await;
    let (relay_node, credential) = fixture.create_relay().await;
    let configured = fixture
        .relay_nodes
        .configure(
            &fixture.admin,
            relay_node.id,
            relay_node.revision,
            RelayNodeConfiguration {
                draining: false,
                disabled: false,
            },
        )
        .await
        .unwrap();
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    assert!(fixture
        .relay_nodes
        .open_connection(epoch, &token(), &token())
        .await
        .is_err());
    let first_connection = fixture
        .relay_nodes
        .open_connection(epoch, &credential, &token())
        .await
        .unwrap();
    let ready = fixture
        .relay_nodes
        .report(&first_connection, &report(1, false))
        .await
        .unwrap();
    assert_eq!(ready.state, "ready");
    assert_eq!(ready.current_connections, Some(8));
    assert_eq!(ready.current_rooms, Some(4));
    assert_eq!(ready.reported_draining, Some(false));
    assert!(ready.fresh);
    assert!(fixture
        .relay_nodes
        .report(&first_connection, &report(1, false))
        .await
        .is_err());
    let mut excessive_connections = report(2, false);
    excessive_connections.current_connections = excessive_connections.max_connections + 1;
    assert_eq!(
        fixture
            .relay_nodes
            .report(&first_connection, &excessive_connections)
            .await
            .unwrap_err(),
        StoreError::InvalidInput
    );
    let replacement = fixture
        .relay_nodes
        .open_connection(epoch, &credential, &token())
        .await
        .unwrap();
    assert!(fixture
        .relay_nodes
        .report(&first_connection, &report(2, false))
        .await
        .is_err());
    assert!(fixture
        .relay_nodes
        .close_connection(&first_connection)
        .await
        .is_err());
    fixture
        .relay_nodes
        .report(&replacement, &report(1, false))
        .await
        .unwrap();
    fixture
        .relay_nodes
        .close_connection(&replacement)
        .await
        .unwrap();
    assert_eq!(fixture.get(configured.id).await.state, "offline");
    fixture.close().await;
}

#[tokio::test]
async fn disable_is_revision_checked_and_immediately_invalidates_the_active_connection() {
    let fixture = Fixture::new().await;
    let (relay_node, credential) = fixture.create_relay().await;
    let epoch = fixture.nodes.begin_runtime().await.unwrap();
    let connection = fixture
        .relay_nodes
        .open_connection(epoch, &credential, &token())
        .await
        .unwrap();
    fixture
        .relay_nodes
        .report(&connection, &report(1, true))
        .await
        .unwrap();
    let disabled = fixture
        .relay_nodes
        .configure(
            &fixture.admin,
            relay_node.id,
            relay_node.revision,
            RelayNodeConfiguration {
                draining: true,
                disabled: true,
            },
        )
        .await
        .unwrap();
    assert!(disabled.disabled);
    assert_eq!(disabled.state, "offline");
    assert!(!disabled.fresh);
    assert!(fixture
        .relay_nodes
        .report(&connection, &report(2, true))
        .await
        .is_err());
    assert!(fixture
        .relay_nodes
        .configure(
            &fixture.admin,
            relay_node.id,
            relay_node.revision,
            RelayNodeConfiguration {
                draining: false,
                disabled: false,
            },
        )
        .await
        .is_err());
    fixture.close().await;
}
