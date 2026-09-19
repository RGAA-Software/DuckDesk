use px_deployment_identity::{
    sign_certificate, DeploymentCertificate, DeploymentKind, DeploymentTrustStore,
};
use px_private_files::private;
use ring::{
    rand::SystemRandom,
    signature::{Ed25519KeyPair, KeyPair},
};
use std::{env, path::PathBuf};
use uuid::Uuid;
use zeroize::Zeroizing;

fn main() {
    if let Err(error) = run() {
        eprintln!("Deployment authority operation failed: {error}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().skip(1).collect();
    match arguments.as_slice() {
        [command] if command == "generate-vendor-key" => generate_vendor_key(),
        [command] if command == "create-trust-store" => create_trust_store(),
        [command] if command == "sign-certificate" => sign_deployment_certificate(),
        _ => Err("usage: px_deployment_authority <generate-vendor-key|create-trust-store|sign-certificate>; offline explicit provisioning only; configuration via environment".into()),
    }
}

fn generate_vendor_key() -> Result<(), Box<dyn std::error::Error>> {
    let signing_key_path = PathBuf::from(required("PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY")?);
    let signing_key_document = Zeroizing::new(
        Ed25519KeyPair::generate_pkcs8(&SystemRandom::new())
            .map_err(|_| "vendor key generation failed")?
            .as_ref()
            .to_vec(),
    );
    let signing_key = Ed25519KeyPair::from_pkcs8(&signing_key_document)
        .map_err(|_| "generated vendor key is invalid")?;
    private::create_private(&signing_key_path, &signing_key_document)?;
    let trust_store =
        DeploymentTrustStore::new(1, [public_key_array(signing_key.public_key().as_ref())?])?;
    println!(
        "vendor_key_id={} vendor_public_key_hex={}",
        trust_store.trusted_keys[0].key_id, trust_store.trusted_keys[0].public_key_hex
    );
    Ok(())
}

fn create_trust_store() -> Result<(), Box<dyn std::error::Error>> {
    let signing_key_path = PathBuf::from(required("PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY")?);
    let trust_store_path = PathBuf::from(required("PIXELS_DEPLOYMENT_TRUST_STORE_OUTPUT")?);
    let trust_epoch = positive_number("PIXELS_DEPLOYMENT_TRUST_EPOCH")?;
    let signing_key_material = private::read_private(&signing_key_path)?;
    let signing_key = Ed25519KeyPair::from_pkcs8(&signing_key_material)
        .map_err(|_| "invalid vendor signing key")?;
    let mut public_keys = vec![public_key_array(signing_key.public_key().as_ref())?];
    for encoded_public_key in env::var("PIXELS_DEPLOYMENT_ADDITIONAL_VENDOR_PUBLIC_KEYS")
        .unwrap_or_default()
        .split(',')
        .filter(|value| !value.is_empty())
    {
        public_keys.push(decode_public_key(encoded_public_key)?);
    }
    let trust_store = DeploymentTrustStore::new(trust_epoch, public_keys)?;
    private::create_private(&trust_store_path, &trust_store.canonical_bytes()?)?;
    println!(
        "trust_epoch={} trusted_key_count={}",
        trust_store.trust_epoch,
        trust_store.trusted_keys.len()
    );
    Ok(())
}

fn sign_deployment_certificate() -> Result<(), Box<dyn std::error::Error>> {
    let signing_key_path = PathBuf::from(required("PIXELS_DEPLOYMENT_VENDOR_SIGNING_KEY")?);
    let certificate_path = PathBuf::from(required("PIXELS_DEPLOYMENT_CERTIFICATE_OUTPUT")?);
    let signing_key_material = private::read_private(&signing_key_path)?;
    let signing_key = Ed25519KeyPair::from_pkcs8(&signing_key_material)
        .map_err(|_| "invalid vendor signing key")?;
    let vendor_public_key = public_key_array(signing_key.public_key().as_ref())?;
    let trust_store = DeploymentTrustStore::new(1, [vendor_public_key])?;
    let deployment_id = required("PIXELS_DEPLOYMENT_ID")?.parse::<Uuid>()?;
    if deployment_id.is_nil() {
        return Err("deployment identifier must not be nil".into());
    }
    let deployment_kind = match required("PIXELS_DEPLOYMENT_KIND")?.as_str() {
        "official" => DeploymentKind::Official,
        "private" => DeploymentKind::Private,
        _ => return Err("deployment kind must be official or private".into()),
    };
    let certificate = DeploymentCertificate {
        schema_version: 1,
        deployment_id,
        deployment_kind,
        deployment_public_key_hex: hex::encode(decode_public_key(&required(
            "PIXELS_DEPLOYMENT_PUBLIC_KEY_HEX",
        )?)?),
        certificate_version: positive_number("PIXELS_DEPLOYMENT_CERTIFICATE_VERSION")?,
        not_before: timestamp("PIXELS_DEPLOYMENT_NOT_BEFORE")?,
        expires_at: timestamp("PIXELS_DEPLOYMENT_EXPIRES_AT")?,
        issuer_key_id: trust_store.trusted_keys[0].key_id.clone(),
    };
    let certificate_wire = sign_certificate(&signing_key_material, &certificate)?;
    private::create_private(&certificate_path, certificate_wire.as_bytes())?;
    println!(
        "deployment_id={} deployment_kind={} certificate_version={} issuer_key_id={}",
        certificate.deployment_id,
        match certificate.deployment_kind {
            DeploymentKind::Official => "official",
            DeploymentKind::Private => "private",
        },
        certificate.certificate_version,
        certificate.issuer_key_id
    );
    Ok(())
}

fn required(name: &str) -> Result<String, Box<dyn std::error::Error>> {
    let value = env::var(name)?;
    if value.is_empty() || value.trim() != value {
        return Err(format!("invalid {name}").into());
    }
    Ok(value)
}

fn positive_number(name: &str) -> Result<u64, Box<dyn std::error::Error>> {
    let value = required(name)?.parse::<u64>()?;
    if value == 0 {
        return Err(format!("{name} must be greater than zero").into());
    }
    Ok(value)
}

fn timestamp(name: &str) -> Result<i64, Box<dyn std::error::Error>> {
    let value = required(name)?.parse::<i64>()?;
    if value < 0 {
        return Err(format!("{name} must not be negative").into());
    }
    Ok(value)
}

fn decode_public_key(value: &str) -> Result<[u8; 32], Box<dyn std::error::Error>> {
    if value.len() != 64 || value.bytes().any(|byte| byte.is_ascii_uppercase()) {
        return Err("public key must be 32-byte canonical lowercase hex".into());
    }
    let public_key: [u8; 32] = hex::decode(value)?
        .try_into()
        .map_err(|_| "public key must be 32-byte canonical lowercase hex")?;
    if public_key == [0; 32] || hex::encode(public_key) != value {
        return Err("public key must be 32-byte canonical lowercase hex".into());
    }
    Ok(public_key)
}

fn public_key_array(value: &[u8]) -> Result<[u8; 32], Box<dyn std::error::Error>> {
    value
        .try_into()
        .map_err(|_| "invalid Ed25519 public key length".into())
}
