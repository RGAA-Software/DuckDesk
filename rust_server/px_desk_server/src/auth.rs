use crate::{error::ApiError, AppState};
use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    Json,
};
use chrono::{DateTime, Utc};
use rand::{rngs::OsRng, TryRngCore};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::sync::Arc;
use subtle::ConstantTimeEq;
use uuid::Uuid;
use zeroize::Zeroizing;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Login {
    token: String,
}
impl Drop for Login {
    fn drop(&mut self) {
        use zeroize::Zeroize;
        self.token.zeroize();
    }
}
#[derive(Serialize)]
pub struct Session {
    access_token: String,
    expires_at: DateTime<Utc>,
}

pub async fn login(
    State(state): State<Arc<AppState>>,
    Json(input): Json<Login>,
) -> Result<Json<Session>, ApiError> {
    let _permit = state
        .login_slots
        .try_acquire()
        .map_err(|_| ApiError::Unavailable)?;
    if input.token.len() != 64
        || !input
            .token
            .bytes()
            .all(|byte_value| byte_value.is_ascii_digit() || (b'a'..=b'f').contains(&byte_value))
    {
        return Err(ApiError::Unauthorized);
    }
    let fingerprint: [u8; 32] = Sha256::digest(input.token.as_bytes()).into();
    if !bool::from(fingerprint.ct_eq(&state.admin_digest)) {
        return Err(ApiError::Unauthorized);
    }
    let mut random = Zeroizing::new([0u8; 32]);
    OsRng
        .try_fill_bytes(random.as_mut())
        .map_err(|_| ApiError::Internal)?;
    let token = hex::encode(random.as_slice());
    let digest = Sha256::digest(token.as_bytes());
    let expires_at = sqlx::query_file_scalar!(
        "queries/create_admin_session.sql",
        Uuid::new_v4(),
        &digest[..],
        &state.admin_digest[..]
    )
    .fetch_one(&state.pool)
    .await?;
    Ok(Json(Session {
        access_token: token,
        expires_at,
    }))
}

pub async fn authorize(state: &AppState, headers: &HeaderMap) -> Result<Uuid, ApiError> {
    let bearer = headers
        .get("authorization")
        .and_then(|authorization_header| authorization_header.to_str().ok())
        .and_then(|header_value| header_value.strip_prefix("Bearer "))
        .ok_or(ApiError::Unauthorized)?;
    if bearer.len() != 64
        || !bearer
            .bytes()
            .all(|byte_value| byte_value.is_ascii_digit() || (b'a'..=b'f').contains(&byte_value))
    {
        return Err(ApiError::Unauthorized);
    }
    let digest = Sha256::digest(bearer.as_bytes());
    sqlx::query_file_scalar!(
        "queries/authenticate_admin_session.sql",
        &digest[..],
        &state.admin_digest[..]
    )
    .fetch_optional(&state.pool)
    .await?
    .ok_or(ApiError::Unauthorized)
}

pub async fn logout(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
) -> Result<StatusCode, ApiError> {
    let id = authorize(&state, &headers).await?;
    sqlx::query_file!("queries/revoke_admin_session.sql", id)
        .execute(&state.pool)
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
