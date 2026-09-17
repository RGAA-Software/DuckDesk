//! Shared password policy and bounded login admission for Auth and Console.
use argon2::{
    password_hash::{PasswordHash, PasswordHasher, PasswordVerifier, SaltString},
    Argon2,
};
use rand::RngCore;
use sha2::{Digest, Sha256};
use std::{
    collections::HashMap,
    net::IpAddr,
    sync::Mutex,
    time::{Duration, Instant},
};
use zeroize::Zeroizing;

pub fn normalize_username(input: &str) -> Option<String> {
    let normalized = input.to_lowercase();
    (input.trim() == input
        && (2..=64).contains(&input.chars().count())
        && (2..=64).contains(&normalized.chars().count())
        && !normalized
            .chars()
            .any(|c| c.is_control() || matches!(c, '/' | '\\')))
    .then_some(normalized)
}
pub fn valid_password(input: &str) -> bool {
    (12..=256).contains(&input.len()) && !input.contains('\0')
}
pub fn hash(password: &str) -> Result<Zeroizing<String>, &'static str> {
    if !valid_password(password) {
        return Err("invalid password");
    }
    let mut bytes = [0; 16];
    rand::rng().fill_bytes(&mut bytes);
    let salt = SaltString::encode_b64(&bytes).map_err(|_| "password processing failed")?;
    Argon2::default()
        .hash_password(password.as_bytes(), &salt)
        .map(|value| Zeroizing::new(value.to_string()))
        .map_err(|_| "password processing failed")
}
pub fn verify(password: &str, encoded: &str) -> bool {
    if !valid_password(password) {
        return false;
    }
    let Some(parsed) = parsed_hash(encoded) else {
        return false;
    };
    Argon2::default()
        .verify_password(password.as_bytes(), &parsed)
        .is_ok()
}

/// The fresh-schema credential format, also enforced by Console/Auth CHECK constraints.
/// Parameter changes require an explicit future password-policy/schema upgrade.
pub fn valid_hash(encoded: &str) -> bool {
    parsed_hash(encoded).is_some()
}

fn parsed_hash(encoded: &str) -> Option<PasswordHash<'_>> {
    const PREFIX: &str = "$argon2id$v=19$m=19456,t=2,p=1$";
    // Bound work before parsing; only our canonical parameter order and unpadded base64 are accepted.
    if encoded.len() != 97 || !encoded.starts_with(PREFIX) {
        return None;
    }
    let parsed = PasswordHash::new(encoded).ok()?;
    // A corrupt/untrusted hash must not request arbitrary memory/CPU from the verifier.
    if parsed.algorithm.as_str() != "argon2id"
        || parsed.version != Some(19)
        || parsed.params.iter().count() != 3
        || parsed.params.get_decimal("m") != Some(19456)
        || parsed.params.get_decimal("t") != Some(2)
        || parsed.params.get_decimal("p") != Some(1)
        || parsed.hash.as_ref().map(|value| value.len()) != Some(32)
    {
        return None;
    }
    let mut salt = [0; 64];
    if parsed
        .salt
        .and_then(|value| value.decode_b64(&mut salt).ok())
        .is_none_or(|value| value.len() != 16)
    {
        return None;
    }
    Some(parsed)
}

#[derive(Default)]
pub struct LoginLimits {
    buckets: Mutex<HashMap<[u8; 32], Bucket>>,
}
struct Bucket {
    started: Instant,
    attempts: u32,
}
impl LoginLimits {
    pub fn allow(&self, username: &str, source: IpAddr) -> bool {
        self.allow_at(username, source, Instant::now())
    }
    fn allow_at(&self, username: &str, source: IpAddr, now: Instant) -> bool {
        let Ok(mut buckets) = self.buckets.lock() else {
            return false;
        };
        buckets.retain(|_, value| now.duration_since(value.started) < Duration::from_secs(60));
        // Domain separation avoids user-controlled collisions with IP identities. Hard cap fails closed.
        let account: [u8; 32] = Sha256::digest(format!("account:{username}")).into();
        let address: [u8; 32] = Sha256::digest(format!("source:{source}")).into();
        if buckets.len() >= 4094
            && (!buckets.contains_key(&account) || !buckets.contains_key(&address))
        {
            return false;
        }
        for (key, limit) in [(account, 10), (address, 30)] {
            if buckets
                .get(&key)
                .is_some_and(|value| value.attempts >= limit)
            {
                return false;
            }
        }
        for key in [account, address] {
            buckets
                .entry(key)
                .or_insert(Bucket {
                    started: now,
                    attempts: 0,
                })
                .attempts += 1;
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn password_policy_and_hash_bounds() {
        assert_eq!(normalize_username("Alice").as_deref(), Some("alice"));
        for name in [" a", "a", "a/b", "ab\n"] {
            assert!(normalize_username(name).is_none());
        }
        assert!(normalize_username(&"中".repeat(64)).is_some());
        assert!(normalize_username(&"İ".repeat(64)).is_none());
        assert!(normalize_username("İ").is_none());
        let encoded = hash("synthetic secret password").unwrap();
        assert_eq!(encoded.len(), 97);
        assert!(valid_hash(&encoded));
        for malformed in [
            "plaintext password".to_owned(),
            "x".repeat(4096),
            encoded.replace("m=19456,t=2,p=1", "t=2,m=19456,p=1"),
            encoded.replace("argon2id", "argon2i"),
            encoded.replace("v=19", "v=16"),
            encoded.replace("m=19456", "m=99999"),
            format!("{}\n", encoded.as_str()),
        ] {
            assert!(!valid_hash(&malformed));
            assert!(!verify("synthetic secret password", &malformed));
        }
        let prefix = "$argon2id$v=19$m=19456,t=2,p=1$";
        let base64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for last in base64.chars() {
            let salt = format!("{}{last}", "A".repeat(21));
            let output = "A".repeat(43);
            assert_eq!(
                valid_hash(&format!("{prefix}{salt}${output}")),
                "AQgw".contains(last)
            );
            let salt = "A".repeat(22);
            let output = format!("{}{last}", "A".repeat(42));
            assert_eq!(
                valid_hash(&format!("{prefix}{salt}${output}")),
                "AEIMQUYcgkosw048".contains(last)
            );
        }
        assert!(verify("synthetic secret password", &encoded));
        assert!(!verify("not the password", &encoded));
        assert!(!verify(
            "synthetic secret password",
            &encoded.replace("m=19456", "m=999999999")
        ));
        assert!(hash("short").is_err());
    }
    #[test]
    fn limits_bound_accounts_sources_memory_and_reset() {
        let limits = LoginLimits::default();
        let now = Instant::now();
        let ip = "127.0.0.1".parse().unwrap();
        for _ in 0..10 {
            assert!(limits.allow_at("one", ip, now));
        }
        assert!(!limits.allow_at("one", ip, now));
        for index in 0..20 {
            assert!(limits.allow_at(&format!("user{index}"), ip, now));
        }
        assert!(!limits.allow_at("new-user", ip, now));
        assert!(limits.allow_at("one", ip, now + Duration::from_secs(60)));
        assert!(limits.buckets.lock().unwrap().len() <= 2);
    }
}
