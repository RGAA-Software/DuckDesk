use serde::{Serialize, Deserialize};
use jsonwebtoken::{encode, decode, Header, Validation, EncodingKey, DecodingKey, TokenData, errors::Result as JwtResult};
use jsonwebtoken::errors::{Error as JwtError, ErrorKind};
use std::time::{SystemTime, UNIX_EPOCH, Duration};
use std::collections::HashSet;
use lazy_static::lazy_static;
use tokio::sync::RwLock;
use uuid::Uuid;
use crate::author_api_error::AuthorApiError;
const SECRET_KEY: &[u8] = b"author_secret_key"; // 实际项目中用更安全的方式存储

use std::sync::atomic::{AtomicU64, Ordering};

static TOKEN_VERSION: AtomicU64 = AtomicU64::new(1);

#[derive(Debug, Serialize, Deserialize)]
pub struct AuthorClaims {
    sub: String,     // 用户ID或用户名
    exp: usize,      // 过期时间
    ver: u64,
}

impl AuthorClaims {
    /// 生成该用户的 JWT
    pub fn generate_token(&self) -> String {
        encode(&Header::default(), self, &EncodingKey::from_secret(SECRET_KEY))
            .expect("JWT encode failed")
    }

    /// 创建一个带过期时间的 AuthorClaims
    pub fn new(user_id: String, expire_seconds: u64) -> Self {
        let expiration = SystemTime::now()
            .checked_add(Duration::from_secs(expire_seconds))
            .unwrap()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs() as usize;
        let ver = TOKEN_VERSION.load(Ordering::Relaxed);
        Self {
            sub: user_id.to_owned(),
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

