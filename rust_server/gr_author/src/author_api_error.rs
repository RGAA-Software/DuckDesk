use axum::Json;
use axum::response::{IntoResponse, Response};
use thiserror::Error;
use gr_base::RespMessage;

#[derive(Debug, Error)]
pub enum AuthorApiError {
    #[error("Invalid parameters")]
    InvalidParams,

    #[error("Database operation failed")]
    DatabaseError,

    #[error("Password invalid")]
    InvalidPassword,

    #[error("Invalid page/size")]
    InvalidPageSize,

    #[error("Already exists")]
    AlreadyExists,

    #[error("Must be administrator")]
    MustBeAdministrator,
    
    #[error("Authorization not found")]
    AuthorizationNotFound,

    #[error("Appkey secret not paired")]
    AppkeySecretNotPaired,

    #[error("Can't create authorization")]
    CantCreateAuthorization,

    #[error("No authors found")]
    NoAuthorsFound,

    #[error("Update auth failed")]
    UpdateAuthFailed,

    #[error("Invalid login token")]
    InvalidLoginToken,

    #[error("Miss login token")]
    MissLoginToken,
}

impl IntoResponse for AuthorApiError {
    fn into_response(self) -> Response {
        let (code, msg) = match self {
            AuthorApiError::InvalidParams => (800, self.to_string()),
            AuthorApiError::DatabaseError => (801, self.to_string()),
            AuthorApiError::InvalidPassword => (802, self.to_string()),
            AuthorApiError::InvalidPageSize => (803, self.to_string()),
            AuthorApiError::AlreadyExists => (804, self.to_string()),
            AuthorApiError::MustBeAdministrator => (805, self.to_string()),
            AuthorApiError::AuthorizationNotFound => (806, self.to_string()),
            AuthorApiError::AppkeySecretNotPaired => (807, self.to_string()),
            AuthorApiError::CantCreateAuthorization => (808, self.to_string()),
            AuthorApiError::NoAuthorsFound => (809, self.to_string()),
            AuthorApiError::UpdateAuthFailed => (810, self.to_string()),
            AuthorApiError::InvalidLoginToken => (811, self.to_string()),
            AuthorApiError::MissLoginToken => (812, self.to_string()),
        };

        let body = Json(RespMessage::new_data(code, msg, ""));
        (axum::http::StatusCode::from_u16(code as u16).unwrap(), body).into_response()
    }
}
