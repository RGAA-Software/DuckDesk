use px_credentials as credentials;
use px_pg::{DatabaseConfig, Transport};
use px_private_files::private as key_file;
use std::{env, path::PathBuf};
use zeroize::Zeroizing;

#[tokio::main]
async fn main() {
    if let Err(error) = run().await {
        eprintln!("Auth initialization failed: {error}");
        std::process::exit(1);
    }
}
async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().skip(1).collect();
    if arguments == ["generate-key"] {
        let path = PathBuf::from(env::var("PIXELS_AUTH_SIGNING_KEY")?);
        let material =
            ring::signature::Ed25519KeyPair::generate_pkcs8(&ring::rand::SystemRandom::new())
                .map_err(|_| "key generation failed")?;
        let signer = px_license::LicenseSigner::from_pkcs8(material.as_ref())?;
        key_file::create_private(&path, material.as_ref())?;
        println!(
            "key_id={} public_key={}",
            signer.key_id(),
            hex::encode(signer.public_key())
        );
        return Ok(());
    }
    if arguments == ["create-trust-store"] {
        let signing_key_path = PathBuf::from(env::var("PIXELS_AUTH_SIGNING_KEY")?);
        let trust_store_path = PathBuf::from(env::var("PIXELS_AUTH_TRUST_STORE")?);
        let signing_key_material = key_file::read_private(&signing_key_path)?;
        let signer = px_license::LicenseSigner::from_pkcs8(&signing_key_material)?;
        let additional_public_keys = env::var("PIXELS_AUTH_ADDITIONAL_PUBLIC_KEYS")
            .unwrap_or_default()
            .split(',')
            .filter(|value| !value.is_empty())
            .map(|public_key_hex| {
                hex::decode(public_key_hex)?
                    .try_into()
                    .map_err(|_| "invalid additional public key".into())
            })
            .collect::<Result<Vec<[u8; 32]>, Box<dyn std::error::Error>>>()?;
        let trust_store = px_license::LicenseTrustStore::new(
            signer.public_key().try_into()?,
            additional_public_keys,
        )?;
        key_file::create_private(&trust_store_path, &trust_store.canonical_bytes()?)?;
        println!(
            "active_key_id={} trusted_key_count={}",
            trust_store.active_key_id,
            trust_store.trusted_keys.len()
        );
        return Ok(());
    }
    if arguments != ["bootstrap"] {
        return Err(
            "usage: px_auth_admin <bootstrap|generate-key|create-trust-store>; explicit provisioning only; configuration via environment"
                .into(),
        );
    }
    let local = match env::var("PIXELS_AUTH_LOCAL_DEVELOPMENT").as_deref() {
        Ok("1") => true,
        Ok("0") | Err(env::VarError::NotPresent) => false,
        _ => return Err("invalid local development setting".into()),
    };
    let config = DatabaseConfig::parse(
        &Zeroizing::new(env::var("PIXELS_DATABASE_URL")?),
        if local {
            Transport::LocalDevelopment
        } else {
            Transport::VerifyFull
        },
    )?;
    let deployment = env::var("PIXELS_DEPLOYMENT_ID")?.parse()?;
    let username = credentials::normalize_username(&env::var("PIXELS_AUTH_INITIAL_USERNAME")?)
        .ok_or("invalid initial username")?;
    let bytes = key_file::read_private(&PathBuf::from(env::var(
        "PIXELS_AUTH_INITIAL_PASSWORD_FILE",
    )?))?;
    // Exact UTF-8, no implicit trim or environment/command-line password fallback.
    let password = std::str::from_utf8(&bytes).map_err(|_| "invalid password encoding")?;
    let hash = credentials::hash(password)?;
    let author = px_auth_store::bootstrap_author(&config, deployment, &username, &hash).await?;
    println!("Auth initialized: author={}", author.id);
    Ok(())
}
