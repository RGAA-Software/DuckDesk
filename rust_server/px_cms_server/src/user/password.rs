use argon2::{
    password_hash::{
        rand_core::{OsRng, RngCore},
        PasswordHash, PasswordHasher, PasswordVerifier, SaltString,
    },
    Algorithm, Argon2, Params, Version,
};
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use ring::digest::{digest, SHA256};

const MIN_PASSWORD_CHARS: usize = 8;
const MAX_PASSWORD_CHARS: usize = 128;

fn argon2() -> Result<Argon2<'static>, String> {
    let params = Params::new(64 * 1024, 3, 1, None).map_err(|e| e.to_string())?;
    Ok(Argon2::new(Algorithm::Argon2id, Version::V0x13, params))
}

pub fn validate(password: &str) -> Result<(), String> {
    let chars = password.chars().count();
    if !(MIN_PASSWORD_CHARS..=MAX_PASSWORD_CHARS).contains(&chars) {
        return Err(format!(
            "password must contain {MIN_PASSWORD_CHARS} to {MAX_PASSWORD_CHARS} characters"
        ));
    }
    if password.trim().is_empty() {
        return Err("password cannot be whitespace only".to_string());
    }
    Ok(())
}

/// Generate a URL-safe 128-bit initial password.
pub fn generate_random() -> String {
    let mut bytes = [0_u8; 16];
    OsRng.fill_bytes(&mut bytes);
    format!("Px{}", URL_SAFE_NO_PAD.encode(bytes))
}

fn recovery_key(installation_secret: &str) -> Result<[u8; 32], String> {
    if installation_secret.is_empty() {
        return Err("password recovery requires a configured installation secret".to_string());
    }
    let mut material = b"pixels-cms-password-recovery-v1\0".to_vec();
    material.extend_from_slice(installation_secret.as_bytes());
    let value = digest(&SHA256, &material);
    value
        .as_ref()
        .try_into()
        .map_err(|_| "failed to derive password recovery key".to_string())
}

/// Store a recoverable copy for authenticated CMS administrators. Login still
/// uses the Argon2id verifier; this value is AES-GCM encrypted with a key
/// derived from the installation-only privacy salt.
pub fn encrypt_recoverable(password: &str, installation_secret: &str) -> Result<String, String> {
    validate(password)?;
    px_base::crypto_util::aes_encrypt(password, &recovery_key(installation_secret)?)
}

pub fn decrypt_recoverable(
    encrypted_password: &str,
    installation_secret: &str,
) -> Result<String, String> {
    px_base::crypto_util::aes_decrypt(encrypted_password, &recovery_key(installation_secret)?)
}

pub fn hash(password: &str) -> Result<String, String> {
    validate(password)?;
    let salt = SaltString::generate(&mut OsRng);
    argon2()?
        .hash_password(password.as_bytes(), &salt)
        .map(|value| value.to_string())
        .map_err(|e| e.to_string())
}

pub fn verify(password: &str, password_hash: &str) -> bool {
    let Ok(parsed) = PasswordHash::new(password_hash) else {
        return false;
    };
    argon2()
        .map(|hasher| hasher.verify_password(password.as_bytes(), &parsed).is_ok())
        .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn password_hash_is_argon2id_and_salted() {
        let first = hash("correct horse battery staple").unwrap();
        let second = hash("correct horse battery staple").unwrap();
        assert!(first.starts_with("$argon2id$"));
        assert_ne!(first, second);
        assert!(verify("correct horse battery staple", &first));
        assert!(!verify("wrong password", &first));
    }

    #[test]
    fn password_policy_rejects_short_and_whitespace() {
        assert!(validate("short").is_err());
        assert!(validate("        ").is_err());
        assert!(validate("valid-passphrase").is_ok());
    }

    #[test]
    fn legacy_md5_is_never_accepted_as_a_verifier() {
        assert!(!verify("password", "5f4dcc3b5aa765d61d8327deb882cf99"));
    }

    #[test]
    fn generated_password_has_at_least_128_bits_of_random_input() {
        let first = generate_random();
        let second = generate_random();
        assert_eq!(first.len(), 24);
        assert!(first.starts_with("Px"));
        assert_ne!(first, second);
        assert!(validate(&first).is_ok());
    }

    #[test]
    fn administrator_password_copy_is_encrypted_and_recoverable() {
        let password = "viewable-admin-password";
        let encrypted = encrypt_recoverable(password, "installation-specific-secret").unwrap();
        assert_ne!(encrypted, password);
        assert_eq!(
            decrypt_recoverable(&encrypted, "installation-specific-secret").unwrap(),
            password
        );
        assert!(decrypt_recoverable(&encrypted, "different-installation-secret").is_err());
    }
}
