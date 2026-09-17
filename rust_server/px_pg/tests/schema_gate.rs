use px_pg::{DatabaseConfig, DatabaseError, Service, Transport};
use sqlx::{Connection, PgConnection, PgPool};
use std::{env, time::Duration};
use uuid::Uuid;
#[path = "../src/catalog.rs"]
mod catalog;
const GATE: i64 = 22091701;

fn deployment() -> Uuid {
    env::var("PIXELS_DEPLOYMENT_ID").unwrap().parse().unwrap()
}
fn config(service: Service, owner: bool) -> DatabaseConfig {
    assert_eq!(env::var("PIXELS_PG_ISOLATED_TEST").as_deref(), Ok("1"));
    let role = if owner { "OWNER" } else { "RUNTIME" };
    DatabaseConfig::parse(
        &env::var(format!(
            "PIXELS_TEST_{}_{}_URL",
            service.name().to_uppercase(),
            role
        ))
        .unwrap(),
        Transport::LocalDevelopment,
    )
    .unwrap()
}
async fn migrate(service: Service) -> Result<(), DatabaseError> {
    px_pg::migrate(
        &config(service, true),
        service,
        deployment(),
        catalog::migrations(service),
    )
    .await
}
async fn count(pool: &PgPool, key: i64) -> i64 {
    sqlx::query_scalar("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND granted AND classid=0 AND objid::bigint=$1 AND objsubid=1 AND database=(SELECT oid FROM pg_database WHERE datname=current_database())")
        .bind(key).fetch_one(pool).await.unwrap()
}
async fn wait_count(pool: &PgPool, key: i64, expected: i64) {
    tokio::time::timeout(Duration::from_secs(10), async {
        while count(pool, key).await != expected {
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    })
    .await
    .expect("exact schema-lock boundary not reached");
}
struct Child(std::process::Child);
impl Child {
    fn start(executable: &str) -> Self {
        Self(
            std::process::Command::new(executable)
                .stdin(std::process::Stdio::null())
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .spawn()
                .unwrap(),
        )
    }
    async fn success(&mut self) {
        tokio::time::timeout(Duration::from_secs(15), async {
            loop {
                if let Some(status) = self.0.try_wait().unwrap() {
                    assert!(status.success());
                    break;
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
        })
        .await
        .expect("owned child exceeded deadline");
    }
}
impl Drop for Child {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

#[tokio::test]
async fn every_service_and_every_pooled_backend_pins_schema_without_blocking_replicas() {
    for service in [Service::Console, Service::Auth, Service::Desk] {
        let settings = config(service, false)
            .with_pool_limits(2, Duration::from_secs(2))
            .unwrap();
        let first = settings
            .connect_runtime(service, deployment(), catalog::migrations(service))
            .await
            .unwrap();
        let second = settings
            .connect_runtime(service, deployment(), catalog::migrations(service))
            .await
            .unwrap();
        let one = first.acquire().await.unwrap();
        let two = first.acquire().await.unwrap();
        let observer = config(service, true).connect().await.unwrap();
        assert_eq!(count(&observer, GATE).await, 3);
        let bootstrap = config(service, true)
            .connect_bootstrap(service, deployment(), catalog::migrations(service))
            .await
            .unwrap();
        assert_eq!(count(&observer, GATE).await, 4);
        assert_eq!(migrate(service).await, Err(DatabaseError::Conflict));
        drop((one, two));
        first.close().await;
        assert_eq!(count(&observer, GATE).await, 2);
        assert_eq!(migrate(service).await, Err(DatabaseError::Conflict));
        second.close().await;
        assert_eq!(count(&observer, GATE).await, 1);
        assert_eq!(migrate(service).await, Err(DatabaseError::Conflict));
        bootstrap.close().await;
        wait_count(&observer, GATE, 0).await;
        migrate(service).await.unwrap();
        observer.close().await;
    }
}

#[tokio::test]
async fn real_migrator_blocks_startup_and_reconnect_then_old_schema_is_rejected() {
    let service = Service::Desk;
    let settings = config(service, false)
        .with_pool_limits(1, Duration::from_millis(300))
        .unwrap();
    let pool = settings
        .connect_runtime(service, deployment(), catalog::migrations(service))
        .await
        .unwrap();
    let pid: i32 = sqlx::query_scalar("SELECT pg_backend_pid()")
        .fetch_one(&pool)
        .await
        .unwrap();
    let killer = config(service, false).connect().await.unwrap();
    let killed: bool = sqlx::query_scalar("SELECT pg_terminate_backend($1,1000)")
        .bind(pid)
        .fetch_one(&killer)
        .await
        .unwrap();
    assert!(killed);
    let owner = config(service, true).connect().await.unwrap();
    wait_count(&owner, GATE, 0).await;
    let mut barrier = PgConnection::connect(&env::var("PIXELS_TEST_DESK_OWNER_URL").unwrap())
        .await
        .unwrap();
    sqlx::query("SELECT pg_advisory_lock(912340567)")
        .execute(&mut barrier)
        .await
        .unwrap();
    let mut child = Child::start(env!("CARGO_BIN_EXE_px_pg_fault"));
    tokio::time::timeout(Duration::from_secs(10),async {
        loop {
            assert!(child.0.try_wait().unwrap().is_none());
            let waiting:i64=sqlx::query_scalar("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND NOT granted AND objid=912340567 AND database=(SELECT oid FROM pg_database WHERE datname=current_database())").fetch_one(&owner).await.unwrap();
            if waiting==1 {break;}
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    }).await.unwrap();
    assert_eq!(count(&owner, GATE).await, 1);
    assert!(matches!(
        settings
            .connect_runtime(service, deployment(), catalog::migrations(service))
            .await,
        Err(DatabaseError::Conflict)
    ));
    assert!(sqlx::query("SELECT 1").execute(&pool).await.is_err());
    sqlx::query("SELECT pg_advisory_unlock(912340567)")
        .execute(&mut barrier)
        .await
        .unwrap();
    child.success().await;
    // Old pool cannot auto-reconnect after the new schema commits, even though its process survived.
    assert!(sqlx::query("SELECT 1").execute(&pool).await.is_err());
    assert!(matches!(
        settings
            .connect_runtime(service, deployment(), catalog::migrations(service))
            .await,
        Err(DatabaseError::Schema)
    ));
    pool.close().await;
    let probe_version = catalog::migrations(service)
        .iter()
        .map(|migration| migration.version)
        .max()
        .unwrap()
        + 1;
    // Remove only this test's synthetic schema in the isolated database, not product data.
    let mut tx = owner.begin().await.unwrap();
    sqlx::query("DROP TABLE pixels.crash_probe")
        .execute(&mut *tx)
        .await
        .unwrap();
    assert_eq!(
        sqlx::query("DELETE FROM pixels._sqlx_migrations WHERE version=$1")
            .bind(probe_version)
            .execute(&mut *tx)
            .await
            .unwrap()
            .rows_affected(),
        1
    );
    tx.commit().await.unwrap();
    migrate(service).await.unwrap();
    barrier.close().await.unwrap();
    killer.close().await;
    owner.close().await;
}

#[tokio::test]
async fn killed_runtime_process_releases_schema_pins_without_manual_unlock() {
    let observer = config(Service::Desk, true).connect().await.unwrap();
    let child = Child::start(env!("CARGO_BIN_EXE_px_pg_schema_probe"));
    wait_count(&observer, 912340569, 1).await;
    assert_eq!(count(&observer, GATE).await, 1);
    assert_eq!(migrate(Service::Desk).await, Err(DatabaseError::Conflict));
    drop(child);
    wait_count(&observer, GATE, 0).await;
    wait_count(&observer, 912340569, 0).await;
    migrate(Service::Desk).await.unwrap();
    observer.close().await;
}

#[tokio::test]
async fn rejected_or_cancelled_startup_never_leaks_schema_locks() {
    let service = Service::Desk;
    let observer = config(service, true).connect().await.unwrap();
    assert!(matches!(
        config(service, false)
            .connect_bootstrap(service, deployment(), catalog::migrations(service))
            .await,
        Err(DatabaseError::Permission)
    ));
    assert!(matches!(
        config(service, true)
            .connect_runtime(service, deployment(), catalog::migrations(service))
            .await,
        Err(DatabaseError::Permission)
    ));
    assert!(matches!(
        config(service, false)
            .connect_runtime(service, Uuid::new_v4(), catalog::migrations(service))
            .await,
        Err(DatabaseError::Identity)
    ));
    wait_count(&observer, GATE, 0).await;
    let mut tx = observer.begin().await.unwrap();
    sqlx::query("LOCK TABLE pixels._sqlx_migrations IN ACCESS EXCLUSIVE MODE")
        .execute(&mut *tx)
        .await
        .unwrap();
    let startup = tokio::spawn(async move {
        config(service, false)
            .connect_runtime(service, deployment(), catalog::migrations(service))
            .await
    });
    wait_count(&observer, GATE, 1).await;
    startup.abort();
    assert!(startup.await.unwrap_err().is_cancelled());
    tx.rollback().await.unwrap();
    wait_count(&observer, GATE, 0).await;
    migrate(service).await.unwrap();
    observer.close().await;
}
