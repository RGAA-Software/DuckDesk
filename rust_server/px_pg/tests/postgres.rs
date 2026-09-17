use px_pg::{DatabaseConfig, DatabaseError, Service, Transport};
use sqlx::{migrate::Migrate, Connection, PgConnection, PgPool};
use std::{env, time::Duration};
use uuid::Uuid;

#[tokio::test]
async fn business_readiness_rejects_owner_and_broadened_runtime_privileges() {
    for service in [Service::Console, Service::Auth, Service::Desk] {
        let owner = pool(service, true).await;
        let runtime = pool(service, false).await;
        let migrations = catalog::migrations(service);
        assert_eq!(
            px_pg::runtime_readiness(&owner, service, deployment(), migrations).await,
            Err(DatabaseError::Permission)
        );
        px_pg::runtime_readiness(&runtime, service, deployment(), migrations)
            .await
            .unwrap();
        let role = format!("pixels_{}_runtime", service.name());
        sqlx::query(&format!("GRANT CREATE ON SCHEMA pixels TO {role}"))
            .execute(&owner)
            .await
            .unwrap();
        let broadened = px_pg::runtime_readiness(&runtime, service, deployment(), migrations).await;
        sqlx::query(&format!("REVOKE CREATE ON SCHEMA pixels FROM {role}"))
            .execute(&owner)
            .await
            .unwrap();
        assert_eq!(broadened, Err(DatabaseError::Permission));
        px_pg::runtime_readiness(&runtime, service, deployment(), migrations)
            .await
            .unwrap();
        runtime.close().await;
        owner.close().await;
    }
}

#[path = "../src/catalog.rs"]
mod catalog;

async fn readiness(pool: &PgPool, service: Service, deployment: Uuid) -> Result<(), DatabaseError> {
    px_pg::readiness(pool, service, deployment, catalog::migrations(service)).await
}

async fn migrate(
    config: &DatabaseConfig,
    service: Service,
    deployment: Uuid,
) -> Result<(), DatabaseError> {
    px_pg::migrate(config, service, deployment, catalog::migrations(service)).await
}

fn deployment() -> Uuid {
    Uuid::parse_str(&env::var("PIXELS_DEPLOYMENT_ID").expect("required test deployment")).unwrap()
}

fn config(service: Service, owner: bool) -> DatabaseConfig {
    assert_eq!(
        env::var("PIXELS_PG_ISOLATED_TEST").as_deref(),
        Ok("1"),
        "run the isolated test harness"
    );
    let role = if owner { "OWNER" } else { "RUNTIME" };
    let key = format!("PIXELS_TEST_{}_{}_URL", service.name().to_uppercase(), role);
    let dsn = env::var(key).expect("required test DSN; missing database is a failure, not a skip");
    DatabaseConfig::parse(&dsn, Transport::LocalDevelopment).unwrap()
}

async fn pool(service: Service, owner: bool) -> PgPool {
    let pool = config(service, owner).connect().await.unwrap();
    readiness(&pool, service, deployment()).await.unwrap();
    pool
}

#[tokio::test]
async fn all_services_ready_and_wrong_identity_rejected() {
    for service in [Service::Console, Service::Auth, Service::Desk] {
        let pool = pool(service, false).await;
        assert_eq!(
            readiness(&pool, service, Uuid::new_v4()).await,
            Err(DatabaseError::Identity)
        );
        let other = if service == Service::Desk {
            Service::Auth
        } else {
            Service::Desk
        };
        assert_eq!(
            readiness(&pool, other, deployment()).await,
            Err(DatabaseError::Identity)
        );
        pool.close().await;
    }
}

