use crate::author::AuthorRole;
use jsonwebtoken::errors::{Error as JwtError, ErrorKind};
use jsonwebtoken::{
    DecodingKey, EncodingKey, Header, TokenData, Validation, decode, encode,
    errors::Result as JwtResult,
};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::{LazyLock, RwLock};
use std::time::{Duration, SystemTime, UNIX_EPOCH};
use uuid::Uuid;

static JWT_SECRET: RwLock<Option<Vec<u8>>> = RwLock::new(None);
static TOKEN_BLACKLIST: LazyLock<RwLock<HashMap<String, usize>>> =
    LazyLock::new(|| RwLock::new(HashMap::new()));

const MIN_JWT_SECRET_LEN: usize = 32;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct AuthorClaims {
    pub sub: String,      // 用户ID或用户名
    pub role: AuthorRole, // 角色
    pub jti: String,      // JWT ID，用于单 token logout
    pub exp: usize,       // 过期时间
}

impl AuthorClaims {
    /// 生成该用户的 JWT
    pub fn generate_token(&self) -> JwtResult<String> {
        let Some(secret) = current_jwt_secret() else {
            return Err(JwtError::from(ErrorKind::InvalidToken));
        };
        encode(&Header::default(), self, &EncodingKey::from_secret(&secret))
    }

    /// 创建一个带过期时间的 AuthorClaims
    pub fn new(user_id: String, role: AuthorRole, expire_seconds: u64) -> Self {
        let expiration = SystemTime::now()
            .checked_add(Duration::from_secs(expire_seconds))
            .unwrap()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as usize;
        Self {
            sub: user_id.to_owned(),
            role,
            jti: Uuid::new_v4().to_string(),
            exp: expiration,
        }
    }

    pub fn verify(token: &str) -> JwtResult<TokenData<Self>> {
        let Some(secret) = current_jwt_secret() else {
            return Err(JwtError::from(ErrorKind::InvalidToken));
        };

        let data = decode::<Self>(
            token,
            &DecodingKey::from_secret(&secret),
            &Validation::default(),
        )?;

        cleanup_blacklist();
        if is_blacklisted(&data.claims.jti) {
            return Err(JwtError::from(ErrorKind::InvalidToken));
        }
        Ok(data)
    }

    pub fn logout(&self) {
        blacklist_token(self.jti.clone(), self.exp);
    }
}

pub fn init_jwt_secret(secret: String) -> bool {
    let secret = secret.trim();
    if secret.len() < MIN_JWT_SECRET_LEN {
        tracing::error!(
            "JWT secret is too short; require at least {} characters",
            MIN_JWT_SECRET_LEN
        );
        return false;
    }
    if secret.starts_with('<') || secret.starts_with("CHANGE_ME") {
        tracing::error!("JWT secret is still a placeholder");
        return false;
    }

    let mut guard = JWT_SECRET.write().expect("jwt secret lock poisoned");
    *guard = Some(secret.as_bytes().to_vec());
    true
}

fn current_jwt_secret() -> Option<Vec<u8>> {
    JWT_SECRET.read().expect("jwt secret lock poisoned").clone()
}

fn blacklist_token(jti: String, exp: usize) {
    let mut guard = TOKEN_BLACKLIST
        .write()
        .expect("token blacklist lock poisoned");
    guard.insert(jti, exp);
}

fn is_blacklisted(jti: &str) -> bool {
    TOKEN_BLACKLIST
        .read()
        .expect("token blacklist lock poisoned")
        .contains_key(jti)
}

pub fn cleanup_blacklist() {
    let now = current_timestamp();
    let mut guard = TOKEN_BLACKLIST
        .write()
        .expect("token blacklist lock poisoned");
    guard.retain(|_, exp| *exp > now);
}

fn current_timestamp() -> usize {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs() as usize
}

#[cfg(test)]
pub fn clear_blacklist_for_test() {
    TOKEN_BLACKLIST
        .write()
        .expect("token blacklist lock poisoned")
        .clear();
}

#[cfg(test)]
fn blacklist_len_for_test() -> usize {
    TOKEN_BLACKLIST
        .read()
        .expect("token blacklist lock poisoned")
        .len()
}

