use base64::{engine::general_purpose, Engine as _};
use gr_auth_mgr::auth_license::{AuthLicense, LicenseVerifier, SignedLicense};
use gr_auth_mgr::authorization::Authorization;
use std::path::Path;

pub const KEY_LAST_ONLINE_VERIFY_MS: &str = "license_last_online_verify_ms";
/// Default offline grace period: 7 days in milliseconds.
pub const DEFAULT_OFFLINE_GRACE_PERIOD_MS: i64 = 7 * 24 * 60 * 60 * 1000;

/// Performs an online license check against the configured auth server.
/// Returns `Ok(true)` when the server confirms validity or when no verify server
/// is configured. HTTPS certificate validation is disabled to support the default
/// self-signed certificate; this is acceptable because the signature itself provides
/// authenticity.
pub async fn verify_license_online(verify_server: &str, deploy_str: &str) -> Result<bool, String> {
    if verify_server.is_empty() {
        return Ok(true);
    }
    let url = format!(
        "{}/api/v1/verify/license",
        verify_server.trim_end_matches('/')
    );
    let client = reqwest::Client::builder()
        .danger_accept_invalid_certs(true)
        .timeout(std::time::Duration::from_secs(10))
        .build()
        .map_err(|e| format!("failed to build http client: {}", e))?;
    let body = serde_json::json!({ "data": deploy_str });
    let resp = client
        .post(&url)
        .json(&body)
        .send()
        .await
        .map_err(|e| format!("online license verification request failed: {}", e))?;
    if !resp.status().is_success() {
        return Err(format!(
            "online license verification returned {}",
            resp.status()
        ));
    }
    let json: serde_json::Value = resp
        .json()
        .await
        .map_err(|e| format!("failed to parse online verification response: {}", e))?;
    let valid = json
        .get("data")
        .and_then(|v| v.as_bool())
        .ok_or("online verification response missing data field")?;
    Ok(valid)
}

/// Checks whether the CMS has been offline for longer than the allowed grace period.
pub fn is_offline_grace_period_exceeded(last_online_verify_ms: i64, now_ms: i64) -> bool {
    if last_online_verify_ms <= 0 {
        // No prior online verification; allow running until the first check can be performed.
        return false;
    }
    now_ms - last_online_verify_ms > DEFAULT_OFFLINE_GRACE_PERIOD_MS
}

const PUBLIC_KEY_ENV: &str = "GR_AUTH_LICENSE_PUBLIC_KEY";
const PUBLIC_KEY_FILE: &str = "certs/auth_license_public.key";

/// Loads the Ed25519 public key used to verify signed licenses.
/// Accepts either the raw 32-byte key or a DER SubjectPublicKeyInfo (44 bytes).
/// Priority:
/// 1. `GR_AUTH_LICENSE_PUBLIC_KEY` environment variable (base64).
/// 2. `certs/auth_license_public.key` file (base64).
pub fn init_license_verifier() -> Result<LicenseVerifier, String> {
    if let Ok(encoded) = std::env::var(PUBLIC_KEY_ENV) {
        let bytes = general_purpose::STANDARD
            .decode(encoded.trim())
            .map_err(|e| format!("failed to decode {}: {}", PUBLIC_KEY_ENV, e))?;
        let verifier = LicenseVerifier::from_public_key_bytes(&extract_raw_public_key(&bytes)?)?;
        tracing::info!("loaded license public key from {}", PUBLIC_KEY_ENV);
        return Ok(verifier);
    }

    if Path::new(PUBLIC_KEY_FILE).exists() {
        let encoded = std::fs::read_to_string(PUBLIC_KEY_FILE)
            .map_err(|e| format!("failed to read {}: {}", PUBLIC_KEY_FILE, e))?;
        let bytes = general_purpose::STANDARD
            .decode(encoded.trim())
            .map_err(|e| format!("failed to decode {}: {}", PUBLIC_KEY_FILE, e))?;
        let verifier = LicenseVerifier::from_public_key_bytes(&extract_raw_public_key(&bytes)?)?;
        tracing::info!("loaded license public key from {}", PUBLIC_KEY_FILE);
        return Ok(verifier);
    }

    Err(format!(
        "license public key not found. Set {} or place a base64 Ed25519 public key in {}.",
        PUBLIC_KEY_ENV, PUBLIC_KEY_FILE
    ))
}

/// Extracts the raw 32-byte Ed25519 public key from either a raw key or a
/// DER SubjectPublicKeyInfo wrapper produced by OpenSSL.
fn extract_raw_public_key(bytes: &[u8]) -> Result<Vec<u8>, String> {
    if bytes.len() == 32 {
        return Ok(bytes.to_vec());
    }
    // DER SubjectPublicKeyInfo for Ed25519: 30 2a 30 05 06 03 2b6570 03 21 00 <32 bytes>
    const ED25519_SPKI_PREFIX: &[u8] = &[
        0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00,
    ];
    if bytes.len() == 44 && bytes.starts_with(ED25519_SPKI_PREFIX) {
        return Ok(bytes[ED25519_SPKI_PREFIX.len()..].to_vec());
    }
    Err(format!(
        "unsupported Ed25519 public key format: expected 32 raw bytes or 44-byte DER SPKI, got {} bytes",
        bytes.len()
    ))
}

