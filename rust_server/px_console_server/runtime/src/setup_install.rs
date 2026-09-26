use crate::{provision_fresh_console_database, ConsoleDatabaseCredentials};
use px_console_store::{initialize_administrator, PasswordDigest, Username};
use px_pg::{migrate, provision_backup_role, DatabaseConfig, Service, Transport};
use px_private_files::{private, CacheRoot};
use rand::RngCore;
use rcgen::{
    BasicConstraints, CertificateParams, ExtendedKeyUsagePurpose, IsCa, Issuer, KeyPair,
    KeyUsagePurpose,
};
use serde::Deserialize;
use sha2::{Digest, Sha256};
use sqlx::migrate::Migrator;
use std::{
    fs,
    path::{Path, PathBuf},
    time::{SystemTime, UNIX_EPOCH},
};
use url::Url;
use uuid::Uuid;
use zeroize::Zeroizing;

static CONSOLE_MIGRATIONS: Migrator = sqlx::migrate!("../migrations");

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SingleServerSetupInput {
    pub postgresql_host: String,
    pub postgresql_port: u16,
    pub postgresql_administrator: String,
    pub postgresql_password: String,
    pub postgresql_ca_pem: String,
    pub public_host: String,
    pub initial_username: String,
    pub initial_password: String,
}

#[derive(Clone)]
pub struct SingleServerLayout {
    pub config_root: PathBuf,
    pub data_root: PathBuf,
    pub runtime_root: PathBuf,
    pub package_root: PathBuf,
    pub linux_container: bool,
}

pub struct SingleServerSetupResult {
    pub deployment_id: Uuid,
    pub console_origin: String,
    pub relay_token: Zeroizing<String>,
}

pub fn activate_single_server_relay(
    layout: &SingleServerLayout,
    relay_token: &str,
) -> Result<(), SingleServerSetupError> {
    if relay_token.len() != 64
        || !relay_token
            .bytes()
            .all(|character| character.is_ascii_digit() || (b'a'..=b'f').contains(&character))
    {
        return Err(SingleServerSetupError::Invalid);
    }
    let environment_path = layout.config_root.join("relay.env");
    let metadata =
        fs::symlink_metadata(&environment_path).map_err(|_| SingleServerSetupError::Private)?;
    if !metadata.is_file() || metadata.file_type().is_symlink() || metadata.len() > 4096 {
        return Err(SingleServerSetupError::Private);
    }
    let previous_environment = Zeroizing::new(
        fs::read_to_string(&environment_path).map_err(|_| SingleServerSetupError::Private)?,
    );
    let token_lines = previous_environment
        .lines()
        .filter(|line| line.starts_with("PIXELS_RELAY_NODE_TOKEN="))
        .count();
    if token_lines != 1 {
        return Err(SingleServerSetupError::Private);
    }
    let next_environment = Zeroizing::new(
        previous_environment
            .lines()
            .map(|line| {
                if line.starts_with("PIXELS_RELAY_NODE_TOKEN=") {
                    format!("PIXELS_RELAY_NODE_TOKEN={relay_token}")
                } else {
                    line.to_owned()
                }
            })
            .collect::<Vec<_>>()
            .join("\n")
            + "\n",
    );
    #[cfg(windows)]
    {
        use std::io::Write;
        // The installer has granted the Relay service read access. The general private-file
        // replacement API intentionally rejects that service ACE when called as LocalSystem.
        // Relay reads this file only on startup; update it before the installer restarts Relay.
        let mut environment_file = fs::OpenOptions::new()
            .write(true)
            .truncate(true)
            .open(&environment_path)
            .map_err(|_| SingleServerSetupError::Private)?;
        environment_file
            .write_all(next_environment.as_bytes())
            .map_err(|_| SingleServerSetupError::Private)?;
        environment_file
            .sync_all()
            .map_err(|_| SingleServerSetupError::Private)?;
        if fs::read(&environment_path).map_err(|_| SingleServerSetupError::Private)?
            != next_environment.as_bytes()
        {
            return Err(SingleServerSetupError::Private);
        }
    }
    #[cfg(unix)]
    {
        use std::{
            io::Write,
            os::unix::fs::{chown, OpenOptionsExt, PermissionsExt},
        };
        let temporary_path = layout
            .config_root
            .join(format!(".relay-env-{}", Uuid::new_v4()));
        let mut temporary_file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .open(&temporary_path)
            .map_err(|_| SingleServerSetupError::Private)?;
        temporary_file
            .write_all(next_environment.as_bytes())
            .map_err(|_| SingleServerSetupError::Private)?;
        temporary_file
            .sync_all()
            .map_err(|_| SingleServerSetupError::Private)?;
        chown(&temporary_path, Some(10002), Some(10002))
            .map_err(|_| SingleServerSetupError::Private)?;
        fs::rename(&temporary_path, &environment_path)
            .map_err(|_| SingleServerSetupError::Private)?;
        let marker = layout.config_root.join("relay.ready");
        fs::write(&marker, b"ready\n").map_err(|_| SingleServerSetupError::Private)?;
        fs::set_permissions(marker, fs::Permissions::from_mode(0o644))
            .map_err(|_| SingleServerSetupError::Private)?;
    }
    Ok(())
}

