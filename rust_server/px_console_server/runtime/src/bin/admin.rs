use px_console_runtime::{
    check_postgresql_administrator, initialize_single_server, provision_fresh_console_database,
    run_single_server_setup, ConsoleDatabaseCredentials, ConsoleLaunchConfig, LicenseLaunchConfig,
    SingleServerLayout, SingleServerSetupInput,
};
use px_console_store::{initialize_administrator, PasswordDigest, Username};
use px_pg::{DatabaseConfig, Transport};
use px_private_files::{private, CacheRoot};
use rand::RngCore;
use std::{env, io::Read, path::PathBuf};
use uuid::Uuid;
use zeroize::Zeroizing;

#[tokio::main]
async fn main() {
    #[cfg(windows)]
    if std::env::args_os().nth(1).as_deref() == Some(std::ffi::OsStr::new("setup-service")) {
        if let Err(error) = px_server_service::dispatch("Pixels.Setup", run_setup_windows_service) {
            eprintln!("Pixels Setup service failed: {error}");
            std::process::exit(1);
        }
        return;
    }
    if let Err(error) = run().await {
        eprintln!("Console initialization failed: {error}");
        std::process::exit(1);
    }
}

#[cfg(windows)]
fn run_setup_windows_service(
    runtime: &tokio::runtime::Runtime,
    stop_token: tokio_util::sync::CancellationToken,
) -> Result<(), String> {
    let arguments = env::args().skip(2).collect::<Vec<_>>();
    let [configuration_root, data_root, runtime_root, package_root, manifest_hash] =
        arguments.as_slice()
    else {
        return Err("Pixels Setup service arguments are invalid".into());
    };
    runtime
        .block_on(run_single_server_setup(
            SingleServerLayout {
                config_root: PathBuf::from(configuration_root),
                data_root: PathBuf::from(data_root),
                runtime_root: PathBuf::from(runtime_root),
                package_root: PathBuf::from(package_root),
                linux_container: false,
            },
            manifest_hash.clone(),
            stop_token,
        ))
        .map_err(|error| error.to_string())
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().skip(1).collect();
    match arguments.as_slice() {
        [command] if command == "bootstrap" => bootstrap().await,
        [command] if command == "generate-secrets" => generate_secrets(),
        [command] if command == "validate-environment" => validate_environment(),
        [command] if command == "validate-license" => validate_license().await,
        [command] if command == "initialize-recording-cache" => initialize_recording_cache(),
        [command] if command == "check-setup-database" => check_setup_database().await,
        [command] if command == "initialize-setup-database" => initialize_setup_database().await,
        [command, configuration_root, data_root, runtime_root, package_root, platform]
            if command == "initialize-single-server" =>
        {
            initialize_server(
                configuration_root,
                data_root,
                runtime_root,
                package_root,
                platform,
            )
            .await
        }
        [command, configuration_root, data_root, runtime_root, package_root, platform, manifest_hash]
            if command == "setup-server" =>
        {
            let linux_container = match platform.as_str() {
                "linux" => true,
                "windows" => false,
                _ => return Err("invalid Single Server platform".into()),
            };
            run_single_server_setup(
                SingleServerLayout {
                    config_root: PathBuf::from(configuration_root),
                    data_root: PathBuf::from(data_root),
                    runtime_root: PathBuf::from(runtime_root),
                    package_root: PathBuf::from(package_root),
                    linux_container,
                },
                manifest_hash.clone(),
                tokio_util::sync::CancellationToken::new(),
            )
            .await
        }
        _ => Err("usage: px_console_admin <bootstrap|generate-secrets|validate-environment|validate-license|initialize-recording-cache|check-setup-database|initialize-setup-database|initialize-single-server>; explicit provisioning only; configuration via environment or setup JSON on stdin".into()),
    }
}

async fn initialize_server(
    configuration_root: &str,
    data_root: &str,
    runtime_root: &str,
    package_root: &str,
    platform: &str,
) -> Result<(), Box<dyn std::error::Error>> {
    let linux_container = match platform {
        "linux" => true,
        "windows" => false,
        _ => return Err("invalid Single Server platform".into()),
    };
    let mut request_json = Zeroizing::new(String::new());
    std::io::stdin()
        .take(65_536)
        .read_to_string(&mut request_json)?;
    let request = serde_json::from_str::<SingleServerSetupInput>(&request_json)?;
    let result = initialize_single_server(
        request,
        &SingleServerLayout {
            config_root: PathBuf::from(configuration_root),
            data_root: PathBuf::from(data_root),
            runtime_root: PathBuf::from(runtime_root),
            package_root: PathBuf::from(package_root),
            linux_container,
        },
    )
    .await?;
    println!(
        "Single Server initialized: deployment={}",
        result.deployment_id
    );
    Ok(())
}

async fn check_setup_database() -> Result<(), Box<dyn std::error::Error>> {
    let administrator_url = Zeroizing::new(env::var("PIXELS_SETUP_DATABASE_URL")?);
    env::remove_var("PIXELS_SETUP_DATABASE_URL");
    check_postgresql_administrator(administrator_url).await?;
    println!("PostgreSQL 18 administrator connection validated");
    Ok(())
}

