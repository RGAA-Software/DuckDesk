use axum::Json;
use axum::response::{IntoResponse, Response};
use thiserror::Error;
use gr_base::RespMessage;

#[derive(Debug, Error)]
#[derive(PartialEq)]
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

    #[error("machine code not matched")]
    MachineCodeNotMatched,
}

// SpvrApiError -> Response
impl IntoResponse for SpvrApiError {
    fn into_response(self) -> Response {
        let (code, msg) = match self {
            SpvrApiError::InvalidParams => (600, self.to_string()),
            SpvrApiError::DatabaseError => (601, self.to_string()),
            SpvrApiError::DeviceNotFound => (602, self.to_string()),
            SpvrApiError::PasswordInvalid => (603, self.to_string()),
            SpvrApiError::InvalidAppkey => (604, self.to_string()),
            SpvrApiError::CreateDeviceFailed => (605, self.to_string()),
            SpvrApiError::InvalidAuthorization=> (606, self.to_string()),
            SpvrApiError::InternalError=> (607, self.to_string()),
            SpvrApiError::UserAlreadyExists=> (608, self.to_string()),
            SpvrApiError::UserNotFound=> (609, self.to_string()),
            SpvrApiError::UserUpdateFailed=> (610, self.to_string()),
            SpvrApiError::FileNoExtension=> (611, self.to_string()),
            SpvrApiError::UploadFileFailed=> (612, self.to_string()),
            SpvrApiError::VerifyPasswordFailed=> (613, self.to_string()),
            SpvrApiError::StreamNotFound=> (614, self.to_string()),
            SpvrApiError::ConnectionNotFound=> (615, self.to_string()),
            SpvrApiError::UserDeviceNotFound=> (616, self.to_string()),
            SpvrApiError::UserDeviceAlreadyExists=> (617, self.to_string()),
            SpvrApiError::NeedDescParam=> (618, self.to_string()),
            SpvrApiError::NeedVersionParam=> (619, self.to_string()),
            SpvrApiError::VersionNotFound=> (620, self.to_string()),
            SpvrApiError::FileNotFound=> (621, self.to_string()),
            SpvrApiError::VisitNotFound=> (622, self.to_string()),
            SpvrApiError::MachineCodeNotMatched=> (623, self.to_string()),
        };

        let body = Json(RespMessage::new_data(code, msg, ""));
        (axum::http::StatusCode::from_u16(code as u16).unwrap(), body).into_response()
    }
}