#[derive(Debug, thiserror::Error)]
pub enum SingleServerSetupError {
    #[error("invalid setup fields, PostgreSQL CA, or destination paths")]
    Invalid,
    #[error("this deployment is already initialized")]
    Existing,
    #[error("PostgreSQL preflight or initialization failed: {0}")]
    Database(crate::SetupDatabaseError),
    #[error("Console schema or administrator initialization failed")]
    Console,
    #[error("private configuration or TLS certificate creation failed")]
    Private,
    #[error("bundled PostgreSQL tools or license trust key are unavailable")]
    Package,
}

/// One cross-platform initializer. The caller starts the already-packaged services afterward.
/// No administrator database URL or password is persisted in the resulting configuration.
pub async fn initialize_single_server(
    mut input: SingleServerSetupInput,
    layout: &SingleServerLayout,
) -> Result<SingleServerSetupResult, SingleServerSetupError> {
    validate_input(&input, layout)?;
    if layout.config_root.join("console.env").exists()
        || layout.config_root.join("relay.env").exists()
        || layout.config_root.join("backup.json").exists()
    {
        return Err(SingleServerSetupError::Existing);
    }
    make_private_directory(&layout.config_root)?;
    make_private_directory(&layout.config_root.join("console"))?;
    make_private_directory(&layout.config_root.join("console/license"))?;
    make_private_directory(&layout.config_root.join("backup"))?;
    make_private_directory(&layout.data_root)?;
    for relative_directory in [
        "recording-cache",
        "backup/repository",
        "backup/scheduler",
        "backup/status",
    ] {
        make_private_directory(&layout.data_root.join(relative_directory))?;
    }

    let postgresql_ca_path = layout.config_root.join("postgresql-ca.crt");
    if postgresql_ca_path.exists() {
        private::replace_private(&postgresql_ca_path, input.postgresql_ca_pem.as_bytes())
            .map_err(|_| SingleServerSetupError::Private)?;
    } else {
        private::create_private(&postgresql_ca_path, input.postgresql_ca_pem.as_bytes())
            .map_err(|_| SingleServerSetupError::Private)?;
    }
    let mut administrator_url = Url::parse("postgresql://localhost/postgres")
        .map_err(|_| SingleServerSetupError::Invalid)?;
    administrator_url
        .set_host(Some(&input.postgresql_host))
        .map_err(|_| SingleServerSetupError::Invalid)?;
    administrator_url
        .set_port(Some(input.postgresql_port))
        .map_err(|_| SingleServerSetupError::Invalid)?;
    administrator_url
        .set_username(&input.postgresql_administrator)
        .map_err(|_| SingleServerSetupError::Invalid)?;
    let administrator_password = Zeroizing::new(std::mem::take(&mut input.postgresql_password));
    administrator_url
        .set_password(Some(&administrator_password))
        .map_err(|_| SingleServerSetupError::Invalid)?;
    administrator_url.query_pairs_mut().append_pair(
        "sslrootcert",
        &postgresql_ca_path.to_string_lossy().replace('\\', "/"),
    );
    let administrator_url = Zeroizing::new(administrator_url.to_string());
    let deployment_id = Uuid::new_v4();
    let database_credentials = ConsoleDatabaseCredentials::generate();
    let (owner_url, runtime_url) = provision_fresh_console_database(
        administrator_url.clone(),
        deployment_id,
        &database_credentials,
    )
    .await
    .map_err(SingleServerSetupError::Database)?;
    let owner_configuration = DatabaseConfig::parse(&owner_url, Transport::VerifyFull)
        .map_err(|_| SingleServerSetupError::Console)?;
    migrate(
        &owner_configuration,
        Service::Console,
        deployment_id,
        &CONSOLE_MIGRATIONS,
    )
    .await
    .map_err(|_| SingleServerSetupError::Console)?;
    let username =
        Username::parse(&input.initial_username).map_err(|_| SingleServerSetupError::Invalid)?;
    let initial_password = Zeroizing::new(std::mem::take(&mut input.initial_password));
    let password_digest =
        px_credentials::hash(&initial_password).map_err(|_| SingleServerSetupError::Invalid)?;
    initialize_administrator(
        &owner_configuration,
        deployment_id,
        &username,
        &PasswordDigest::parse(password_digest.to_string())
            .map_err(|_| SingleServerSetupError::Console)?,
    )
    .await
    .map_err(|_| SingleServerSetupError::Console)?;

    let backup_password = random_secret();
    let mut backup_admin_url =
        Url::parse(&administrator_url).map_err(|_| SingleServerSetupError::Console)?;
    backup_admin_url.set_path("/pixels_console");
    let backup_configuration =
        DatabaseConfig::parse(backup_admin_url.as_str(), Transport::VerifyFull)
            .map_err(|_| SingleServerSetupError::Console)?;
    provision_backup_role(
        &backup_configuration,
        Service::Console,
        deployment_id,
        &backup_password,
    )
    .await
    .map_err(|_| SingleServerSetupError::Console)?;
    let password_file = layout.config_root.join("backup/console.pgpass");
    let password_record = Zeroizing::new(format!(
        "{}:{}:pixels_console:pixels_console_backup:{}\n",
        input.postgresql_host,
        input.postgresql_port,
        backup_password.as_str()
    ));
    private::create_private(&password_file, password_record.as_bytes())
        .map_err(|_| SingleServerSetupError::Private)?;

    let (certificate_authority, server_certificate, server_key) =
        generate_console_certificate(&input.public_host)?;
    let console_ca_path = layout.config_root.join("console-ca.crt");
    let console_certificate_path = layout.config_root.join("console-tls.crt");
    let console_key_path = layout.config_root.join("console-tls.key");
    private::create_private(&console_ca_path, certificate_authority.as_bytes())
        .map_err(|_| SingleServerSetupError::Private)?;
    private::create_private(&console_certificate_path, server_certificate.as_bytes())
        .map_err(|_| SingleServerSetupError::Private)?;
    private::create_private(&console_key_path, server_key.as_bytes())
        .map_err(|_| SingleServerSetupError::Private)?;
    let bundled_trust = fs::read(layout.package_root.join("assets/license-trust.json"))
        .map_err(|_| SingleServerSetupError::Package)?;
    let trust_bytes = bundled_trust.strip_suffix(b"\n").unwrap_or(&bundled_trust);
    px_license::LicenseTrustStore::from_canonical_bytes(&trust_bytes)
        .map_err(|_| SingleServerSetupError::Package)?;
    let trust_path = layout.config_root.join("license-trust.json");
    private::create_private(&trust_path, &trust_bytes)
        .map_err(|_| SingleServerSetupError::Private)?;

    let guest_key_path = layout.config_root.join("guest-source.key");
    let workspace_key_path = layout.config_root.join("workspace.key");
    write_random_key(&guest_key_path)?;
    write_random_key(&workspace_key_path)?;
    let workspace_key_id = Uuid::new_v4();
    let cache_path = layout.data_root.join("recording-cache");
    CacheRoot::initialize(&cache_path, deployment_id)
        .map_err(|_| SingleServerSetupError::Private)?;

    let console_origin = format!("https://{}:4600", input.public_host);
    let relay_app_key = random_secret();
    let relay_control_key = random_secret();
    let relay_token = random_secret();
    let relay_control_host = if layout.linux_container {
        "console"
    } else {
        "localhost"
    };
    let static_directory = layout.runtime_root.join("static/console");
    let workspace_keys = serde_json::json!([{
        "id":workspace_key_id,
        "path":workspace_key_path,
    }]);
    let console_environment = Zeroizing::new(format!(
        "PIXELS_DEPLOYMENT_ID={deployment_id}\n\
         PIXELS_CONSOLE_DISTRIBUTION=customer\n\
         PIXELS_CONSOLE_RELEASE_NAMESPACE=pixels.customer\n\
         PIXELS_CONSOLE_LOCAL_DEVELOPMENT=0\n\
         PIXELS_CONSOLE_DATABASE_URL={}\n\
         PIXELS_CONSOLE_LISTEN=0.0.0.0:4600\n\
         PIXELS_CONSOLE_STATIC_DIRECTORY={}\n\
         PIXELS_CONSOLE_TLS_CERT={}\n\
         PIXELS_CONSOLE_TLS_KEY={}\n\
         PIXELS_CONSOLE_PUBLIC_ORIGIN={console_origin}\n\
         PIXELS_CONSOLE_REGISTRATION=0\n\
         PIXELS_CONSOLE_GUESTS=0\n\
         PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS=3600\n\
         PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS=3600\n\
         PIXELS_CONSOLE_GUEST_SOURCE_KEY={}\n\
         PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY={workspace_key_id}\n\
         PIXELS_CONSOLE_WORKSPACE_KEYS='{workspace_keys}'\n\
         PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY={}\n\
         PIXELS_CONSOLE_RECORDING_CACHE_BYTES=1073741824\n\
         PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS=4\n\
         PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS=86400\n\
         PIXELS_CONSOLE_LICENSE_TRUST_STORE={}\n\
         PIXELS_CONSOLE_LICENSE_FILE={}\n\
         PIXELS_RELAY_APP_KEY={}\n",
        runtime_url.as_str(),
        static_directory.display(),
        console_certificate_path.display(),
        console_key_path.display(),
        guest_key_path.display(),
        cache_path.display(),
        trust_path.display(),
        layout
            .config_root
            .join("console/license/console.license")
            .display(),
        relay_app_key.as_str(),
    ));
    let relay_environment = Zeroizing::new(format!(
        "PIXELS_DEPLOYMENT_ID={deployment_id}\n\
         PIXELS_RELAY_LISTEN=0.0.0.0:4605\n\
         PIXELS_RELAY_APP_KEY={}\n\
         PIXELS_RELAY_CONTROL_KEY={}\n\
         PIXELS_RELAY_CONSOLE_CONTROL_URL=wss://{}:4600/api/console/relay-control\n\
         PIXELS_RELAY_CONSOLE_CA_FILE={}\n\
         PIXELS_RELAY_NODE_TOKEN={}\n",
        relay_app_key.as_str(),
        relay_control_key.as_str(),
        relay_control_host,
        console_ca_path.display(),
        relay_token.as_str(),
    ));
    write_backup_config(&input, layout, deployment_id, &password_file)?;
    private::create_private(
        &layout.config_root.join("console.env"),
        console_environment.as_bytes(),
    )
    .map_err(|_| SingleServerSetupError::Private)?;
    private::create_private(
        &layout.config_root.join("relay.env"),
        relay_environment.as_bytes(),
    )
    .map_err(|_| SingleServerSetupError::Private)?;
    if layout.linux_container {
        private::create_private(
            &layout.config_root.join("backup.env"),
            format!(
                "PIXELS_BACKUP_PG_SSL_ROOT_CERT={}\n",
                postgresql_ca_path.display()
            )
            .as_bytes(),
        )
        .map_err(|_| SingleServerSetupError::Private)?;
        assign_linux_service_permissions(layout)?;
        let marker = layout.config_root.join("setup.ready");
        fs::write(&marker, b"ready\n").map_err(|_| SingleServerSetupError::Private)?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(marker, fs::Permissions::from_mode(0o644))
                .map_err(|_| SingleServerSetupError::Private)?;
        }
    }
    Ok(SingleServerSetupResult {
        deployment_id,
        console_origin,
        relay_token,
    })
}

