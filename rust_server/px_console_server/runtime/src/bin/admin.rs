use px_console_store::{initialize_administrator, PasswordDigest, Username};
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
        [command] if command == "initialize-recording-cache" => initialize_recording_cache(),
        _ => Err("usage: px_console_admin <bootstrap|generate-secrets|generate-deployment-key|initialize-recording-cache>; explicit provisioning only; configuration via environment".into()),
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
