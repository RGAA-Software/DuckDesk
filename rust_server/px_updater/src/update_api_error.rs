use axum::Json;
use axum::response::{IntoResponse, Response};
use px_base::RespMessage;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum UpdateApiError {
    #[error("Invalid parameters")]
    InvalidParams,

    #[error("Need description")]
    NeedDescParam,

    #[error("Database error")]
    DatabaseError,

    #[error("Need version")]
    NeedVersionParam,

    #[error("Update file failed")]
    UploadFileFailed,

    #[error("Version not found")]
    VersionNotFound,

    #[error("file not found")]
    FileNotFound,
}

impl IntoResponse for UpdateApiError {
    fn into_response(self) -> Response {
        let (code, msg) = match self {
            UpdateApiError::InvalidParams => (600, self.to_string()),
            UpdateApiError::NeedDescParam => (601, self.to_string()),
            UpdateApiError::DatabaseError => (602, self.to_string()),
            UpdateApiError::NeedVersionParam => (603, self.to_string()),
            UpdateApiError::UploadFileFailed => (604, self.to_string()),
            UpdateApiError::VersionNotFound => (605, self.to_string()),
            UpdateApiError::FileNotFound => (606, self.to_string()),
        };

        let body = Json(RespMessage::new_data(code, msg, ""));
        (axum::http::StatusCode::from_u16(code as u16).unwrap(), body).into_response()
    }
}
