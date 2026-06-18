use axum::Json;
use axum::http::StatusCode;
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
        let status = self.status_code();
        let code = self.business_code();
        let msg = self.to_string();
        let body = Json(RespMessage::new_data(code, msg, ""));
        (status, body).into_response()
    }
}

impl AuthorApiError {
    pub fn business_code(&self) -> i32 {
        match self {
            AuthorApiError::InvalidParams => 800,
            AuthorApiError::DatabaseError => 801,
            AuthorApiError::InvalidPassword => 802,
            AuthorApiError::InvalidPageSize => 803,
            AuthorApiError::AlreadyExists => 804,
            AuthorApiError::MustBeAdministrator => 805,
            AuthorApiError::AuthorizationNotFound => 806,
            AuthorApiError::AppkeySecretNotPaired => 807,
            AuthorApiError::CantCreateAuthorization => 808,
            AuthorApiError::NoAuthorsFound => 809,
            AuthorApiError::UpdateAuthFailed => 810,
            AuthorApiError::InvalidLoginToken => 811,
            AuthorApiError::MissLoginToken => 812,
        }
    }

    pub fn status_code(&self) -> StatusCode {
        match self {
            AuthorApiError::InvalidParams
            | AuthorApiError::InvalidPageSize
            | AuthorApiError::AlreadyExists
            | AuthorApiError::AuthorizationNotFound
            | AuthorApiError::AppkeySecretNotPaired
            | AuthorApiError::CantCreateAuthorization
            | AuthorApiError::NoAuthorsFound
            | AuthorApiError::UpdateAuthFailed => StatusCode::BAD_REQUEST,
            AuthorApiError::InvalidPassword
            | AuthorApiError::InvalidLoginToken
            | AuthorApiError::MissLoginToken => StatusCode::UNAUTHORIZED,
            AuthorApiError::MustBeAdministrator => StatusCode::FORBIDDEN,
            AuthorApiError::DatabaseError => StatusCode::INTERNAL_SERVER_ERROR,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn business_codes_remain_stable() {
        assert_eq!(AuthorApiError::InvalidParams.business_code(), 800);
        assert_eq!(AuthorApiError::InvalidPassword.business_code(), 802);
        assert_eq!(AuthorApiError::MustBeAdministrator.business_code(), 805);
        assert_eq!(AuthorApiError::InvalidLoginToken.business_code(), 811);
        assert_eq!(AuthorApiError::MissLoginToken.business_code(), 812);
    }

    #[test]
    fn errors_map_to_valid_http_status_codes() {
        let cases = [
            (AuthorApiError::InvalidParams, StatusCode::BAD_REQUEST),
            (AuthorApiError::DatabaseError, StatusCode::INTERNAL_SERVER_ERROR),
            (AuthorApiError::InvalidPassword, StatusCode::UNAUTHORIZED),
            (AuthorApiError::MustBeAdministrator, StatusCode::FORBIDDEN),
            (AuthorApiError::InvalidLoginToken, StatusCode::UNAUTHORIZED),
            (AuthorApiError::MissLoginToken, StatusCode::UNAUTHORIZED),
        ];

        for (error, status) in cases {
            assert_eq!(error.status_code(), status);
        };
    }
}