/// Global lock used by all tests that mutate or inspect the token blacklist.
/// This prevents parallel tests from clearing each other's blacklist entries.
#[cfg(test)]
pub(crate) static BLACKLIST_TEST_LOCK: LazyLock<std::sync::Mutex<()>> =
    LazyLock::new(|| std::sync::Mutex::new(()));

#[cfg(test)]
pub(crate) fn blacklist_test_guard() -> std::sync::MutexGuard<'static, ()> {
    BLACKLIST_TEST_LOCK
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

#[cfg(test)]
mod tests {
    use crate::author::AuthorRole;
    use crate::author_claims::{
        AuthorClaims, blacklist_len_for_test, blacklist_test_guard, blacklist_token,
        cleanup_blacklist, clear_blacklist_for_test, current_timestamp, init_jwt_secret,
        is_blacklisted,
    };

    fn init_test_secret() {
        assert!(init_jwt_secret(
            "test-secret-must-be-at-least-32-bytes".to_string()
        ));
    }

    #[test]
    fn generated_token_preserves_subject_and_role() {
        let _guard = blacklist_test_guard();
        init_test_secret();
        clear_blacklist_for_test();
        let claims = AuthorClaims::new("Admin".to_string(), AuthorRole::Admin, 3600);

        let token = claims.generate_token().expect("token should encode");
        let token_data = AuthorClaims::verify(&token).expect("token should verify");

        assert_eq!(token_data.claims.sub, "Admin");
        assert_eq!(token_data.claims.role, AuthorRole::Admin);
        assert!(!token_data.claims.jti.is_empty());
    }

    #[test]
    fn tampered_token_is_rejected() {
        let _guard = blacklist_test_guard();
        init_test_secret();
        clear_blacklist_for_test();
        let claims = AuthorClaims::new("Admin".to_string(), AuthorRole::Admin, 3600);

        let mut token = claims.generate_token().expect("token should encode");
        token.push('x');

        assert!(AuthorClaims::verify(&token).is_err());
    }

    #[test]
    fn expired_token_is_rejected() {
        let _guard = blacklist_test_guard();
        init_test_secret();
        clear_blacklist_for_test();
        let mut claims = AuthorClaims::new("Admin".to_string(), AuthorRole::Admin, 3600);
        claims.exp = current_timestamp().saturating_sub(3600);

        let token = claims
            .generate_token()
            .expect("expired token should encode");

        assert!(AuthorClaims::verify(&token).is_err());
    }

    #[test]
    fn generated_tokens_have_unique_jti() {
        let first = AuthorClaims::new("Admin".to_string(), AuthorRole::Admin, 3600);
        let second = AuthorClaims::new("Admin".to_string(), AuthorRole::Admin, 3600);

        assert_ne!(first.jti, second.jti);
    }

    #[test]
    fn logout_invalidates_only_current_token() {
        let _guard = blacklist_test_guard();
        init_test_secret();
        clear_blacklist_for_test();
        let first = AuthorClaims::new("Admin".to_string(), AuthorRole::Admin, 3600);
        let second = AuthorClaims::new("Admin".to_string(), AuthorRole::Admin, 3600);
        let first_token = first.generate_token().expect("first token should encode");
        let second_token = second.generate_token().expect("second token should encode");

        first.logout();

        assert!(AuthorClaims::verify(&first_token).is_err());
        assert!(AuthorClaims::verify(&second_token).is_ok());
    }

    #[test]
    fn cleanup_blacklist_removes_expired_entries() {
        let _guard = blacklist_test_guard();
        clear_blacklist_for_test();
        blacklist_token("expired".to_string(), current_timestamp().saturating_sub(1));
        blacklist_token("active".to_string(), current_timestamp() + 3600);

        cleanup_blacklist();

        assert_eq!(blacklist_len_for_test(), 1);
        assert!(is_blacklisted("active"));
    }

    #[test]
    fn rejects_short_jwt_secret() {
        assert!(!init_jwt_secret("too-short".to_string()));
    }

    #[test]
    fn rejects_placeholder_jwt_secret() {
        assert!(!init_jwt_secret(
            "CHANGE_ME_TO_A_RANDOM_SECRET_AT_LEAST_32_CHARS".to_string()
        ));
        assert!(!init_jwt_secret(
            "<random secret with at least 32 characters>".to_string()
        ));
    }
}