#[cfg(unix)]
fn assign_linux_service_permissions(
    layout: &SingleServerLayout,
) -> Result<(), SingleServerSetupError> {
    use std::os::unix::fs::{chown, PermissionsExt};

    let assign = |path: PathBuf, user_id: u32, mode: u32| {
        chown(&path, Some(user_id), Some(user_id)).map_err(|_| SingleServerSetupError::Private)?;
        fs::set_permissions(&path, fs::Permissions::from_mode(mode))
            .map_err(|_| SingleServerSetupError::Private)
    };
    for path in [
        layout.config_root.clone(),
        layout.config_root.join("console"),
        layout.config_root.join("backup"),
        layout.data_root.clone(),
        layout.data_root.join("backup"),
    ] {
        fs::set_permissions(path, fs::Permissions::from_mode(0o755))
            .map_err(|_| SingleServerSetupError::Private)?;
    }
    assign(layout.config_root.join("console/license"), 10001, 0o700)?;
    for relative_path in [
        "console.env",
        "license-trust.json",
        "console-tls.key",
        "guest-source.key",
        "workspace.key",
    ] {
        assign(layout.config_root.join(relative_path), 10001, 0o600)?;
    }
    assign(layout.config_root.join("relay.env"), 10002, 0o600)?;
    for relative_path in ["backup.json", "backup/console.pgpass"] {
        assign(layout.config_root.join(relative_path), 10003, 0o600)?;
    }
    for relative_path in [
        "postgresql-ca.crt",
        "console-ca.crt",
        "console-tls.crt",
        "backup.env",
    ] {
        fs::set_permissions(
            layout.config_root.join(relative_path),
            fs::Permissions::from_mode(0o644),
        )
        .map_err(|_| SingleServerSetupError::Private)?;
    }
    for relative_path in [
        "recording-cache",
        "recording-cache/owner.lock",
        "recording-cache/root.identity",
    ] {
        assign(
            layout.data_root.join(relative_path),
            10001,
            if relative_path == "recording-cache" {
                0o700
            } else {
                0o600
            },
        )?;
    }
    for relative_path in ["backup/repository", "backup/scheduler", "backup/status"] {
        assign(layout.data_root.join(relative_path), 10003, 0o700)?;
    }
    Ok(())
}

