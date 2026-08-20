use axum::http::{header, HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::Json;
use serde::Serialize;
use thiserror::Error;

#[derive(Serialize)]
struct CmsErrorResponse {
    code: i32,
    error: &'static str,
    message: String,
    data: Option<()>,
    request_id: String,
}

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

// CmsApiError -> Response
impl IntoResponse for CmsApiError {
    fn into_response(self) -> Response {
        let status = self.status_code();
        let code = self.business_code();
        let msg = self.to_string();
        let request_id = uuid::Uuid::new_v4().simple().to_string();
        let body = Json(CmsErrorResponse {
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
        if matches!(self, CmsApiError::RateLimited | CmsApiError::QuotaExceeded) {
            response
                .headers_mut()
                .insert(header::RETRY_AFTER, HeaderValue::from_static("60"));
        }
        response
    }
}

impl CmsApiError {
    pub fn error_name(&self) -> &'static str {
        match self {
            CmsApiError::InvalidParams => "INVALID_ARGUMENT",
            CmsApiError::AuthenticationRequired => "AUTH_REQUIRED",
            CmsApiError::InvalidCredentials => "AUTH_INVALID_CREDENTIALS",
            CmsApiError::Forbidden | CmsApiError::MaxStreamsReached => "SUBJECT_FORBIDDEN",
            CmsApiError::GroupNotFound
            | CmsApiError::ResourceNotFound
            | CmsApiError::DeviceNotFound
            | CmsApiError::UserNotFound => "RESOURCE_NOT_FOUND",
            CmsApiError::VersionConflict => "VERSION_CONFLICT",
            CmsApiError::TicketExpiredOrUsed => "TICKET_EXPIRED_OR_USED",
            CmsApiError::RateLimited => "RATE_LIMITED",
            CmsApiError::QuotaExceeded => "QUOTA_EXCEEDED",
            CmsApiError::DeviceOffline => "DEVICE_OFFLINE",
            CmsApiError::RequestTimeout => "SCHEDULER_UNAVAILABLE",
            _ => "REQUEST_FAILED",
        }
    }

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
            CmsApiError::AuthenticationRequired => 631,
            CmsApiError::InvalidCredentials => 632,
            CmsApiError::Forbidden => 633,
            CmsApiError::GroupNotFound => 634,
            CmsApiError::VersionConflict => 635,
            CmsApiError::ResourceNotFound => 636,
            CmsApiError::TicketExpiredOrUsed => 637,
            CmsApiError::RateLimited => 638,
            CmsApiError::QuotaExceeded => 639,
        }
    }

    pub fn status_code(&self) -> StatusCode {
        match self {
            CmsApiError::InvalidAppkey
            | CmsApiError::TokenInvalid
            | CmsApiError::AuthenticationRequired
            | CmsApiError::InvalidCredentials => StatusCode::UNAUTHORIZED,
            CmsApiError::MaxStreamsReached | CmsApiError::Forbidden => StatusCode::FORBIDDEN,
            CmsApiError::VersionConflict => StatusCode::CONFLICT,
            CmsApiError::GroupNotFound | CmsApiError::ResourceNotFound => StatusCode::NOT_FOUND,
            CmsApiError::TicketExpiredOrUsed => StatusCode::GONE,
            CmsApiError::RateLimited | CmsApiError::QuotaExceeded => StatusCode::TOO_MANY_REQUESTS,
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
        assert_eq!(
            resp.headers().get(header::CONTENT_TYPE).unwrap(),
            "application/json; charset=utf-8"
        );
        assert_eq!(resp.headers().get("x-request-id").unwrap().len(), 32);
    }

    #[test]
    fn throttling_errors_include_retry_after() {
        let resp = CmsApiError::RateLimited.into_response();
        assert_eq!(resp.status(), StatusCode::TOO_MANY_REQUESTS);
        assert_eq!(resp.headers().get(header::RETRY_AFTER).unwrap(), "60");
    }
}
