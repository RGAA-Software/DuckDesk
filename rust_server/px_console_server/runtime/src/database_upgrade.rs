//! Offline schema-owner credentials are separate from all business service identities.
use px_pg::{migration_preflight, DatabaseConfig, MigrationStatus, Service, Transport};
use px_private_files::private;
use sqlx::{migrate::Migrator, Acquire};
use std::{fs, path::Path};
use url::Url;
use uuid::Uuid;
use zeroize::Zeroizing;

static CONSOLE_MIGRATIONS: Migrator = sqlx::migrate!("../migrations");
const OWNER_CREDENTIAL: &str = "database-upgrade/console-owner.url";

pub(crate) fn save_console_owner_credential(
    config_root: &Path,
    owner_url: &str,
) -> Result<(), &'static str> {
    crate::setup_install::make_private_directory(&config_root.join("database-upgrade"))
        .map_err(|_| "offline migration directory is not private")?;
    private::create_private(&config_root.join(OWNER_CREDENTIAL), owner_url.as_bytes())
}

struct UpgradeIdentity {
    deployment_id: Uuid,
    runtime_url: Zeroizing<String>,
}

fn read_upgrade_identity(config_root: &Path) -> Result<UpgradeIdentity, &'static str> {
    if !config_root.is_absolute() {
        return Err("configuration root must be absolute");
    }
    let environment_path = config_root.join("console.env");
    if !fs::symlink_metadata(&environment_path)
        .map_err(|_| "Console environment is unavailable")?
        .file_type()
        .is_file()
    {
        return Err("Console environment must be a regular file");
    }
    let environment = Zeroizing::new(
        fs::read_to_string(environment_path).map_err(|_| "Console environment is unavailable")?,
    );
    if environment.len() > 65536 {
        return Err("Console environment exceeds its limit");
    }
    let environment_value = |name: &str| -> Result<&str, &'static str> {
        let prefix = format!("{name}=");
        let mut matching_values = environment
            .lines()
            .filter_map(|line| line.strip_prefix(&prefix));
        let value = matching_values
            .next()
            .ok_or("Console upgrade identity is missing")?;
        if value.is_empty() || matching_values.next().is_some() {
            return Err("Console upgrade identity is ambiguous");
        }
        Ok(value)
    };
    let deployment_id = environment_value("PIXELS_DEPLOYMENT_ID")?
        .parse::<Uuid>()
        .map_err(|_| "Console deployment identity is invalid")?;
    if deployment_id.is_nil() {
        return Err("Console deployment identity is invalid");
    }
    Ok(UpgradeIdentity {
        deployment_id,
        runtime_url: Zeroizing::new(environment_value("PIXELS_CONSOLE_DATABASE_URL")?.to_owned()),
    })
}

fn validate_owner_endpoint(owner_url: &str, runtime_url: &str) -> Result<(), &'static str> {
    let owner = Url::parse(owner_url).map_err(|_| "offline migration endpoint is invalid")?;
    let runtime = Url::parse(runtime_url).map_err(|_| "Console database endpoint is invalid")?;
    if owner.username() != "pixels_console_owner"
        || owner.password().is_none_or(str::is_empty)
        || owner.host_str() != runtime.host_str()
        || owner.port() != runtime.port()
        || owner.path() != "/pixels_console"
        || runtime.path() != owner.path()
        || runtime.username() != "pixels_console_runtime"
        || owner.query().is_some()
        || owner.fragment().is_some()
    {
        return Err("offline migration identity differs from Console");
    }
    DatabaseConfig::parse(owner_url, Transport::PreferTls)
        .map_err(|_| "offline migration endpoint is invalid")?;
    Ok(())
}

fn load_owner_configuration(
    config_root: &Path,
    identity: &UpgradeIdentity,
) -> Result<DatabaseConfig, &'static str> {
    let mut credential_bytes = private::read_private(&config_root.join(OWNER_CREDENTIAL))?;
    let owner_url = Zeroizing::new(
        String::from_utf8(std::mem::take(&mut *credential_bytes))
            .map_err(|_| "offline migration credential is invalid")?,
    );
    validate_owner_endpoint(&owner_url, &identity.runtime_url)?;
    DatabaseConfig::parse(&owner_url, Transport::PreferTls)
        .map_err(|_| "offline migration credential is invalid")
}

pub async fn preflight_single_server_database(
    config_root: &Path,
) -> Result<MigrationStatus, Box<dyn std::error::Error>> {
    let identity = read_upgrade_identity(config_root)?;
    let owner_configuration = load_owner_configuration(config_root, &identity)?;
    Ok(migration_preflight(
        &owner_configuration,
        Service::Console,
        identity.deployment_id,
        &CONSOLE_MIGRATIONS,
    )
    .await?)
}