/// Converts a verified `AuthLicense` into the legacy `Authorization` model,
/// preserving credentials from an existing authorization if available.
pub fn license_to_authorization(
    license: &AuthLicense,
    existing: Option<&Authorization>,
    deploy_str: String,
) -> Authorization {
    Authorization {
        auth_id: license.auth_id.clone(),
        auth_name: license.auth_name.clone(),
        machine_code: license.machine_code.clone(),
        description: existing.map(|a| a.description.clone()).unwrap_or_default(),
        max_streams: license.max_streams,
        appkey: license.appkey.clone(),
        app_secret: existing.map(|a| a.app_secret.clone()).unwrap_or_default(),
        username: existing.map(|a| a.username.clone()).unwrap_or_default(),
        password: existing.map(|a| a.password.clone()).unwrap_or_default(),
        created_timestamp_ms: license.created_at_ms,
        end_timestamp_ms: license.expires_at_ms,
        last_modify_timestamp: gr_base::get_current_timestamp(),
        days: license.days,
        verify_server: existing
            .map(|a| a.verify_server.clone())
            .unwrap_or_default(),
        deploy_str,
        role: license.role,
        used_time_ms: 0,
    }
}

/// Tries to parse a deploy string as a signed license and verify it.
/// Returns `Ok(authorization)` on success, `Err` if parsing/verification fails.
pub fn parse_and_verify_signed_license(
    verifier: &LicenseVerifier,
    deploy_str: &str,
    machine_code: &str,
    now_ms: i64,
) -> Result<Authorization, String> {
    let signed = SignedLicense::parse_deploy_string(deploy_str)?;
    if !verifier.verify(&signed, machine_code, now_ms)? {
        return Err("signed license verification failed".to_string());
    }
    Ok(license_to_authorization(
        &signed.license,
        None,
        deploy_str.to_string(),
    ))
}

#[cfg(test)]
mod tests {
    use super::*;
    use gr_auth_mgr::auth_license::LicenseSigner;
    use std::env;

    fn sample_license() -> AuthLicense {
        AuthLicense {
            auth_id: "auth-1".to_string(),
            auth_name: "name-1".to_string(),
            machine_code: "mc-1".to_string(),
            max_streams: 2,
            days: 7,
            role: 1,
            created_at_ms: 1000,
            expires_at_ms: 1000 + 7 * 24 * 60 * 60 * 1000,
            appkey: "appkey-1".to_string(),
        }
    }

    #[test]
    fn verifier_loads_from_env_var() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let encoded = general_purpose::STANDARD.encode(&pub_key);
        env::set_var(PUBLIC_KEY_ENV, &encoded);
        let verifier = init_license_verifier().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let signed = signer.sign(&sample_license()).unwrap();
        assert!(verifier
            .verify(&signed, "mc-1", signed.license.expires_at_ms - 1)
            .unwrap());
        env::remove_var(PUBLIC_KEY_ENV);
    }

    #[test]
    fn parse_and_verify_signed_license_roundtrip() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        let signed = signer.sign(&sample_license()).unwrap();
        let deploy = signed.to_deploy_string().unwrap();
        let auth = parse_and_verify_signed_license(
            &verifier,
            &deploy,
            "mc-1",
            signed.license.expires_at_ms - 1,
        )
        .unwrap();
        assert_eq!(auth.auth_id, "auth-1");
        assert_eq!(auth.machine_code, "mc-1");
        assert_eq!(auth.max_streams, 2);
    }

    #[test]
    fn parse_and_verify_rejects_wrong_machine() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        let signed = signer.sign(&sample_license()).unwrap();
        let deploy = signed.to_deploy_string().unwrap();
        assert!(parse_and_verify_signed_license(
            &verifier,
            &deploy,
            "wrong",
            signed.license.expires_at_ms - 1
        )
        .is_err());
    }

    #[test]
    fn extracts_raw_key_from_der_spki() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        assert_eq!(signer.public_key_bytes(), pub_key);

        // Simulate DER SubjectPublicKeyInfo: prefix + raw key.
        let mut der = vec![
            0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00,
        ];
        der.extend_from_slice(&pub_key);
        let extracted = extract_raw_public_key(&der).unwrap();
        assert_eq!(extracted, pub_key);

        // Raw key also works.
        assert_eq!(extract_raw_public_key(&pub_key).unwrap(), pub_key);
    }

    #[test]
    fn license_to_authorization_preserves_credentials() {
        let existing = Authorization {
            username: "admin".to_string(),
            password: "secret".to_string(),
            app_secret: "app-secret".to_string(),
            verify_server: "https://example.com".to_string(),
            ..Default::default()
        };
        let license = sample_license();
        let auth = license_to_authorization(&license, Some(&existing), "deploy".to_string());
        assert_eq!(auth.username, "admin");
        assert_eq!(auth.password, "secret");
        assert_eq!(auth.app_secret, "app-secret");
        assert_eq!(auth.verify_server, "https://example.com");
        assert_eq!(auth.auth_id, license.auth_id);
    }
}
