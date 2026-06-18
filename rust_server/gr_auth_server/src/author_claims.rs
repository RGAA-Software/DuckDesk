use serde::{Serialize, Deserialize};
use jsonwebtoken::{encode, decode, Header, Validation, EncodingKey, DecodingKey, TokenData, errors::Result as JwtResult};
use jsonwebtoken::errors::{Error as JwtError, ErrorKind};
use std::sync::RwLock;
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use crate::author::AuthorRole;

use std::sync::atomic::{AtomicU64, Ordering};

static TOKEN_VERSION: AtomicU64 = AtomicU64::new(1);
static JWT_SECRET: RwLock<Option<Vec<u8>>> = RwLock::new(None);

const MIN_JWT_SECRET_LEN: usize = 32;

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct AuthorClaims {
    pub sub: String,      // 用户ID或用户名
    pub role: AuthorRole, // 角色
    pub exp: usize,       // 过期时间
    pub ver: u64,
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
        let ver = TOKEN_VERSION.load(Ordering::Relaxed);
        Self {
            sub: user_id.to_owned(),
            role,
            exp: expiration,
            ver,
        }
    }
    
    pub fn verify(token: &str) -> JwtResult<TokenData<Self>> {
        let Some(secret) = current_jwt_secret() else {
            return Err(JwtError::from(ErrorKind::InvalidToken));
        };

        let data = decode::<Self>(
            token,
            &DecodingKey::from_secret(&secret),
            &Validation::default()
        )?;

        let current = TOKEN_VERSION.load(Ordering::Relaxed);
        if data.claims.ver != current {
            return Err(JwtError::from(ErrorKind::InvalidToken));
        }
        Ok(data)
    }

    pub async fn logout() {
        TOKEN_VERSION.fetch_add(1, Ordering::Relaxed);
    }

}

pub fn init_jwt_secret(secret: String) -> bool {
    let secret = secret.trim();
    if secret.len() < MIN_JWT_SECRET_LEN {
        tracing::error!("JWT secret is too short; require at least {} characters", MIN_JWT_SECRET_LEN);
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
    JWT_SECRET
        .read()
        .expect("jwt secret lock poisoned")
        .clone()
}

#[cfg(test)]
mod tests {
    use crate::author_claims::{init_jwt_secret, AuthorClaims};
    use crate::author::AuthorRole;

    fn init_test_secret() {
        assert!(init_jwt_secret("test-secret-must-be-at-least-32-bytes".to_string()));
    }

    #[test]
    fn generated_token_preserves_subject_and_role() {
        init_test_secret();
        let claims = AuthorClaims::new(
            "Admin".to_string(),
            AuthorRole::Admin,
            3600,
        );

        let token = claims.generate_token().expect("token should encode");
        let token_data = AuthorClaims::verify(&token).expect("token should verify");

        assert_eq!(token_data.claims.sub, "Admin");
        assert_eq!(token_data.claims.role, AuthorRole::Admin);
    }

    #[test]
    fn tampered_token_is_rejected() {
        init_test_secret();
        let claims = AuthorClaims::new(
            "Admin".to_string(),
            AuthorRole::Admin,
            3600,
        );

        let mut token = claims.generate_token().expect("token should encode");
        token.push('x');

        assert!(AuthorClaims::verify(&token).is_err());
    }

    #[test]
    fn rejects_short_jwt_secret() {
        assert!(!init_jwt_secret("too-short".to_string()));
    }

    #[test]
    fn rejects_placeholder_jwt_secret() {
        assert!(!init_jwt_secret("CHANGE_ME_TO_A_RANDOM_SECRET_AT_LEAST_32_CHARS".to_string()));
        assert!(!init_jwt_secret("<random secret with at least 32 characters>".to_string()));
    }
}
