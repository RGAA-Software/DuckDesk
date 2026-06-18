use serde::{Serialize, Deserialize};
use jsonwebtoken::{encode, decode, Header, Validation, EncodingKey, DecodingKey, TokenData, errors::Result as JwtResult};
use jsonwebtoken::errors::{Error as JwtError, ErrorKind};
use std::time::{SystemTime, UNIX_EPOCH, Duration};
const SECRET_KEY: &[u8] = b"author_secret_key"; // 实际项目中用更安全的方式存储

use std::sync::atomic::{AtomicU64, Ordering};

static TOKEN_VERSION: AtomicU64 = AtomicU64::new(1);

#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct AuthorClaims {
    pub sub: String,         // 用户ID或用户名
    pub permission: String,  // 旧权限字段，后续迁移为 role
    pub exp: usize,          // 过期时间
    pub ver: u64,
}

impl AuthorClaims {
    /// 生成该用户的 JWT
    pub fn generate_token(&self) -> JwtResult<String> {
        encode(&Header::default(), self, &EncodingKey::from_secret(SECRET_KEY))
    }

    /// 创建一个带过期时间的 AuthorClaims
    pub fn new(user_id: String, permission: String, expire_seconds: u64) -> Self {
        let expiration = SystemTime::now()
            .checked_add(Duration::from_secs(expire_seconds))
            .unwrap()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as usize;
        let ver = TOKEN_VERSION.load(Ordering::Relaxed);
        Self {
            sub: user_id.to_owned(),
            permission,
            exp: expiration,
            ver,
        }
    }
    
    pub fn verify(token: &str) -> JwtResult<TokenData<Self>> {
        let data = decode::<Self>(
            token,
            &DecodingKey::from_secret(SECRET_KEY),
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

#[cfg(test)]
mod tests {
    use crate::author_claims::AuthorClaims;
    use crate::author_manager::AUTHOR_PERM_ALL;

    #[test]
    fn generated_token_preserves_subject_and_permission() {
        let claims = AuthorClaims::new(
            "Admin".to_string(),
            AUTHOR_PERM_ALL.to_string(),
            3600,
        );

        let token = claims.generate_token().expect("token should encode");
        let token_data = AuthorClaims::verify(&token).expect("token should verify");

        assert_eq!(token_data.claims.sub, "Admin");
        assert_eq!(token_data.claims.permission, AUTHOR_PERM_ALL);
    }

    #[test]
    fn tampered_token_is_rejected() {
        let claims = AuthorClaims::new(
            "Admin".to_string(),
            AUTHOR_PERM_ALL.to_string(),
            3600,
        );

        let mut token = claims.generate_token().expect("token should encode");
        token.push('x');

        assert!(AuthorClaims::verify(&token).is_err());
    }
}
