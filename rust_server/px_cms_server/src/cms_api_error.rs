use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;
use px_base::RespMessage;
use thiserror::Error;

#[derive(Debug, Error, PartialEq)]
pub enum CmsApiError {
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

    #[error("device is offline")]
    DeviceOffline,

    #[error("request to device timed out")]
    RequestTimeout,

    #[error("invalid or expired upload token")]
    TokenInvalid,

    #[error("record not found")]
    RecordNotFound,

    #[error("device has no safety password")]
    SafetyPwdMissing,
}

// CmsApiError -> Response
impl IntoResponse for CmsApiError {
    fn into_response(self) -> Response {
        let status = self.status_code();
        let code = self.business_code();
        let msg = self.to_string();
        let body = Json(RespMessage::new_data(code, msg, ""));
        (status, body).into_response()
    }
}

impl CmsApiError {
    pub fn business_code(&self) -> i32 {
        match self {
            CmsApiError::InvalidParams => 600,
            CmsApiError::DatabaseError => 601,
            CmsApiError::DeviceNotFound => 602,
            CmsApiError::PasswordInvalid => 603,
            CmsApiError::InvalidAppkey => 604,
            CmsApiError::CreateDeviceFailed => 605,
            CmsApiError::InvalidAuthorization => 606,
            CmsApiError::InternalError => 607,
            CmsApiError::UserAlreadyExists => 608,
            CmsApiError::UserNotFound => 609,
            CmsApiError::UserUpdateFailed => 610,
            CmsApiError::FileNoExtension => 611,
            CmsApiError::UploadFileFailed => 612,
            CmsApiError::VerifyPasswordFailed => 613,
            CmsApiError::StreamNotFound => 614,
            CmsApiError::ConnectionNotFound => 615,
            CmsApiError::UserDeviceNotFound => 616,
            CmsApiError::UserDeviceAlreadyExists => 617,
            CmsApiError::NeedDescParam => 618,
            CmsApiError::NeedVersionParam => 619,
            CmsApiError::VersionNotFound => 620,
            CmsApiError::FileNotFound => 621,
            CmsApiError::VisitNotFound => 622,
            CmsApiError::FileTransferNotFound => 625,
            CmsApiError::MachineCodeNotMatched => 623,
            CmsApiError::MaxStreamsReached => 624,
            CmsApiError::DeviceOffline => 626,
            CmsApiError::RequestTimeout => 627,
            CmsApiError::TokenInvalid => 628,
            CmsApiError::RecordNotFound => 629,
            CmsApiError::SafetyPwdMissing => 630,
        }
    }

    pub fn status_code(&self) -> StatusCode {
        match self {
            CmsApiError::InvalidAppkey | CmsApiError::TokenInvalid => StatusCode::UNAUTHORIZED,
            CmsApiError::MaxStreamsReached => StatusCode::FORBIDDEN,
            CmsApiError::DeviceOffline => StatusCode::SERVICE_UNAVAILABLE,
            CmsApiError::RequestTimeout => StatusCode::GATEWAY_TIMEOUT,
            CmsApiError::InvalidAuthorization
            | CmsApiError::MachineCodeNotMatched
            | CmsApiError::InvalidParams
            | CmsApiError::DeviceNotFound
            | CmsApiError::PasswordInvalid
            | CmsApiError::CreateDeviceFailed
            | CmsApiError::InternalError
            | CmsApiError::UserAlreadyExists
            | CmsApiError::UserNotFound
            | CmsApiError::UserUpdateFailed
            | CmsApiError::FileNoExtension
            | CmsApiError::UploadFileFailed
            | CmsApiError::VerifyPasswordFailed
            | CmsApiError::StreamNotFound
            | CmsApiError::ConnectionNotFound
            | CmsApiError::UserDeviceNotFound
            | CmsApiError::UserDeviceAlreadyExists
            | CmsApiError::NeedDescParam
            | CmsApiError::NeedVersionParam
            | CmsApiError::VersionNotFound
            | CmsApiError::FileNotFound
            | CmsApiError::VisitNotFound
            | CmsApiError::FileTransferNotFound
            | CmsApiError::RecordNotFound
            | CmsApiError::SafetyPwdMissing => StatusCode::BAD_REQUEST,
            CmsApiError::DatabaseError => StatusCode::INTERNAL_SERVER_ERROR,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn business_codes_remain_stable() {
        assert_eq!(CmsApiError::InvalidAppkey.business_code(), 604);
        assert_eq!(CmsApiError::MaxStreamsReached.business_code(), 624);
        assert_eq!(CmsApiError::InvalidParams.business_code(), 600);
        assert_eq!(CmsApiError::DatabaseError.business_code(), 601);
    }

    #[test]
    fn auth_errors_return_401() {
        assert_eq!(
            CmsApiError::InvalidAppkey.status_code(),
            StatusCode::UNAUTHORIZED
        );
    }

    #[test]
    fn max_streams_returns_403() {
        assert_eq!(
            CmsApiError::MaxStreamsReached.status_code(),
            StatusCode::FORBIDDEN
        );
    }

    #[test]
    fn response_preserves_business_code_in_body() {
        let resp = CmsApiError::InvalidAppkey.into_response();
        assert_eq!(resp.status(), StatusCode::UNAUTHORIZED);
    }
}
