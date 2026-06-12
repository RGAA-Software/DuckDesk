use axum::Json;
use axum::response::{IntoResponse, Response};
use thiserror::Error;
use gr_base::RespMessage;

#[derive(Debug, Error)]
pub enum RelayApiError {
    #[error("Invalid parameters")]
    InvalidParams,

    #[error("Database operation failed")]
    DatabaseError,

    #[error("Room not found")]
    RoomNotFound,

    #[error("Device not found")]
    DeviceNotFound,

    #[error("Notify event failed")]
    NotifyEventFailed,
    
    #[error("Invalid Appkey")]
    InvalidAppkey
}

impl IntoResponse for RelayApiError {
    fn into_response(self) -> Response {
        let (code, msg) = match self {
            RelayApiError::InvalidParams => (700, self.to_string()),
            RelayApiError::DatabaseError => (701, self.to_string()),
            RelayApiError::RoomNotFound => (702, self.to_string()),
            RelayApiError::DeviceNotFound => (703, self.to_string()),
            RelayApiError::NotifyEventFailed => (704, self.to_string()),
            RelayApiError::InvalidAppkey => (705, self.to_string()),
        };

        let body = Json(RespMessage::new_data(code, msg, ""));
        (axum::http::StatusCode::from_u16(code as u16).unwrap(), body).into_response()
    }
}
