use crate::{DatabaseConfig, DatabaseError};
use sqlx::{
    migrate::{Migrate, Migrator},
    Connection, PgConnection, PgPool,
};
use std::time::Duration;
use uuid::Uuid;

// Distinct from SQLx's migration serialization and Console's single-activation lock.
pub(crate) const SCHEMA_LOCK: i64 = 22091701;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MigrationStatus {
    pub installed_version: i64,
    pub target_version: i64,
}

/// Read-only owner preflight accepts only a checksum-exact prefix of the incoming schema.
/// It does not change roles, run DDL, or admit an old runtime against a newer schema.
pub async fn migration_preflight(
    config: &DatabaseConfig,
    service: Service,
    deployment: Uuid,
    expected: &Migrator,
) -> Result<MigrationStatus, DatabaseError> {
    tokio::time::timeout(Duration::from_secs(15), async {
        let mut connection = PgConnection::connect_with(&config.options()).await?;
        identity(&mut connection, service, deployment).await?;
        crate::runtime::require_owner(&mut connection, service).await?;
        let rows: Vec<(i64, bool, Vec<u8>)> = sqlx::query_as(
            "SELECT version, success, checksum FROM pixels._sqlx_migrations ORDER BY version",
        )
        .fetch_all(&mut connection)
        .await?;
        let status = validate_migration_prefix(&rows, expected)?;
        connection.close().await?;
        Ok(status)
    })
    .await
    .map_err(|_| DatabaseError::Unavailable)?
}

fn validate_migration_prefix(
    rows: &[(i64, bool, Vec<u8>)],
    expected: &Migrator,
) -> Result<MigrationStatus, DatabaseError> {
    if rows.is_empty()
        || rows.len() > expected.iter().count()
        || rows.iter().zip(expected.iter()).any(|(row, migration)| {
            row.0 != migration.version || !row.1 || row.2.as_slice() != migration.checksum.as_ref()
        })
    {
        return Err(DatabaseError::Schema);
    }
    Ok(MigrationStatus {
        installed_version: rows.last().ok_or(DatabaseError::Schema)?.0,
        target_version: expected.iter().last().ok_or(DatabaseError::Schema)?.version,
    })
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Service {
    Console,
    Auth,
    Desk,
}

impl Service {
    pub fn name(self) -> &'static str {
        match self {
            Self::Console => "console",
            Self::Auth => "auth",
            Self::Desk => "desk",
        }
    }
    pub fn parse(value: &str) -> Result<Self, DatabaseError> {
        match value {
            "console" => Ok(Self::Console),
            "auth" => Ok(Self::Auth),
            "desk" => Ok(Self::Desk),
            _ => Err(DatabaseError::Configuration),
        }
    }
}

pub(crate) async fn identity(
    connection: &mut PgConnection,
    service: Service,
    deployment: Uuid,
) -> Result<(), DatabaseError> {
    let version: i32 = sqlx::query_scalar("SELECT current_setting('server_version_num')::integer")
        .fetch_one(&mut *connection)
        .await?;
    if version / 10000 != 18 {
        return Err(DatabaseError::Schema);
    }
    let rows: Vec<(Uuid, String)> =
        sqlx::query_as("SELECT deployment_id, service FROM pixels.deployment_identity")
            .fetch_all(&mut *connection)
            .await?;
    if rows.len() != 1 || rows[0].0 != deployment || rows[0].1 != service.name() {
        return Err(DatabaseError::Identity);
    }
    Ok(())
}

/// Read-only gate; never runs DDL. Health callers must distinguish this from liveness.
pub async fn readiness(
    pool: &PgPool,
    service: Service,
    deployment: Uuid,
    expected: &Migrator,
) -> Result<(), DatabaseError> {
    let mut connection = pool.acquire().await?;
    readiness_on(&mut connection, service, deployment, expected).await
}