#[cfg(windows)]
fn assign_linux_service_permissions(
    _layout: &SingleServerLayout,
) -> Result<(), SingleServerSetupError> {
    Err(SingleServerSetupError::Invalid)
}

fn validate_input(
    input: &SingleServerSetupInput,
    layout: &SingleServerLayout,
) -> Result<(), SingleServerSetupError> {
    if input.postgresql_port == 0
        || input.postgresql_password.is_empty()
        || input.postgresql_ca_pem.len() > 4096
        || !input
            .postgresql_ca_pem
            .contains("-----BEGIN CERTIFICATE-----")
        || !px_credentials::valid_password(&input.initial_password)
        || Username::parse(&input.initial_username).is_err()
        || input.public_host.is_empty()
        || input.public_host.contains(['/', '@', '?', '#', ':'])
        || url::Host::parse(&input.public_host).is_err()
        || input.postgresql_host.is_empty()
        || input.postgresql_host.contains(['/', '@', '?', '#', ':'])
        || url::Host::parse(&input.postgresql_host).is_err()
        || input.postgresql_administrator.is_empty()
        || [
            &layout.config_root,
            &layout.data_root,
            &layout.runtime_root,
            &layout.package_root,
        ]
        .iter()
        .any(|path| !path.is_absolute())
    {
        return Err(SingleServerSetupError::Invalid);
    }
    Ok(())
}

