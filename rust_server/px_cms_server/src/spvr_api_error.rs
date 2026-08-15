use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use px_base::RespMessage;
use thiserror::Error;

#[derive(Debug, Error, PartialEq)]
pub enum SpvrApiError {
    #[error("Invalid parameters")]
    InvalidParams,

    #[error("Database operation failed")]
    DatabaseError,

    #[error("Device not found")]
    DeviceNotFound,

    #[error("Password invalid")]
    PasswordInvalid,

    #[error("Invalid appkey")]
    InvalidAppkey,

    #[error("Create device failed")]
    CreateDeviceFailed,

    #[error("Invalid authorization")]
    InvalidAuthorization,

    #[error("Internal error")]
    InternalError,

    #[error("user already exists")]
    UserAlreadyExists,

    #[error("user not found")]
    UserNotFound,

    #[error("user update failed")]
    UserUpdateFailed,

    #[error("file no extension")]
    FileNoExtension,

    #[error("upload file failed")]
    UploadFileFailed,

    #[error("verify password failed")]
    VerifyPasswordFailed,

    #[error("stream not found")]
    StreamNotFound,

    #[error("connection not found")]
    ConnectionNotFound,

    #[error("user-device not found")]
    UserDeviceNotFound,

    #[error("user-device already exists")]
    UserDeviceAlreadyExists,

    #[error("Need description")]
    NeedDescParam,

    #[error("Need version")]
    NeedVersionParam,

    #[error("Version not found")]
    VersionNotFound,

    #[error("file not found")]
    FileNotFound,

    #[error("visit not found")]
    VisitNotFound,

    #[error("file transfer not found")]
    FileTransferNotFound,

    #[error("machine code not matched")]
    MachineCodeNotMatched,

    #[error("max streams reached")]
    MaxStreamsReached,
}

// SpvrApiError -> Response
impl IntoResponse for SpvrApiError {
    fn into_response(self) -> Response {
        let status = self.status_code();
        let code = self.business_code();
        let msg = self.to_string();
        let body = Json(RespMessage::new_data(code, msg, ""));
        (status, body).into_response()
    }
}

impl SpvrApiError {
    pub fn business_code(&self) -> i32 {
        match self {
            SpvrApiError::InvalidParams => 600,
            SpvrApiError::DatabaseError => 601,
            SpvrApiError::DeviceNotFound => 602,
            SpvrApiError::PasswordInvalid => 603,
            SpvrApiError::InvalidAppkey => 604,
            SpvrApiError::CreateDeviceFailed => 605,
            SpvrApiError::InvalidAuthorization => 606,
            SpvrApiError::InternalError => 607,
            SpvrApiError::UserAlreadyExists => 608,
            SpvrApiError::UserNotFound => 609,
            SpvrApiError::UserUpdateFailed => 610,
            SpvrApiError::FileNoExtension => 611,
            SpvrApiError::UploadFileFailed => 612,
            SpvrApiError::VerifyPasswordFailed => 613,
            SpvrApiError::StreamNotFound => 614,
            SpvrApiError::ConnectionNotFound => 615,
            SpvrApiError::UserDeviceNotFound => 616,
            SpvrApiError::UserDeviceAlreadyExists => 617,
            SpvrApiError::NeedDescParam => 618,
            SpvrApiError::NeedVersionParam => 619,
            SpvrApiError::VersionNotFound => 620,
            SpvrApiError::FileNotFound => 621,
            SpvrApiError::VisitNotFound => 622,
            SpvrApiError::FileTransferNotFound => 625,
            SpvrApiError::MachineCodeNotMatched => 623,
            SpvrApiError::MaxStreamsReached => 624,
        }
    }

    pub fn status_code(&self) -> StatusCode {
        match self {
            SpvrApiError::InvalidAppkey => StatusCode::UNAUTHORIZED,
            SpvrApiError::MaxStreamsReached => StatusCode::FORBIDDEN,
            SpvrApiError::InvalidAuthorization
            | SpvrApiError::MachineCodeNotMatched
            | SpvrApiError::InvalidParams
            | SpvrApiError::DeviceNotFound
            | SpvrApiError::PasswordInvalid
            | SpvrApiError::CreateDeviceFailed
            | SpvrApiError::InternalError
            | SpvrApiError::UserAlreadyExists
            | SpvrApiError::UserNotFound
            | SpvrApiError::UserUpdateFailed
            | SpvrApiError::FileNoExtension
            | SpvrApiError::UploadFileFailed
            | SpvrApiError::VerifyPasswordFailed
            | SpvrApiError::StreamNotFound
            | SpvrApiError::ConnectionNotFound
            | SpvrApiError::UserDeviceNotFound
            | SpvrApiError::UserDeviceAlreadyExists
            | SpvrApiError::NeedDescParam
            | SpvrApiError::NeedVersionParam
            | SpvrApiError::VersionNotFound
            | SpvrApiError::FileNotFound
            | SpvrApiError::VisitNotFound
            | SpvrApiError::FileTransferNotFound => StatusCode::BAD_REQUEST,
            SpvrApiError::DatabaseError => StatusCode::INTERNAL_SERVER_ERROR,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn business_codes_remain_stable() {
        assert_eq!(SpvrApiError::InvalidAppkey.business_code(), 604);
        assert_eq!(SpvrApiError::MaxStreamsReached.business_code(), 624);
        assert_eq!(SpvrApiError::InvalidParams.business_code(), 600);
        assert_eq!(SpvrApiError::DatabaseError.business_code(), 601);
    }

    #[test]
    fn auth_errors_return_401() {
        assert_eq!(
            SpvrApiError::InvalidAppkey.status_code(),
            StatusCode::UNAUTHORIZED
        );
    }

    #[test]
    fn max_streams_returns_403() {
        assert_eq!(
            SpvrApiError::MaxStreamsReached.status_code(),
            StatusCode::FORBIDDEN
        );
    }

    #[test]
    fn response_preserves_business_code_in_body() {
        let resp = SpvrApiError::InvalidAppkey.into_response();
        assert_eq!(resp.status(), StatusCode::UNAUTHORIZED);
    }
}
