use axum::response::{IntoResponse, Response};
use axum::Json;
use px_base::RespMessage;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum StatApiError {
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

impl IntoResponse for StatApiError {
    fn into_response(self) -> Response {
        let (code, msg) = match self {
            StatApiError::InvalidParams => (600, self.to_string()),
            StatApiError::NeedDescParam => (601, self.to_string()),
            StatApiError::DatabaseError => (602, self.to_string()),
            StatApiError::ItemNotFound => (603, self.to_string()),
            StatApiError::InvalidVersionVerifyCode => (604, self.to_string()),
            StatApiError::VersionNotFound => (605, self.to_string()),
        };

        let body = Json(RespMessage::new_data(code, msg, ""));
        (axum::http::StatusCode::from_u16(code as u16).unwrap(), body).into_response()
    }
}