fn make_private_directory(path: &Path) -> Result<(), SingleServerSetupError> {
    fs::create_dir_all(path).map_err(|_| SingleServerSetupError::Private)?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o700))
            .map_err(|_| SingleServerSetupError::Private)?;
    }
    private::verify_private_directory(path).map_err(|_| SingleServerSetupError::Private)
}

fn random_secret() -> Zeroizing<String> {
    let mut entropy = Zeroizing::new([0_u8; 32]);
    rand::rng().fill_bytes(entropy.as_mut());
    Zeroizing::new(hex::encode(entropy.as_ref()))
}

fn write_random_key(path: &Path) -> Result<(), SingleServerSetupError> {
    let mut entropy = Zeroizing::new([0_u8; 32]);
    rand::rng().fill_bytes(entropy.as_mut());
    private::create_private(path, entropy.as_ref()).map_err(|_| SingleServerSetupError::Private)
}

fn generate_console_certificate(
    host: &str,
) -> Result<(String, String, String), SingleServerSetupError> {
    let mut certificate_authority_params = CertificateParams::default();
    certificate_authority_params.is_ca = IsCa::Ca(BasicConstraints::Unconstrained);
    certificate_authority_params.key_usages =
        vec![KeyUsagePurpose::KeyCertSign, KeyUsagePurpose::CrlSign];
    let certificate_authority_key =
        KeyPair::generate().map_err(|_| SingleServerSetupError::Private)?;
    let certificate_authority = certificate_authority_params
        .self_signed(&certificate_authority_key)
        .map_err(|_| SingleServerSetupError::Private)?;
    let issuer = Issuer::from_params(&certificate_authority_params, &certificate_authority_key);
    let mut server_params = CertificateParams::new(vec![
        host.to_owned(),
        "localhost".to_owned(),
        "console".to_owned(),
        "127.0.0.1".to_owned(),
    ])
    .map_err(|_| SingleServerSetupError::Private)?;
    server_params.extended_key_usages = vec![ExtendedKeyUsagePurpose::ServerAuth];
    server_params.key_usages = vec![
        KeyUsagePurpose::DigitalSignature,
        KeyUsagePurpose::KeyEncipherment,
    ];
    let server_key = KeyPair::generate().map_err(|_| SingleServerSetupError::Private)?;
    let server_certificate = server_params
        .signed_by(&server_key, &issuer)
        .map_err(|_| SingleServerSetupError::Private)?;
    Ok((
        certificate_authority.pem(),
        server_certificate.pem(),
        server_key.serialize_pem(),
    ))
}