#[tokio::test]
async fn runtime_cannot_migrate_ddl_or_modify_identity_and_ledger() {
    for service in [Service::Console, Service::Auth, Service::Desk] {
        let pool = pool(service, false).await;
        for sql in [
            "CREATE TABLE pixels.unauthorized(id integer)",
            "CREATE SCHEMA unauthorized",
            "CREATE TEMP TABLE unauthorized(id integer)",
            "DELETE FROM pixels.deployment_identity",
            "UPDATE pixels._sqlx_migrations SET success=false",
            "CREATE ROLE unauthorized SUPERUSER",
        ] {
            let error = sqlx::query(sql).execute(&pool).await.unwrap_err();
            assert_eq!(DatabaseError::from(error), DatabaseError::Permission);
        }
        assert_eq!(
            migrate(&config(service, false), service, deployment()).await,
            Err(DatabaseError::Permission)
        );
        readiness(&pool, service, deployment()).await.unwrap();
        pool.close().await;
    }
}

#[tokio::test]
async fn recovery_security_watermarks_advance_for_every_business_table_and_are_runtime_read_only() {
    for service in [Service::Console, Service::Auth, Service::Desk] {
        let owner = pool(service, true).await;
        let runtime = pool(service, false).await;
        let (recovery_generation, sequence_before): (Uuid, i64) = sqlx::query_as(
            "SELECT recovery_generation,security_sequence FROM pixels.recovery_security_state WHERE singleton",
        )
        .fetch_one(&runtime)
        .await
        .unwrap();
        assert!(!recovery_generation.is_nil());
        assert!(sequence_before > 0);

        let protected_table_count: i64 = sqlx::query_scalar(
            "SELECT count(*) FROM information_schema.tables WHERE table_schema='pixels' AND table_type='BASE TABLE' AND table_name NOT IN ('_sqlx_migrations','deployment_identity','recovery_security_state','pg_fixture')",
        )
        .fetch_one(&owner)
        .await
        .unwrap();
        let trigger_count: i64 = sqlx::query_scalar(
            "SELECT count(*) FROM pg_catalog.pg_trigger AS trigger_record JOIN pg_catalog.pg_class AS table_record ON table_record.oid=trigger_record.tgrelid JOIN pg_catalog.pg_namespace AS schema_record ON schema_record.oid=table_record.relnamespace WHERE schema_record.nspname='pixels' AND trigger_record.tgname='recovery_security_advance' AND NOT trigger_record.tgisinternal",
        )
        .fetch_one(&owner)
        .await
        .unwrap();
        assert_eq!(trigger_count, protected_table_count);

        let business_statement = match service {
            Service::Console => "UPDATE pixels.users SET username=username WHERE FALSE",
            Service::Auth => "UPDATE pixels.customers SET name=name WHERE FALSE",
            Service::Desk => "UPDATE pixels.feedback SET title=title WHERE FALSE",
        };
        let write_barrier_id = Uuid::new_v4();
        sqlx::query(
            "UPDATE pixels.recovery_security_state SET write_barrier_id=$1,write_barrier_expires_at=CURRENT_TIMESTAMP+INTERVAL '1 minute',write_gate_token_sha256=$2 WHERE singleton",
        )
        .bind(write_barrier_id)
        .bind("a".repeat(64))
        .execute(&owner)
        .await
        .unwrap();
        let blocked_write = sqlx::query(business_statement)
            .execute(&owner)
            .await
            .unwrap_err();
        assert_eq!(
            DatabaseError::from(blocked_write),
            DatabaseError::WriteBarrier
        );
        let sequence_while_blocked: i64 = sqlx::query_scalar(
            "SELECT security_sequence FROM pixels.recovery_security_state WHERE singleton",
        )
        .fetch_one(&runtime)
        .await
        .unwrap();
        assert_eq!(sequence_while_blocked, sequence_before);
        sqlx::query(
            "UPDATE pixels.recovery_security_state SET write_barrier_id=NULL,write_barrier_expires_at=NULL,write_gate_token_sha256=NULL WHERE singleton AND write_barrier_id=$1",
        )
        .bind(write_barrier_id)
        .execute(&owner)
        .await
        .unwrap();
        sqlx::query(business_statement)
            .execute(&owner)
            .await
            .unwrap();
        let sequence_after: i64 = sqlx::query_scalar(
            "SELECT security_sequence FROM pixels.recovery_security_state WHERE singleton",
        )
        .fetch_one(&runtime)
        .await
        .unwrap();
        assert_eq!(sequence_after, sequence_before + 1);

        let runtime_mutation = sqlx::query(
            "UPDATE pixels.recovery_security_state SET security_sequence=security_sequence+1 WHERE singleton",
        )
        .execute(&runtime)
        .await
        .unwrap_err();
        assert_eq!(
            DatabaseError::from(runtime_mutation),
            DatabaseError::Permission
        );
        runtime.close().await;
        owner.close().await;
    }
}

