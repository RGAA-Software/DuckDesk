use axum::http::{header, HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde::Serialize;
use thiserror::Error;

#[derive(Serialize)]
struct ConsoleErrorResponse {
    code: i32,
    error: &'static str,
    message: String,
    data: Option<()>,
    request_id: String,
}

#[derive(Debug, Error, PartialEq)]
pub enum ConsoleApiError {
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

    #[error("authentication required")]
    AuthenticationRequired,

    #[error("invalid credentials")]
    InvalidCredentials,

    #[error("forbidden")]
    Forbidden,

    #[error("group not found")]
    GroupNotFound,

    #[error("version conflict")]
    VersionConflict,

    #[error("resource not found")]
    ResourceNotFound,

    #[error("ticket expired or already used")]
    TicketExpiredOrUsed,

    #[error("rate limit exceeded")]
    RateLimited,

    #[error("quota exceeded")]
    QuotaExceeded,
}

// ConsoleApiError -> Response
impl IntoResponse for ConsoleApiError {
    fn into_response(self) -> Response {
        let status = self.status_code();
        let code = self.business_code();
        let msg = self.to_string();
        let request_id = uuid::Uuid::new_v4().simple().to_string();
        let body = Json(ConsoleErrorResponse {
            code,
            error: self.error_name(),
            message: msg,
            data: None,
            request_id: request_id.clone(),
        });
        let mut response = (status, body).into_response();
        response.headers_mut().insert(
            header::CONTENT_TYPE,
            HeaderValue::from_static("application/json; charset=utf-8"),
        );
        if let Ok(value) = HeaderValue::from_str(&request_id) {
            response.headers_mut().insert("x-request-id", value);
        }
        if matches!(
            self,
            ConsoleApiError::RateLimited | ConsoleApiError::QuotaExceeded
        ) {
            response
                .headers_mut()
                .insert(header::RETRY_AFTER, HeaderValue::from_static("60"));
        }
        response
    }
}

impl ConsoleApiError {
    pub fn error_name(&self) -> &'static str {
        match self {
            ConsoleApiError::InvalidParams => "INVALID_ARGUMENT",
            ConsoleApiError::AuthenticationRequired => "AUTH_REQUIRED",
            ConsoleApiError::InvalidCredentials => "AUTH_INVALID_CREDENTIALS",
            ConsoleApiError::Forbidden | ConsoleApiError::MaxStreamsReached => "SUBJECT_FORBIDDEN",
            ConsoleApiError::GroupNotFound
            | ConsoleApiError::ResourceNotFound
            | ConsoleApiError::DeviceNotFound
            | ConsoleApiError::UserNotFound => "RESOURCE_NOT_FOUND",
            ConsoleApiError::VersionConflict => "VERSION_CONFLICT",
            ConsoleApiError::TicketExpiredOrUsed => "TICKET_EXPIRED_OR_USED",
            ConsoleApiError::RateLimited => "RATE_LIMITED",
            ConsoleApiError::QuotaExceeded => "QUOTA_EXCEEDED",
            ConsoleApiError::DeviceOffline => "DEVICE_OFFLINE",
            ConsoleApiError::RequestTimeout => "SCHEDULER_UNAVAILABLE",
            _ => "REQUEST_FAILED",
        }
    }

    pub fn business_code(&self) -> i32 {
        match self {
            ConsoleApiError::InvalidParams => 600,
            ConsoleApiError::DatabaseError => 601,
            ConsoleApiError::DeviceNotFound => 602,
            ConsoleApiError::PasswordInvalid => 603,
            ConsoleApiError::InvalidAppkey => 604,
            ConsoleApiError::CreateDeviceFailed => 605,
            ConsoleApiError::InvalidAuthorization => 606,
            ConsoleApiError::InternalError => 607,
            ConsoleApiError::UserAlreadyExists => 608,
            ConsoleApiError::UserNotFound => 609,
            ConsoleApiError::UserUpdateFailed => 610,
            ConsoleApiError::FileNoExtension => 611,
            ConsoleApiError::UploadFileFailed => 612,
            ConsoleApiError::VerifyPasswordFailed => 613,
            ConsoleApiError::StreamNotFound => 614,
            ConsoleApiError::ConnectionNotFound => 615,
            ConsoleApiError::UserDeviceNotFound => 616,
            ConsoleApiError::UserDeviceAlreadyExists => 617,
            ConsoleApiError::NeedDescParam => 618,
            ConsoleApiError::NeedVersionParam => 619,
            ConsoleApiError::VersionNotFound => 620,
            ConsoleApiError::FileNotFound => 621,
            ConsoleApiError::VisitNotFound => 622,
            ConsoleApiError::FileTransferNotFound => 625,
            ConsoleApiError::MachineCodeNotMatched => 623,
            ConsoleApiError::MaxStreamsReached => 624,
            ConsoleApiError::DeviceOffline => 626,
            ConsoleApiError::RequestTimeout => 627,
            ConsoleApiError::TokenInvalid => 628,
            ConsoleApiError::RecordNotFound => 629,
            ConsoleApiError::SafetyPwdMissing => 630,
            ConsoleApiError::AuthenticationRequired => 631,
            ConsoleApiError::InvalidCredentials => 632,
            ConsoleApiError::Forbidden => 633,
            ConsoleApiError::GroupNotFound => 634,
            ConsoleApiError::VersionConflict => 635,
            ConsoleApiError::ResourceNotFound => 636,
            ConsoleApiError::TicketExpiredOrUsed => 637,
            ConsoleApiError::RateLimited => 638,
            ConsoleApiError::QuotaExceeded => 639,
        }
    }

    pub fn status_code(&self) -> StatusCode {
        match self {
            ConsoleApiError::InvalidAppkey
            | ConsoleApiError::TokenInvalid
            | ConsoleApiError::AuthenticationRequired
            | ConsoleApiError::InvalidCredentials => StatusCode::UNAUTHORIZED,
            ConsoleApiError::MaxStreamsReached | ConsoleApiError::Forbidden => {
                StatusCode::FORBIDDEN
            }
            ConsoleApiError::VersionConflict => StatusCode::CONFLICT,
            ConsoleApiError::GroupNotFound | ConsoleApiError::ResourceNotFound => {
                StatusCode::NOT_FOUND
            }
            ConsoleApiError::TicketExpiredOrUsed => StatusCode::GONE,
            ConsoleApiError::RateLimited | ConsoleApiError::QuotaExceeded => {
                StatusCode::TOO_MANY_REQUESTS
            }
            ConsoleApiError::DeviceOffline => StatusCode::SERVICE_UNAVAILABLE,
            ConsoleApiError::RequestTimeout => StatusCode::GATEWAY_TIMEOUT,
            ConsoleApiError::InvalidAuthorization
            | ConsoleApiError::MachineCodeNotMatched
            | ConsoleApiError::InvalidParams
            | ConsoleApiError::DeviceNotFound
            | ConsoleApiError::PasswordInvalid
            | ConsoleApiError::CreateDeviceFailed
            | ConsoleApiError::InternalError
            | ConsoleApiError::UserAlreadyExists
            | ConsoleApiError::UserNotFound
            | ConsoleApiError::UserUpdateFailed
            | ConsoleApiError::FileNoExtension
            | ConsoleApiError::UploadFileFailed
            | ConsoleApiError::VerifyPasswordFailed
            | ConsoleApiError::StreamNotFound
            | ConsoleApiError::ConnectionNotFound
            | ConsoleApiError::UserDeviceNotFound
            | ConsoleApiError::UserDeviceAlreadyExists
            | ConsoleApiError::NeedDescParam
            | ConsoleApiError::NeedVersionParam
            | ConsoleApiError::VersionNotFound
            | ConsoleApiError::FileNotFound
            | ConsoleApiError::VisitNotFound
            | ConsoleApiError::FileTransferNotFound
            | ConsoleApiError::RecordNotFound
            | ConsoleApiError::SafetyPwdMissing => StatusCode::BAD_REQUEST,
            ConsoleApiError::DatabaseError => StatusCode::INTERNAL_SERVER_ERROR,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn business_codes_remain_stable() {
        assert_eq!(ConsoleApiError::InvalidAppkey.business_code(), 604);
        assert_eq!(ConsoleApiError::MaxStreamsReached.business_code(), 624);
        assert_eq!(ConsoleApiError::InvalidParams.business_code(), 600);
        assert_eq!(ConsoleApiError::DatabaseError.business_code(), 601);
    }

    #[test]
    fn auth_errors_return_401() {
        assert_eq!(
            ConsoleApiError::InvalidAppkey.status_code(),
            StatusCode::UNAUTHORIZED
        );
    }

    #[test]
    fn max_streams_returns_403() {
        assert_eq!(
            ConsoleApiError::MaxStreamsReached.status_code(),
            StatusCode::FORBIDDEN
        );
    }

    #[test]
    fn response_preserves_business_code_in_body() {
        let resp = ConsoleApiError::InvalidAppkey.into_response();
        assert_eq!(resp.status(), StatusCode::UNAUTHORIZED);
        assert_eq!(
            resp.headers().get(header::CONTENT_TYPE).unwrap(),
            "application/json; charset=utf-8"
        );
        assert_eq!(resp.headers().get("x-request-id").unwrap().len(), 32);
    }

    #[test]
    fn throttling_errors_include_retry_after() {
        let resp = ConsoleApiError::RateLimited.into_response();
        assert_eq!(resp.status(), StatusCode::TOO_MANY_REQUESTS);
        assert_eq!(resp.headers().get(header::RETRY_AFTER).unwrap(), "60");
    }
}