fn write_backup_config(
    input: &SingleServerSetupInput,
    layout: &SingleServerLayout,
    deployment_id: Uuid,
    password_file: &Path,
) -> Result<(), SingleServerSetupError> {
    let console_schema_version = CONSOLE_MIGRATIONS
        .migrations
        .last()
        .and_then(|migration| u32::try_from(migration.version).ok())
        .ok_or(SingleServerSetupError::Console)?;
    let tool_directory = if layout.linux_container {
        layout.runtime_root.join("postgresql/18/bin")
    } else {
        layout.runtime_root.join("postgresql/bin")
    };
    let executable_extension = if layout.linux_container { "" } else { ".exe" };
    let dump_path = tool_directory.join(format!("pg_dump{executable_extension}"));
    let restore_path = tool_directory.join(format!("pg_restore{executable_extension}"));
    let packaged_tool_directory = if layout.linux_container {
        layout.package_root.join("postgresql/18/bin")
    } else {
        layout.package_root.join("postgresql/bin")
    };
    let dump_hash =
        tool_hash(&packaged_tool_directory.join(format!("pg_dump{executable_extension}")))?;
    let restore_hash =
        tool_hash(&packaged_tool_directory.join(format!("pg_restore{executable_extension}")))?;
    let anchor_unix = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| SingleServerSetupError::Private)?
        .as_secs();
    let backup_configuration = serde_json::json!({
        "schema_version":2,
        "deployment_id":deployment_id,
        "repository_root":layout.data_root.join("backup/repository"),
        "offsite_repository_root":null,
        "scheduler_root":layout.data_root.join("backup/scheduler"),
        "status_root":layout.data_root.join("backup/status"),
        "pg_dump_path":dump_path,
        "pg_dump_sha256":dump_hash,
        "pg_restore_path":restore_path,
        "pg_restore_sha256":restore_hash,
        "command_timeout_seconds":3600,
        "poll_interval_seconds":60,
        "schedule":{"deployment_id":deployment_id,"anchor_unix":anchor_unix,"period_seconds":86400},
        "retention":{"hourly":24,"daily":7,"weekly":4,"monthly":6,"pre_upgrade":5,"manual_days":30},
        "offsite_retention":null,
        "plan":{
            "deployment_id":deployment_id,
            "kind":"independent",
            "write_barrier_proof_file":null,
            "retention":["daily"],
            "previous_recovery_set_id":null,
            "targets":[
                {"state":"required","database":{
                    "service":"console","host":input.postgresql_host,"port":input.postgresql_port,
                    "database":"pixels_console","username":"pixels_console_backup",
                    "password_file":password_file,"schema_version":console_schema_version
                }},
                {"state":"not_applicable","service":"auth","reason":"Auth signer is not installed in the private Server"},
                {"state":"not_applicable","service":"desk","reason":"Desk is the official website and is not installed in Customer Server"}
            ]
        }
    });
    let bytes =
        serde_json::to_vec(&backup_configuration).map_err(|_| SingleServerSetupError::Private)?;
    private::create_private(&layout.config_root.join("backup.json"), &bytes)
        .map_err(|_| SingleServerSetupError::Private)
}

fn tool_hash(path: &Path) -> Result<String, SingleServerSetupError> {
    let bytes = fs::read(path).map_err(|_| SingleServerSetupError::Package)?;
    Ok(hex::encode(Sha256::digest(bytes)))
}