#[tokio::test]
async fn roles_cannot_connect_to_other_service_databases() {
    for source in [Service::Console, Service::Auth, Service::Desk] {
        for target in [Service::Console, Service::Auth, Service::Desk] {
            if source == target {
                continue;
            }
            for role in ["OWNER", "RUNTIME"] {
                let dsn = env::var(format!(
                    "PIXELS_TEST_{}_{}_URL",
                    source.name().to_uppercase(),
                    role
                ))
                .unwrap();
                let mut url = url::Url::parse(&dsn).unwrap();
                url.set_path(&format!("/pixels_{}", target.name()));
                let config =
                    DatabaseConfig::parse(url.as_str(), Transport::LocalDevelopment).unwrap();
                let error = config.connect().await.unwrap_err();
                assert_eq!(error, DatabaseError::Permission);
            }
        }
    }
}

#[tokio::test]
async fn transactions_commit_rollback_and_unique_conflicts_are_real() {
    let pool = pool(Service::Desk, false).await;
    let id = Uuid::new_v4();
    let mut tx = pool.begin().await.unwrap();
    sqlx::query(
        "INSERT INTO pixels.pg_fixture(id,version,created_at) VALUES($1,$2,to_timestamp($3))",
    )
    .bind(id)
    .bind("test-version")
    .bind(1700000000_f64)
    .execute(&mut *tx)
    .await
    .unwrap();
    let before: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.pg_fixture WHERE id=$1")
        .bind(id)
        .fetch_one(&pool)
        .await
        .unwrap();
    assert_eq!(
        before, 0,
        "independent connection must not observe uncommitted data"
    );
    tx.commit().await.unwrap();
    let value: (String, i64) = sqlx::query_as(
        "SELECT version,extract(epoch FROM created_at)::bigint FROM pixels.pg_fixture WHERE id=$1",
    )
    .bind(id)
    .fetch_one(&pool)
    .await
    .unwrap();
    assert_eq!(value, ("test-version".into(), 1700000000));
    let conflict = sqlx::query("INSERT INTO pixels.pg_fixture(id,version) VALUES($1,'duplicate')")
        .bind(id)
        .execute(&pool)
        .await
        .unwrap_err();
    assert_eq!(DatabaseError::from(conflict), DatabaseError::Conflict);
    let mut tx = pool.begin().await.unwrap();
    sqlx::query("UPDATE pixels.pg_fixture SET version='rollback' WHERE id=$1")
        .bind(id)
        .execute(&mut *tx)
        .await
        .unwrap();
    tx.rollback().await.unwrap();
    let value: String = sqlx::query_scalar("SELECT version FROM pixels.pg_fixture WHERE id=$1")
        .bind(id)
        .fetch_one(&pool)
        .await
        .unwrap();
    assert_eq!(value, "test-version");
    pool.close().await;
}

