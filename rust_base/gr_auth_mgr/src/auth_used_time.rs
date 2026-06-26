use base64::{engine::general_purpose, Engine as _};
use ring::digest::{self, Context};
use ring::hmac;

use crate::authorization::Authorization;

const USED_TIME_HMAC_SEED: &[u8] = b"GR_USED_TIME_HMAC_SEED_v1";
const USED_TIME_PARTS: usize = 3;

/// Derives a stable HMAC key from authorization-bound material.
///
/// NOTE: The seed is public in the source code. This provides tamper-evidence
/// against casual modification, not cryptographic secrecy against a determined
/// attacker who can read the binary. Phase 3 will add online calibration and
/// revocation for stronger protection.
fn derive_hmac_key(auth: &Authorization) -> hmac::Key {
    let mut ctx = Context::new(&digest::SHA256);
    ctx.update(USED_TIME_HMAC_SEED);
    ctx.update(auth.auth_id.as_bytes());
    ctx.update(auth.machine_code.as_bytes());
    ctx.update(auth.appkey.as_bytes());
    let key_bytes = ctx.finish();
    hmac::Key::new(hmac::HMAC_SHA256, key_bytes.as_ref())
}

/// Signs the used-time record so local storage tampering can be detected.
/// Format: `used_time_ms|expires_at_ms|base64_hmac`
pub fn sign_used_time(used_time_ms: i64, expires_at_ms: i64, auth: &Authorization) -> String {
    let key = derive_hmac_key(auth);
    let payload = format!("{}|{}", used_time_ms, expires_at_ms);
    let tag = hmac::sign(&key, payload.as_bytes());
    format!(
        "{}|{}|{}",
        used_time_ms,
        expires_at_ms,
        general_purpose::STANDARD.encode(tag.as_ref())
    )
}

/// Verifies the HMAC and returns the used time. Returns an error if the record
/// has been tampered with or is malformed.
pub fn verify_used_time(record: &str, auth: &Authorization) -> Result<i64, String> {
    let parts: Vec<&str> = record.split('|').collect();
    if parts.len() != USED_TIME_PARTS {
        return Err(format!(
            "invalid used-time record: expected {} parts, got {}",
            USED_TIME_PARTS,
            parts.len()
        ));
    }
    let used_time_ms = parts[0]
        .parse::<i64>()
        .map_err(|e| format!("invalid used_time_ms: {}", e))?;
    let expires_at_ms = parts[1]
        .parse::<i64>()
        .map_err(|e| format!("invalid expires_at_ms: {}", e))?;

    let expected = sign_used_time(used_time_ms, expires_at_ms, auth);
    if expected != record {
        return Err("used-time HMAC verification failed".to_string());
    }

    Ok(used_time_ms)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_auth() -> Authorization {
        Authorization {
            auth_id: "auth-1".to_string(),
            auth_name: "name".to_string(),
            machine_code: "mc-1".to_string(),
            appkey: "appkey-1".to_string(),
            days: 30,
            max_streams: 4,
            end_timestamp_ms: 1234567890,
            ..Default::default()
        }
    }

    #[test]
    fn signed_used_time_verifies() {
        let auth = sample_auth();
        let record = sign_used_time(12345, auth.end_timestamp_ms, &auth);
        assert_eq!(verify_used_time(&record, &auth).unwrap(), 12345);
    }

    #[test]
    fn tampered_used_time_fails() {
        let auth = sample_auth();
        let record = sign_used_time(12345, auth.end_timestamp_ms, &auth);
        let tampered = record.replace("12345", "99999");
        assert!(verify_used_time(&tampered, &auth).is_err());
    }

    #[test]
    fn tampered_expires_at_fails() {
        let auth = sample_auth();
        let record = sign_used_time(12345, auth.end_timestamp_ms, &auth);
        let tampered = record.replace(
            &auth.end_timestamp_ms.to_string(),
            &(auth.end_timestamp_ms + 1).to_string(),
        );
        assert!(verify_used_time(&tampered, &auth).is_err());
    }

    #[test]
    fn different_auth_produces_different_hmac() {
        let auth1 = sample_auth();
        let mut auth2 = sample_auth();
        auth2.appkey = "other-appkey".to_string();
        let record1 = sign_used_time(12345, auth1.end_timestamp_ms, &auth1);
        let record2 = sign_used_time(12345, auth2.end_timestamp_ms, &auth2);
        assert_ne!(record1, record2);
        assert!(verify_used_time(&record1, &auth2).is_err());
    }

    #[test]
    fn malformed_record_rejected() {
        let auth = sample_auth();
        assert!(verify_used_time("not-a-record", &auth).is_err());
        assert!(verify_used_time("1|2", &auth).is_err());
        assert!(verify_used_time("1|2|3|4", &auth).is_err());
        assert!(verify_used_time("a|b|c", &auth).is_err());
    }
}
