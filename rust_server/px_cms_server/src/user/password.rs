use argon2::{
    password_hash::{rand_core::OsRng, PasswordHash, PasswordHasher, PasswordVerifier, SaltString},
    Algorithm, Argon2, Params, Version,
};

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
}