#[tokio::test]
async fn pool_exhaustion_is_bounded_and_recovers() {
    let config = config(Service::Desk, false)
        .with_pool_limits(1, Duration::from_millis(250))
        .unwrap();
    let pool = config.connect().await.unwrap();
    let held = pool.acquire().await.unwrap();
    let result = tokio::time::timeout(Duration::from_secs(3), pool.acquire())
        .await
        .unwrap();
    assert_eq!(
        DatabaseError::from(result.unwrap_err()),
        DatabaseError::Unavailable
    );
    drop(held);
    readiness(&pool, Service::Desk, deployment()).await.unwrap();
    pool.close().await;
}

#[tokio::test]
async fn invalid_password_is_redacted() {
    let original = env::var("PIXELS_TEST_DESK_RUNTIME_URL").unwrap();
    let mut url = url::Url::parse(&original).unwrap();
    url.set_password(Some("intentional-invalid-test-password"))
        .unwrap();
    let config = DatabaseConfig::parse(url.as_str(), Transport::LocalDevelopment).unwrap();
    let error = config.connect().await.unwrap_err();
    assert_eq!(error, DatabaseError::Permission);
    assert!(!format!("{error:?} {error}").contains("password"));
}

#[tokio::test]
async fn concurrent_migrators_are_repeatable_and_serialized() {
    let first_config = config(Service::Desk, true);
    let second_config = first_config.clone();
    for _ in 0..100 {
        let (result_a, result_b) = tokio::join!(
            migrate(&first_config, Service::Desk, deployment()),
            migrate(&second_config, Service::Desk, deployment())
        );
        result_a.unwrap();
        result_b.unwrap();
    }
    let owner = pool(Service::Desk, true).await;
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels._sqlx_migrations")
        .fetch_one(&owner)
        .await
        .unwrap();
    assert_eq!(
        count as usize,
        catalog::migrations(Service::Desk).iter().count()
    );
    owner.close().await;
}

#[tokio::test]
async fn held_migration_lock_has_a_bounded_failure_and_releases() {
    let dsn = env::var("PIXELS_TEST_DESK_OWNER_URL").unwrap();
    let mut blocker = PgConnection::connect(&dsn).await.unwrap();
    blocker.lock().await.unwrap();
    let result = tokio::time::timeout(
        Duration::from_secs(10),
        migrate(&config(Service::Desk, true), Service::Desk, deployment()),
    )
    .await
    .unwrap();
    assert_eq!(result, Err(DatabaseError::Unavailable));
    blocker.unlock().await.unwrap();
    blocker.close().await.unwrap();
    migrate(&config(Service::Desk, true), Service::Desk, deployment())
        .await
        .unwrap();
}

#[tokio::test]
async fn altered_dirty_missing_or_future_schema_is_not_ready() {
    let owner = pool(Service::Desk, true).await;
    let runtime = pool(Service::Desk, false).await;
    let baseline: Vec<(i64, bool, Vec<u8>)> = sqlx::query_as(
        "SELECT version,success,checksum FROM pixels._sqlx_migrations ORDER BY version",
    )
    .fetch_all(&owner)
    .await
    .unwrap();
    assert!(
        baseline.len() >= 2,
        "exercise restoration with multiple migrations"
    );
    let original: Vec<u8> =
        sqlx::query_scalar("SELECT checksum FROM pixels._sqlx_migrations WHERE version=1")
            .fetch_one(&owner)
            .await
            .unwrap();
    for sql in [
        "UPDATE pixels._sqlx_migrations SET checksum=decode('00','hex') WHERE version=1",
        "UPDATE pixels._sqlx_migrations SET success=false WHERE version=1",
        "UPDATE pixels._sqlx_migrations SET version=999 WHERE version=1",
    ] {
        sqlx::query(sql).execute(&owner).await.unwrap();
        let result = readiness(&runtime, Service::Desk, deployment()).await;
        let migration_result =
            migrate(&config(Service::Desk, true), Service::Desk, deployment()).await;
        sqlx::query("UPDATE pixels._sqlx_migrations SET version=1, success=true, checksum=$1 WHERE version IN (1,999)")
            .bind(&original)
            .execute(&owner)
            .await
            .unwrap();
        let restored: Vec<(i64, bool, Vec<u8>)> = sqlx::query_as(
            "SELECT version,success,checksum FROM pixels._sqlx_migrations ORDER BY version",
        )
        .fetch_all(&owner)
        .await
        .unwrap();
        assert_eq!(
            restored, baseline,
            "unrelated migration records must remain intact"
        );
        assert_eq!(result, Err(DatabaseError::Schema));
        assert_eq!(migration_result, Err(DatabaseError::Schema));
    }
    sqlx::query("ALTER TABLE pixels._sqlx_migrations RENAME TO saved_migrations")
        .execute(&owner)
        .await
        .unwrap();
    let result = readiness(&runtime, Service::Desk, deployment()).await;
    sqlx::query("ALTER TABLE pixels.saved_migrations RENAME TO _sqlx_migrations")
        .execute(&owner)
        .await
        .unwrap();
    assert_eq!(result, Err(DatabaseError::Schema));
    readiness(&runtime, Service::Desk, deployment())
        .await
        .unwrap();
    owner.close().await;
    runtime.close().await;
}

