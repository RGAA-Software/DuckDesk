use px_pg::{DatabaseConfig, DatabaseError, Transport};
use sqlx::Row;
use std::{path::Path, time::Duration};
use url::Url;
use zeroize::Zeroizing;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum SetupDatabaseError {
    #[error("PostgreSQL connection requires an absolute TLS root certificate path")]
    Configuration,
    #[error("PostgreSQL is unavailable or TLS verification failed")]
    Unavailable,
    #[error("PostgreSQL administrator authentication failed")]
    Authentication,
    #[error("PostgreSQL 18 is required")]
    Version,
    #[error("PostgreSQL setup requires a superuser for the existing Backup role provisioner")]
    Permission,
    #[error("a Pixels Console database or role already exists; refusing to overwrite it")]
    AlreadyExists,
    #[error("PostgreSQL Console database initialization failed")]
    Operation,
}

/// Read-only setup check. The caller owns the one-time administrator credential and must not log it.
pub async fn check_postgresql_administrator(
    administrator_url: Zeroizing<String>,
) -> Result<(), SetupDatabaseError> {
    let parsed_url =
        Url::parse(&administrator_url).map_err(|_| SetupDatabaseError::Configuration)?;
    let certificate_paths = parsed_url
        .query_pairs()
        .filter(|(parameter_name, _)| parameter_name == "sslrootcert")
        .map(|(_, certificate_path)| certificate_path.into_owned())
        .collect::<Vec<_>>();
    if certificate_paths.len() != 1
        || !Path::new(&certificate_paths[0]).is_absolute()
        || !Path::new(&certificate_paths[0]).is_file()
    {
        return Err(SetupDatabaseError::Configuration);
    }
    let database_configuration = DatabaseConfig::parse(&administrator_url, Transport::VerifyFull)
        .and_then(|configuration| configuration.with_pool_limits(1, Duration::from_secs(5)))
        .map_err(|_| SetupDatabaseError::Configuration)?;
    let pool = database_configuration
        .connect()
        .await
        .map_err(|error| match error {
            DatabaseError::Permission => SetupDatabaseError::Authentication,
            _ => SetupDatabaseError::Unavailable,
        })?;
    let check_result = async {
        let privilege_row = sqlx::query(
            "SELECT pg_catalog.current_setting('server_version_num')::integer AS server_version, \
             rolsuper FROM pg_catalog.pg_roles WHERE rolname = current_user",
        )
        .fetch_one(&pool)
        .await
        .map_err(|_| SetupDatabaseError::Unavailable)?;
        let server_version: i32 = privilege_row
            .try_get("server_version")
            .map_err(|_| SetupDatabaseError::Unavailable)?;
        let is_superuser: bool = privilege_row
            .try_get("rolsuper")
            .map_err(|_| SetupDatabaseError::Unavailable)?;
        if !(180000..190000).contains(&server_version) {
            return Err(SetupDatabaseError::Version);
        }
        if !is_superuser {
            return Err(SetupDatabaseError::Permission);
        }
        Ok(())
    }
    .await;
    pool.close().await;
    check_result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn rejects_setup_urls_without_a_real_explicit_tls_root() {
        for administrator_url in [
            "postgresql://admin:secret@localhost/postgres".to_owned(),
            "postgresql://admin:secret@localhost/postgres?sslrootcert=relative.pem".to_owned(),
            "postgresql://admin:secret@localhost/postgres?sslrootcert=/not-present/ca.pem"
                .to_owned(),
        ] {
            assert_eq!(
                check_postgresql_administrator(Zeroizing::new(administrator_url)).await,
                Err(SetupDatabaseError::Configuration)
            );
        }
    }
}
