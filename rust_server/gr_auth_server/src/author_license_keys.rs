use base64::{Engine as _, engine::general_purpose};
use gr_auth_mgr::auth_license::{AuthLicense, LicenseSigner, SignedLicense};
use gr_auth_mgr::authorization::Authorization;
use std::path::Path;

const PRIVATE_KEY_ENV: &str = "GR_AUTH_LICENSE_PRIVATE_KEY";
const PRIVATE_KEY_FILE: &str = "certs/auth_license_private.key";
const PUBLIC_KEY_FILE: &str = "certs/auth_license_public.key";

/// Initializes the license signer for this auth server process.
///
/// Priority:
/// 1. `GR_AUTH_LICENSE_PRIVATE_KEY` environment variable (base64 PKCS#8).
/// 2. `certs/auth_license_private.key` file (base64 PKCS#8).
/// 3. Generate a new key pair and persist it (development only; logs a warning).
pub fn init_license_signer() -> Result<LicenseSigner, String> {
    if let Ok(encoded) = std::env::var(PRIVATE_KEY_ENV) {
        let bytes = general_purpose::STANDARD
            .decode(encoded.trim())
            .map_err(|e| format!("failed to decode {}: {}", PRIVATE_KEY_ENV, e))?;
        let signer = LicenseSigner::from_pkcs8_bytes(&bytes)?;
        tracing::info!("loaded license private key from {}", PRIVATE_KEY_ENV);
        return Ok(signer);
    }

    if Path::new(PRIVATE_KEY_FILE).exists() {
        let encoded = std::fs::read_to_string(PRIVATE_KEY_FILE)
            .map_err(|e| format!("failed to read {}: {}", PRIVATE_KEY_FILE, e))?;
        let bytes = general_purpose::STANDARD
            .decode(encoded.trim())
            .map_err(|e| format!("failed to decode {}: {}", PRIVATE_KEY_FILE, e))?;
        let signer = LicenseSigner::from_pkcs8_bytes(&bytes)?;
        tracing::info!("loaded license private key from {}", PRIVATE_KEY_FILE);
        return Ok(signer);
    }

    tracing::warn!(
        "no license private key found; generating a new one and saving it to {}. \
         For production, set {} or pre-generate a key pair.",
        PRIVATE_KEY_FILE,
        PRIVATE_KEY_ENV
    );
    let (private_key, public_key) = LicenseSigner::generate_keypair()?;
    let private_b64 = general_purpose::STANDARD.encode(&private_key);
    let public_b64 = general_purpose::STANDARD.encode(&public_key);

    if let Some(parent) = Path::new(PRIVATE_KEY_FILE).parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    std::fs::write(PRIVATE_KEY_FILE, private_b64)
        .map_err(|e| format!("failed to write {}: {}", PRIVATE_KEY_FILE, e))?;
    std::fs::write(PUBLIC_KEY_FILE, public_b64)
        .map_err(|e| format!("failed to write {}: {}", PUBLIC_KEY_FILE, e))?;

    LicenseSigner::from_pkcs8_bytes(&private_key)
}

/// Returns the base64-encoded public key for distribution to CMS instances.
pub fn get_license_public_key_b64(signer: &LicenseSigner) -> String {
    general_purpose::STANDARD.encode(signer.public_key_bytes())
}

/// Convenience helper: sign an `Authorization`-derived license payload.
pub fn sign_authorization(
    signer: &LicenseSigner,
    auth_id: String,
    auth_name: String,
    machine_code: String,
    max_streams: i32,
    days: i32,
    role: i32,
    created_at_ms: i64,
    expires_at_ms: i64,
    appkey: String,
) -> Result<SignedLicense, String> {
    let license = AuthLicense {
        auth_id,
        auth_name,
        machine_code,
        max_streams,
        days,
        role,
        created_at_ms,
        expires_at_ms,
        appkey,
    };
    signer.sign(&license)
}

/// Signs a full `Authorization` model, producing a `SignedLicense` containing only
/// the fields the CMS needs to enforce.
pub fn sign_authorization_model(
    signer: &LicenseSigner,
    auth: &Authorization,
) -> Result<SignedLicense, String> {
    sign_authorization(
        signer,
        auth.auth_id.clone(),
        auth.auth_name.clone(),
        auth.machine_code.clone(),
        auth.max_streams,
        auth.days,
        auth.role,
        auth.created_timestamp_ms,
        auth.end_timestamp_ms,
        auth.appkey.clone(),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::env;

    #[test]
    fn generated_keypair_can_sign_and_verify() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let signed = sign_authorization(
            &signer,
            "id".to_string(),
            "name".to_string(),
            "mc".to_string(),
            1,
            1,
            1,
            0,
            1000,
            "key".to_string(),
        )
        .unwrap();
        let verifier =
            gr_auth_mgr::auth_license::LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        assert!(verifier.verify_signature(&signed).unwrap());
    }

    #[test]
    fn init_signer_loads_from_env_var() {
        let (priv_key, _pub_key) = LicenseSigner::generate_keypair().unwrap();
        let encoded = general_purpose::STANDARD.encode(&priv_key);
        unsafe {
            env::set_var(PRIVATE_KEY_ENV, &encoded);
        }
        let signer = init_license_signer().unwrap();
        let signed = sign_authorization(
            &signer,
            "id".to_string(),
            "name".to_string(),
            "mc".to_string(),
            1,
            1,
            1,
            0,
            1000,
            "key".to_string(),
        )
        .unwrap();
        assert!(signed.to_deploy_string().unwrap().contains('.'));
        unsafe {
            env::remove_var(PRIVATE_KEY_ENV);
        }
    }
}