#[tokio::test]
async fn competing_unique_inserts_have_one_winner() {
    let pool = pool(Service::Desk, false).await;
    let id = Uuid::new_v4();
    let barrier = std::sync::Arc::new(tokio::sync::Barrier::new(20));
    let mut tasks = Vec::new();
    for _ in 0..20 {
        let pool = pool.clone();
        let barrier = barrier.clone();
        tasks.push(tokio::spawn(async move {
            barrier.wait().await;
            sqlx::query("INSERT INTO pixels.pg_fixture(id,version) VALUES($1,'race')")
                .bind(id)
                .execute(&pool)
                .await
                .map(|_| ())
                .map_err(DatabaseError::from)
        }));
    }
    let mut successes = 0;
    for task in tasks {
        match task.await.unwrap() {
            Ok(()) => successes += 1,
            Err(database_error) => assert_eq!(database_error, DatabaseError::Conflict),
        }
    }
    assert_eq!(successes, 1);
    let count: i64 = sqlx::query_scalar("SELECT count(*) FROM pixels.pg_fixture WHERE id=$1")
        .bind(id)
        .fetch_one(&pool)
        .await
        .unwrap();
    assert_eq!(count, 1);
    pool.close().await;
}

#[tokio::test]
async fn new_identity_tables_enforce_constraints_and_store_no_plaintext_password() {
    let console = pool(Service::Console, false).await;
    let id = Uuid::new_v4();
    let username = format!("test-{id}");
    let digest = px_credentials::hash("synthetic database test password").unwrap();
    sqlx::query("INSERT INTO pixels.users(id,username,username_normalized,password_hash) VALUES($1,$2,$2,$3)")
        .bind(id).bind(&username).bind(digest.as_str()).execute(&console).await.unwrap();
    let duplicate = sqlx::query("INSERT INTO pixels.users(id,username,username_normalized,password_hash) VALUES($1,$2,$2,$3)")
        .bind(Uuid::new_v4()).bind(&username).bind(digest.as_str()).execute(&console).await.unwrap_err();
    assert_eq!(DatabaseError::from(duplicate), DatabaseError::Conflict);
    let revision = sqlx::query("UPDATE pixels.users SET authorization_revision=0 WHERE id=$1")
        .bind(id)
        .execute(&console)
        .await
        .unwrap_err();
    assert_eq!(DatabaseError::from(revision), DatabaseError::Conflict);
    let before: i64 =
        sqlx::query_scalar("SELECT authorization_revision FROM pixels.users WHERE id=$1")
            .bind(id)
            .fetch_one(&console)
            .await
            .unwrap();
    assert_eq!(before, 1);
    let legacy_secrets: i64 = sqlx::query_scalar("SELECT count(*) FROM information_schema.columns WHERE table_schema='pixels' AND table_name='users' AND column_name IN ('password', 'password_ciphertext')")
        .fetch_one(&console).await.unwrap();
    assert_eq!(legacy_secrets, 0);
    let auth = pool(Service::Auth, false).await;
    sqlx::query("INSERT INTO pixels.authors(id,username_normalized,password_hash,role) VALUES($1,$2,$3,'admin')")
        .bind(id).bind(&username).bind(digest.as_str()).execute(&auth).await.unwrap();
    let invalid = sqlx::query("UPDATE pixels.authors SET role='unrecognized-role' WHERE id=$1")
        .bind(id)
        .execute(&auth)
        .await
        .unwrap_err();
    assert_eq!(DatabaseError::from(invalid), DatabaseError::Conflict);
    for (table, field, pool) in [
        ("users", "username", &console),
        ("users", "username_normalized", &console),
        ("authors", "username_normalized", &auth),
    ] {
        for name in ["x".to_owned(), "x".repeat(65), "中".repeat(65)] {
            let error = sqlx::query(&format!("UPDATE pixels.{table} SET {field}=$1 WHERE id=$2"))
                .bind(name)
                .bind(id)
                .execute(pool)
                .await
                .unwrap_err();
            assert_eq!(DatabaseError::from(error), DatabaseError::Conflict);
        }
        // The limit counts Unicode scalar values, not UTF-8 bytes.
        // Include this run's UUID so Windows and Linux can exercise the same isolated
        // database sequentially without colliding with each other's boundary fixture.
        let boundary_name = format!("{}{}", id.simple(), "中".repeat(32));
        assert_eq!(boundary_name.chars().count(), 64);
        sqlx::query(&format!("UPDATE pixels.{table} SET {field}=$1 WHERE id=$2"))
            .bind(boundary_name)
            .bind(id)
            .execute(pool)
            .await
            .unwrap();
    }
    let mut values = vec![
        digest.to_string(),
        "plaintext password".into(),
        "x".repeat(4096),
        digest.replace("m=19456,t=2,p=1", "t=2,m=19456,p=1"),
        digest.replace("argon2id", "argon2i"),
        digest.replace("v=19", "v=16"),
        digest.replace("m=19456", "m=99999"),
        format!("{}\n", digest.as_str()),
    ];
    let prefix = "$argon2id$v=19$m=19456,t=2,p=1$";
    for last in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".chars() {
        values.push(format!(
            "{prefix}{}{last}${}",
            "A".repeat(21),
            "A".repeat(43)
        ));
        values.push(format!(
            "{prefix}{}${}{last}",
            "A".repeat(22),
            "A".repeat(42)
        ));
    }
    for (table, pool) in [("users", &console), ("authors", &auth)] {
        for value in &values {
            let accepted = px_credentials::valid_hash(value);
            let result = sqlx::query(&format!(
                "UPDATE pixels.{table} SET password_hash=$1 WHERE id=$2"
            ))
            .bind(value)
            .bind(id)
            .execute(pool)
            .await;
            if accepted {
                assert_eq!(result.unwrap().rows_affected(), 1);
            } else {
                assert_eq!(
                    DatabaseError::from(result.unwrap_err()),
                    DatabaseError::Conflict
                );
            }
        }
        sqlx::query(&format!(
            "UPDATE pixels.{table} SET password_hash=$1 WHERE id=$2"
        ))
        .bind(digest.as_str())
        .bind(id)
        .execute(pool)
        .await
        .unwrap();
    }
    console.close().await;
    auth.close().await;
}