pub(crate) async fn readiness_on(
    connection: &mut PgConnection,
    service: Service,
    deployment: Uuid,
    expected: &Migrator,
) -> Result<(), DatabaseError> {
    identity(connection, service, deployment).await?;
    let rows: Vec<(i64, bool, Vec<u8>)> = sqlx::query_as(
        "SELECT version, success, checksum FROM pixels._sqlx_migrations ORDER BY version",
    )
    .fetch_all(&mut *connection)
    .await?;
    if rows.len() != expected.iter().count()
        || rows.iter().zip(expected.iter()).any(|(row, migration)| {
            row.0 != migration.version || !row.1 || row.2.as_slice() != migration.checksum.as_ref()
        })
    {
        return Err(DatabaseError::Schema);
    }
    Ok(())
}

/// Dedicated connection: on timeout/drop SQLx cannot return a session holding an advisory lock to a pool.
/// Only the separate migration CLI calls this. SQLx serializes migrators and validates checksums.
pub async fn migrate(
    config: &DatabaseConfig,
    service: Service,
    deployment: Uuid,
    migrations: &Migrator,
) -> Result<(), DatabaseError> {
    tokio::time::timeout(Duration::from_secs(30), async {
        let mut connection = PgConnection::connect_with(&config.options()).await?;
        identity(&mut connection, service, deployment).await?;
        crate::runtime::require_owner(&mut connection, service).await?;
        // PostgreSQL session advisory locks are reentrant. Keep one outer acquisition
        // after SQLx releases its inner acquisition, so the ledger GRANT is serialized too.
        // The dedicated connection releases all levels on cancellation/error/process death.
        connection.lock().await.map_err(DatabaseError::from)?;
        // A live business connection holds the shared side for its entire lifetime.
        // Never start even a repeat/no-op migration while runtime pools are open.
        let exclusive: bool = sqlx::query_scalar("SELECT pg_try_advisory_lock($1)")
            .bind(SCHEMA_LOCK)
            .fetch_one(&mut connection)
            .await?;
        if !exclusive {
            return Err(DatabaseError::Conflict);
        }
        migrations
            .run(&mut connection)
            .await
            .map_err(DatabaseError::from)?;
        // Explicit privilege; runtime can inspect, never modify SQLx's migration ledger.
        let grant = format!(
            "GRANT SELECT ON pixels._sqlx_migrations TO pixels_{}_runtime",
            service.name()
        );
        sqlx::query(&grant).execute(&mut connection).await?;
        connection.unlock().await.map_err(DatabaseError::from)?;
        connection.close().await?;
        Ok(())
    })
    .await
    .map_err(|_| DatabaseError::Unavailable)?
}

#[cfg(test)]
mod upgrade_tests {
    use super::*;
    use sqlx::migrate::{Migration, MigrationType};
    use std::borrow::Cow;

    fn expected_migrations() -> Migrator {
        let migrations = (1..=2)
            .map(|version| {
                Migration::new(
                    version,
                    Cow::Borrowed("upgrade-test"),
                    MigrationType::Simple,
                    Cow::Borrowed("SELECT 1;"),
                    false,
                )
            })
            .collect();
        Migrator {
            migrations: Cow::Owned(migrations),
            ..Migrator::DEFAULT
        }
    }

    #[test]
    fn exact_prefix_and_current_schema_are_accepted() {
        let expected = expected_migrations();
        let rows: Vec<_> = expected
            .iter()
            .map(|migration| (migration.version, true, migration.checksum.to_vec()))
            .collect();
        assert_eq!(
            validate_migration_prefix(&rows[..1], &expected).unwrap(),
            MigrationStatus {
                installed_version: 1,
                target_version: 2
            }
        );
        assert_eq!(
            validate_migration_prefix(&rows, &expected).unwrap(),
            MigrationStatus {
                installed_version: 2,
                target_version: 2
            }
        );
    }

    #[test]
    fn missing_failed_modified_or_future_history_is_rejected() {
        let expected = expected_migrations();
        let checksum = expected.iter().next().unwrap().checksum.to_vec();
        for invalid_rows in [
            vec![],
            vec![(1, false, checksum.clone())],
            vec![(1, true, vec![0])],
            vec![(2, true, checksum.clone())],
            vec![
                (1, true, checksum.clone()),
                (2, true, checksum.clone()),
                (3, true, checksum),
            ],
        ] {
            assert!(validate_migration_prefix(&invalid_rows, &expected).is_err());
        }
    }
}