async fn initialize_setup_database() -> Result<(), Box<dyn std::error::Error>> {
    let administrator_url = Zeroizing::new(env::var("PIXELS_SETUP_DATABASE_URL")?);
    let owner_password = Zeroizing::new(env::var("PIXELS_SETUP_OWNER_PASSWORD")?);
    let runtime_password = Zeroizing::new(env::var("PIXELS_SETUP_RUNTIME_PASSWORD")?);
    for variable_name in [
        "PIXELS_SETUP_DATABASE_URL",
        "PIXELS_SETUP_OWNER_PASSWORD",
        "PIXELS_SETUP_RUNTIME_PASSWORD",
    ] {
        env::remove_var(variable_name);
    }
    let deployment_id = env::var("PIXELS_DEPLOYMENT_ID")?.parse::<Uuid>()?;
    let credentials = ConsoleDatabaseCredentials::new(owner_password, runtime_password)?;
    let _role_urls =
        provision_fresh_console_database(administrator_url, deployment_id, &credentials).await?;
    println!("Fresh Console database initialized");
    Ok(())
}

fn validate_environment() -> Result<(), Box<dyn std::error::Error>> {
    ConsoleLaunchConfig::from_env()?;
    println!("Console environment validated");
    Ok(())
}

async fn validate_license() -> Result<(), Box<dyn std::error::Error>> {
    let deployment_id = env::var("PIXELS_DEPLOYMENT_ID")?.parse::<Uuid>()?;
    if deployment_id.is_nil() {
        return Err("deployment identifier must not be nil".into());
    }
    let configuration = LicenseLaunchConfig::new(
        PathBuf::from(env::var("PIXELS_CONSOLE_LICENSE_TRUST_STORE")?),
        PathBuf::from(env::var("PIXELS_CONSOLE_LICENSE_FILE")?),
    )?;
    configuration.admit(deployment_id).await?;
    println!("Console license validated");
    Ok(())
}

fn initialize_recording_cache() -> Result<(), Box<dyn std::error::Error>> {
    let deployment = env::var("PIXELS_DEPLOYMENT_ID")?.parse::<Uuid>()?;
    if deployment.is_nil() {
        return Err("deployment identifier must not be nil".into());
    }
    let path = PathBuf::from(env::var("PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY")?);
    let root = CacheRoot::initialize(&path, deployment)?;
    println!("Console recording cache initialized: root_id={}", root.id());
    Ok(())
}

async fn bootstrap() -> Result<(), Box<dyn std::error::Error>> {
    let local_development = match env::var("PIXELS_CONSOLE_LOCAL_DEVELOPMENT").as_deref() {
        Ok("1") => true,
        Ok("0") | Err(env::VarError::NotPresent) => false,
        _ => return Err("invalid local development setting".into()),
    };
    let database = DatabaseConfig::parse(
        &Zeroizing::new(env::var("PIXELS_DATABASE_URL")?),
        if local_development {
            Transport::LocalDevelopment
        } else {
            Transport::VerifyFull
        },
    )?;
    let deployment = env::var("PIXELS_DEPLOYMENT_ID")?.parse::<Uuid>()?;
    if deployment.is_nil() {
        return Err("deployment identifier must not be nil".into());
    }
    let username = Username::parse(&env::var("PIXELS_CONSOLE_INITIAL_USERNAME")?)?;
    let password_bytes = private::read_private(&PathBuf::from(env::var(
        "PIXELS_CONSOLE_INITIAL_PASSWORD_FILE",
    )?))?;
    let password = Zeroizing::new(
        std::str::from_utf8(&password_bytes)
            .map_err(|_| "invalid password encoding")?
            .to_owned(),
    );
    let password_hash = px_credentials::hash(&password)?;
    initialize_administrator(
        &database,
        deployment,
        &username,
        &PasswordDigest::parse(password_hash.to_string())?,
    )
    .await?;
    println!("Console initialized: username={}", username.normalized());
    Ok(())
}

fn generate_secrets() -> Result<(), Box<dyn std::error::Error>> {
    let guest_source_path = PathBuf::from(env::var("PIXELS_CONSOLE_GUEST_SOURCE_KEY")?);
    let workspace_key_path = PathBuf::from(env::var("PIXELS_CONSOLE_WORKSPACE_KEY")?);
    if guest_source_path == workspace_key_path {
        return Err("guest and workspace keys require distinct paths".into());
    }
    let workspace_key_id = env::var("PIXELS_CONSOLE_WORKSPACE_KEY_ID")?.parse::<Uuid>()?;
    if workspace_key_id.is_nil() {
        return Err("workspace key identifier must not be nil".into());
    }
    let mut guest_source_key = Zeroizing::new([0_u8; 32]);
    let mut workspace_key = Zeroizing::new([0_u8; 32]);
    rand::rng().fill_bytes(guest_source_key.as_mut());
    rand::rng().fill_bytes(workspace_key.as_mut());
    private::create_private(&guest_source_path, guest_source_key.as_ref())?;
    if let Err(error) = private::create_private(&workspace_key_path, workspace_key.as_ref()) {
        let _ = std::fs::remove_file(&guest_source_path);
        return Err(error.into());
    }
    println!(
        "Console secrets generated: workspace_key_id={workspace_key_id}; configure PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY and PIXELS_CONSOLE_WORKSPACE_KEYS explicitly"
    );
    Ok(())
}
