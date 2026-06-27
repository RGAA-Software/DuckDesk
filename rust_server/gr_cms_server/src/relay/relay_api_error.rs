use axum::response::{IntoResponse, Response};
use axum::Json;
use gr_base::RespMessage;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum RelayApiError {
    #[error("Invalid parameters")]
    InvalidParams,

    #[error("Room not found")]
    RoomNotFound,

    #[error("Device not found")]
    DeviceNotFound,

    #[error("Notify event failed")]
    NotifyEventFailed,
}

impl IntoResponse for RelayApiError {
    fn into_response(self) -> Response {
        let (code, msg) = match self {
            RelayApiError::InvalidParams => (700, self.to_string()),
            RelayApiError::RoomNotFound => (701, self.to_string()),
            RelayApiError::DeviceNotFound => (702, self.to_string()),
            RelayApiError::NotifyEventFailed => (703, self.to_string()),
        };

        let body = Json(RespMessage::new_data(code, msg, ""));
        (axum::http::StatusCode::from_u16(code as u16).unwrap(), body).into_response()
    }
}