pub async fn upgrade_single_server_database(
    config_root: &Path,
) -> Result<MigrationStatus, Box<dyn std::error::Error>> {
    let status = preflight_single_server_database(config_root).await?;
    let identity = read_upgrade_identity(config_root)?;
    let owner_configuration = load_owner_configuration(config_root, &identity)?;
    px_pg::migrate(
        &owner_configuration,
        Service::Console,
        identity.deployment_id,
        &CONSOLE_MIGRATIONS,
    )
    .await?;
    Ok(status)
}

/// Explicit operator recovery only; never invoked by routine installation or startup.
/// An existing credential is never overwritten and no business role is modified.
pub async fn recover_single_server_database_owner(
    config_root: &Path,
    administrator_url: Zeroizing<String>,
) -> Result<(), Box<dyn std::error::Error>> {
    let identity = read_upgrade_identity(config_root)?;
    if config_root.join(OWNER_CREDENTIAL).exists() {
        return Err("offline migration credential already exists; recovery refused".into());
    }
    crate::check_postgresql_administrator(administrator_url.clone()).await?;
    let runtime = Url::parse(&identity.runtime_url)?;
    let mut administrator = Url::parse(&administrator_url)?;
    if administrator.host_str() != runtime.host_str() || administrator.port() != runtime.port() {
        return Err("administrator database endpoint differs from Console".into());
    }
    administrator.set_path("/pixels_console");
    let administrator_configuration =
        DatabaseConfig::parse(administrator.as_str(), Transport::PreferTls)?;
    let administrator_pool = administrator_configuration.connect().await?;
    let mut connection = administrator_pool.acquire().await?;
    let database_identity: Vec<(Uuid, String)> =
        sqlx::query_as("SELECT deployment_id, service FROM pixels.deployment_identity")
            .fetch_all(&mut *connection)
            .await?;
    if database_identity != vec![(identity.deployment_id, "console".to_owned())] {
        return Err("database deployment identity differs from Console".into());
    }
    let owner_restricted: bool = sqlx::query_scalar(
        "SELECT NOT rolsuper AND NOT rolcreatedb AND NOT rolcreaterole AND NOT rolreplication AND NOT rolbypassrls
         FROM pg_roles WHERE rolname='pixels_console_owner'",
    )
    .fetch_one(&mut *connection)
    .await?;
    if !owner_restricted {
        return Err("offline schema owner has unexpected privileges".into());
    }
    let credentials = crate::ConsoleDatabaseCredentials::generate();
    let mut owner_url = runtime;
    owner_url
        .set_username("pixels_console_owner")
        .map_err(|_| "invalid owner endpoint")?;
    owner_url
        .set_password(Some(&credentials.owner_password))
        .map_err(|_| "invalid owner credential")?;
    owner_url.set_query(None);
    validate_owner_endpoint(owner_url.as_str(), &identity.runtime_url)?;
    // Persist before mutation; failure never loses a successfully changed role password.
    save_console_owner_credential(config_root, owner_url.as_str())?;
    let mut transaction = connection.begin().await?;
    sqlx::query("SELECT pg_catalog.set_config('pixels.upgrade_owner_password',$1,true)")
        .bind(credentials.owner_password.as_str())
        .execute(&mut *transaction)
        .await?;
    sqlx::query(
        "DO $pixels$ BEGIN EXECUTE format('ALTER ROLE pixels_console_owner PASSWORD %L',
         pg_catalog.current_setting('pixels.upgrade_owner_password')); END $pixels$",
    )
    .execute(&mut *transaction)
    .await?;
    transaction.commit().await?;
    drop(connection);
    administrator_pool.close().await;
    preflight_single_server_database(config_root).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn owner_endpoint_is_bound_to_runtime_host_port_database_and_role() {
        let runtime_url =
            "postgresql://pixels_console_runtime:runtime-secret@localhost:5432/pixels_console";
        let owner_url =
            "postgresql://pixels_console_owner:owner-secret@localhost:5432/pixels_console";
        assert!(validate_owner_endpoint(owner_url, runtime_url).is_ok());
        for invalid_url in [
            owner_url.replace("localhost", "other-server"),
            owner_url.replace("5432", "5433"),
            owner_url.replace("/pixels_console", "/other_database"),
            owner_url.replace("pixels_console_owner", "pixels_console_runtime"),
            owner_url.replace(":owner-secret", ""),
            format!("{owner_url}?options=unsafe"),
        ] {
            assert!(validate_owner_endpoint(&invalid_url, runtime_url).is_err());
        }
    }
}
