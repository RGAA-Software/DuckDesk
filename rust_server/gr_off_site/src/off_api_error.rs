use axum::Json;
use axum::response::{IntoResponse, Response};
use thiserror::Error;
use base::RespMessage;

#[derive(Debug, Error)]
pub enum OffApiError {
    #[error("Invalid parameters")]
    InvalidParams,

    #[error("Need description")]
    NeedDescParam,

    #[error("Database error")]
    DatabaseError,
    
    #[error("Item not found")]
    ItemNotFound,

    #[error("Invalid version verify code")]
    InvalidVersionVerifyCode,

    #[error("Version info not found")]
    VersionNotFound,
}

impl IntoResponse for OffApiError {
    fn into_response(self) -> Response {
        let (code, msg) = match self {
            OffApiError::InvalidParams => (600, self.to_string()),
            OffApiError::NeedDescParam => (601, self.to_string()),
            OffApiError::DatabaseError => (602, self.to_string()),
            OffApiError::ItemNotFound => (603, self.to_string()),
            OffApiError::InvalidVersionVerifyCode => (604, self.to_string()),
            OffApiError::VersionNotFound => (605, self.to_string()),
        };

        let body = Json(RespMessage::new_data(code, msg, ""));
        (axum::http::StatusCode::from_u16(code as u16).unwrap(), body).into_response()
    }
}