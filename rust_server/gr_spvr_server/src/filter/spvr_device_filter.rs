use axum::body::Body;
use axum::http::{Request, StatusCode};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;
use crate::gDeviceManager;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_http_util::{get_int_param, get_str_param};

#[derive(Debug, Deserialize)]
struct DeviceIdQueryParams {
    device_id: String,
}

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    if let Some(query) = req.uri().query() {
        if let Ok(params) = serde_urlencoded::from_str::<DeviceIdQueryParams>(query) {
            if !params.device_id.is_empty() {
                // check exists
                if let Err(e) = gDeviceManager
                    .query_device_by_id(params.device_id).await {
                    return e.into_response();
                }
                return next.run(req).await;
            }
        }
        else {
            return SpvrApiError::InvalidParams.into_response();
        }
    }
    SpvrApiError::DeviceNotFound.into_response()
}