/// Owns exactly the process created by this test; panic cleanup cannot kill another application.
struct TestChild(std::process::Child);

impl TestChild {
    fn start() -> Self {
        Self(
            std::process::Command::new(env!("CARGO_BIN_EXE_px_pg_fault"))
                .stdin(std::process::Stdio::null())
                .stdout(std::process::Stdio::null())
                .stderr(std::process::Stdio::null())
                .spawn()
                .unwrap(),
        )
    }

    async fn wait_success(&mut self) {
        tokio::time::timeout(Duration::from_secs(15), async {
            loop {
                if let Some(status) = self.0.try_wait().unwrap() {
                    assert!(status.success());
                    break;
                }
                tokio::time::sleep(Duration::from_millis(20)).await;
            }
        })
        .await
        .expect("migration child exceeded deadline");
    }
}

impl Drop for TestChild {
    fn drop(&mut self) {
        let _ = self.0.kill();
        let _ = self.0.wait();
    }
}

#[tokio::test]
async fn killed_migration_rolls_back_and_two_process_retry_executes_once() {
    let owner = pool(Service::Desk, true).await;
    let probe_version = catalog::migrations(Service::Desk)
        .iter()
        .map(|migration| migration.version)
        .max()
        .unwrap()
        + 1;
    let dsn = env::var("PIXELS_TEST_DESK_OWNER_URL").unwrap();
    let mut barrier = PgConnection::connect(&dsn).await.unwrap();
    sqlx::query("SELECT pg_advisory_lock(912340567)")
        .execute(&mut barrier)
        .await
        .unwrap();
    let mut child = TestChild::start();
    // Observe the exact SQL barrier after CREATE TABLE, not an assumed startup delay.
    tokio::time::timeout(Duration::from_secs(10), async {
        loop {
            assert!(child.0.try_wait().unwrap().is_none(), "migration child exited before reaching the fault boundary");
            let waiting: i64 = sqlx::query_scalar("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND NOT granted AND objid=912340567 AND database=(SELECT oid FROM pg_database WHERE datname=current_database())")
                .fetch_one(&owner).await.unwrap();
            if waiting == 1 { break; }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    }).await.expect("child never reached the transactional fault boundary");
    drop(child); // OS-level process termination, not cancellation of an in-process future.
    sqlx::query("SELECT pg_advisory_unlock(912340567)")
        .execute(&mut barrier)
        .await
        .unwrap();
    barrier.close().await.unwrap();
    // A repeat migration is also a synchronization barrier for release of the killed session's lock.
    migrate(&config(Service::Desk, true), Service::Desk, deployment())
        .await
        .unwrap();
    let exists: bool = sqlx::query_scalar("SELECT to_regclass('pixels.crash_probe') IS NOT NULL")
        .fetch_one(&owner)
        .await
        .unwrap();
    assert!(!exists, "DDL executed before the kill must roll back");
    let count: i64 =
        sqlx::query_scalar("SELECT count(*) FROM pixels._sqlx_migrations WHERE version=$1")
            .bind(probe_version)
            .fetch_one(&owner)
            .await
            .unwrap();
    assert_eq!(count, 0);
    let mut first = TestChild::start();
    let mut second = TestChild::start();
    tokio::join!(first.wait_success(), second.wait_success());
    let rows: Vec<i32> = sqlx::query_scalar("SELECT id FROM pixels.crash_probe")
        .fetch_all(&owner)
        .await
        .unwrap();
    assert_eq!(rows, vec![1]);
    let count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pixels._sqlx_migrations WHERE version=$1 AND success",
    )
    .bind(probe_version)
    .fetch_one(&owner)
    .await
    .unwrap();
    assert_eq!(count, 1);
    // Only the test's own synthetic schema is removed; restore the product schema for remaining tests.
    let mut cleanup = owner.begin().await.unwrap();
    sqlx::query("DROP TABLE pixels.crash_probe")
        .execute(&mut *cleanup)
        .await
        .unwrap();
    sqlx::query("DELETE FROM pixels._sqlx_migrations WHERE version=$1")
        .bind(probe_version)
        .execute(&mut *cleanup)
        .await
        .unwrap();
    cleanup.commit().await.unwrap();
    readiness(&owner, Service::Desk, deployment())
        .await
        .unwrap();
    owner.close().await;
}
