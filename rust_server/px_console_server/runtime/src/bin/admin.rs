use px_console_runtime::{
    ConsoleLaunchConfig, DeploymentIdentityLaunchConfig, LicenseLaunchConfig,
};
use px_console_store::{initialize_administrator, PasswordDigest, Username};
use px_license::Distribution;
use px_pg::{DatabaseConfig, Transport};
use px_private_files::{private, CacheRoot};
use rand::RngCore;
use ring::{
    rand::SystemRandom,
    signature::{Ed25519KeyPair, KeyPair},
};
use std::{env, path::PathBuf};
use uuid::Uuid;
use zeroize::Zeroizing;

#[tokio::main]
async fn main() {
    if let Err(error) = run().await {
        eprintln!("Console initialization failed: {error}");
        std::process::exit(1);
    }
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().skip(1).collect();
    match arguments.as_slice() {
        [command] if command == "bootstrap" => bootstrap().await,
        [command] if command == "generate-secrets" => generate_secrets(),
        [command] if command == "generate-deployment-key" => generate_deployment_key(),
        [command] if command == "validate-deployment-identity" => {
            validate_deployment_identity().await
        }
        [command] if command == "validate-environment" => validate_environment(),
        [command] if command == "validate-license" => validate_license().await,
        [command] if command == "initialize-recording-cache" => initialize_recording_cache(),
        _ => Err("usage: px_console_admin <bootstrap|generate-secrets|generate-deployment-key|validate-deployment-identity|validate-environment|validate-license|initialize-recording-cache>; explicit provisioning only; configuration via environment".into()),
    }
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
    let authority_deployment_id =
        env::var("PIXELS_CONSOLE_LICENSE_AUTHORITY_DEPLOYMENT_ID")?.parse::<Uuid>()?;
    let configuration = LicenseLaunchConfig::new(
        &env::var("PIXELS_CONSOLE_DISTRIBUTION")?,
        env::var("PIXELS_CONSOLE_RELEASE_NAMESPACE")?,
        optional("PIXELS_CONSOLE_OEM_ID"),
        env::var("PIXELS_CONSOLE_MACHINE_SHA256")?,
        authority_deployment_id,
        PathBuf::from(env::var("PIXELS_CONSOLE_LICENSE_TRUST_STORE")?),
        PathBuf::from(env::var("PIXELS_CONSOLE_LICENSE_FILE")?),
        PathBuf::from(env::var("PIXELS_CONSOLE_LICENSE_STATE_DIRECTORY")?),
        optional("PIXELS_CONSOLE_AUTH_VERIFY_URL"),
        optional("PIXELS_CONSOLE_AUTH_VERIFY_CA").map(PathBuf::from),
        flag("PIXELS_CONSOLE_LOCAL_DEVELOPMENT")?,
    )?;
    configuration.admit(deployment_id).await?;
    println!("Console license validated");
    Ok(())
}

fn optional(name: &str) -> Option<String> {
    env::var(name).ok().filter(|value| !value.is_empty())
}

async fn validate_deployment_identity() -> Result<(), Box<dyn std::error::Error>> {
    let deployment_id = env::var("PIXELS_DEPLOYMENT_ID")?.parse::<Uuid>()?;
    if deployment_id.is_nil() {
        return Err("deployment identifier must not be nil".into());
    }
    let distribution = env::var("PIXELS_CONSOLE_DISTRIBUTION")?
        .parse::<Distribution>()
        .map_err(|_| "distribution must be official, customer, or oem")?;
    let configuration = DeploymentIdentityLaunchConfig::new(
        PathBuf::from(env::var("PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE")?),
        PathBuf::from(env::var("PIXELS_CONSOLE_DEPLOYMENT_SIGNING_KEY")?),
        PathBuf::from(env::var("PIXELS_CONSOLE_DEPLOYMENT_TRUST_STORE")?),
        positive_number("PIXELS_CONSOLE_DEPLOYMENT_CERTIFICATE_VERSION")?,
        positive_number("PIXELS_CONSOLE_DESCRIPTOR_REVISION")?,
        positive_number("PIXELS_CONSOLE_DEPLOYMENT_TRUST_EPOCH")?,
        positive_number("PIXELS_CONSOLE_MINIMUM_CLIENT_BUILD")?,
        flag("PIXELS_CONSOLE_REGISTRATION")?,
        flag("PIXELS_CONSOLE_GUESTS")?,
    )?;
    configuration
        .load(
            deployment_id,
            distribution,
            &env::var("PIXELS_CONSOLE_RELEASE_NAMESPACE")?,
            optional("PIXELS_CONSOLE_OEM_ID").as_deref(),
        )
        .await?;
    println!("Console deployment identity validated");
    Ok(())
}

fn positive_number(name: &str) -> Result<u64, Box<dyn std::error::Error>> {
    let value = env::var(name)?.parse::<u64>()?;
    if value == 0 {
        return Err(format!("{name} must be greater than zero").into());
    }
    Ok(value)
}

fn flag(name: &str) -> Result<bool, Box<dyn std::error::Error>> {
    match env::var(name).as_deref() {
        Ok("1") => Ok(true),
        Ok("0") | Err(env::VarError::NotPresent) => Ok(false),
        _ => Err(format!("{name} must be zero or one").into()),
    }
}

fn generate_deployment_key() -> Result<(), Box<dyn std::error::Error>> {
    let signing_key_path = PathBuf::from(env::var("PIXELS_CONSOLE_DEPLOYMENT_SIGNING_KEY")?);
    let signing_key_document = Zeroizing::new(
        Ed25519KeyPair::generate_pkcs8(&SystemRandom::new())
            .map_err(|_| "deployment key generation failed")?
            .as_ref()
            .to_vec(),
    );
    let signing_key = Ed25519KeyPair::from_pkcs8(&signing_key_document)
        .map_err(|_| "generated deployment key is invalid")?;
    private::create_private(&signing_key_path, &signing_key_document)?;
    println!(
        "deployment_public_key_hex={}",
        hex::encode(signing_key.public_key().as_ref())
    );
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
