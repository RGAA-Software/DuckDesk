use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use px_console_store::StoreError;
use px_pg::DatabaseError;

#[derive(Debug, Clone, Copy, thiserror::Error)]
pub enum ApiError {
    #[error("invalid input")]
    Invalid,
    #[error("authentication required")]
    Unauthorized,
    #[error("access rejected")]
    Rejected,
    #[error("not found")]
    NotFound,
    #[error("conflicting state")]
    Conflict,
    #[error("rate limited")]
    RateLimited,
    #[error("service unavailable")]
    Unavailable,
    #[error("internal operation failed")]
    Internal,
}
impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (status, code) = match self {
            Self::Invalid => (StatusCode::BAD_REQUEST, "invalid_input"),
            Self::Unauthorized => (StatusCode::UNAUTHORIZED, "unauthorized"),
            Self::Rejected => (StatusCode::FORBIDDEN, "rejected"),
            Self::NotFound => (StatusCode::NOT_FOUND, "not_found"),
            Self::Conflict => (StatusCode::CONFLICT, "conflict"),
            Self::RateLimited => (StatusCode::TOO_MANY_REQUESTS, "rate_limited"),
            Self::Unavailable => (StatusCode::SERVICE_UNAVAILABLE, "unavailable"),
            Self::Internal => (StatusCode::INTERNAL_SERVER_ERROR, "internal"),
        };
        (status, Json(serde_json::json!({"code":code}))).into_response()
    }
}
impl From<DatabaseError> for ApiError {
    fn from(value: DatabaseError) -> Self {
        match value {
            DatabaseError::Conflict => Self::Conflict,
            _ => Self::Unavailable,
        }
    }
}
impl From<StoreError> for ApiError {
    fn from(value: StoreError) -> Self {
        match value {
            StoreError::InvalidInput => Self::Invalid,
            StoreError::Rejected => Self::Rejected,
            StoreError::NotFound => Self::NotFound,
            StoreError::Database(e) => e.into(),
            _ => Self::Unavailable,
        }
    }
}
