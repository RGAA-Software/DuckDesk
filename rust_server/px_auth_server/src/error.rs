use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use px_pg::DatabaseError;

#[derive(Debug, thiserror::Error)]
pub enum ApiError {
    #[error("invalid_request")]
    Invalid,
    #[error("unauthorized")]
    Unauthorized,
    #[error("not_found")]
    NotFound,
    #[error("conflict")]
    Conflict,
    #[error("unavailable")]
    Unavailable,
    #[error("rate_limited")]
    RateLimited,
    #[error("internal_error")]
    Internal,
}

impl From<DatabaseError> for ApiError {
    fn from(error: DatabaseError) -> Self {
        match error {
            DatabaseError::Conflict => Self::Conflict,
            _ => Self::Unavailable,
        }
    }
}
impl From<sqlx::Error> for ApiError {
    fn from(error: sqlx::Error) -> Self {
        DatabaseError::from(error).into()
    }
}
impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let status = match self {
            Self::RateLimited => StatusCode::TOO_MANY_REQUESTS,
            Self::Invalid => StatusCode::BAD_REQUEST,
            Self::Unauthorized => StatusCode::UNAUTHORIZED,
            Self::NotFound => StatusCode::NOT_FOUND,
            Self::Conflict => StatusCode::CONFLICT,
            Self::Unavailable => StatusCode::SERVICE_UNAVAILABLE,
            Self::Internal => StatusCode::INTERNAL_SERVER_ERROR,
        };
        (status, Json(serde_json::json!({"code": self.to_string()}))).into_response()
    }
}

impl From<px_auth_store::AuthError> for ApiError {
    fn from(error: px_auth_store::AuthError) -> Self {
        match error {
            px_auth_store::AuthError::Invalid => Self::Invalid,
            px_auth_store::AuthError::Rejected => Self::Unauthorized,
            px_auth_store::AuthError::Conflict => Self::Conflict,
            px_auth_store::AuthError::Database(error) => error.into(),
        }
    }
}
