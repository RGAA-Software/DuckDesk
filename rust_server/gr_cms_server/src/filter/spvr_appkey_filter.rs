use axum::body::Body;
use axum::http::{Request, StatusCode};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;
use crate::gAuthManager;
use crate::spvr_api_error::SpvrApiError;

#[derive(Debug, Deserialize)]
struct AppkeyQueryParams {
    appkey: String,
}

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    let path = req.uri().path().to_string();
    // tracing::info!("path: {}, uri: {}", path, req.uri().to_string());
    if req.uri().to_string().contains("/get/authorization") // x
        || req.uri().to_string().contains("/get/used/time") // x
        || req.uri().to_string().contains("/update/authorization") // x
        || req.uri().to_string().contains("/servers/config") // x
        || req.uri().to_string().contains("/gen/access/info") // x
        || req.uri().to_string().contains("/gen/raw/access/info") // x
        || req.uri().to_string().contains("/static")
        || req.uri().to_string().contains("/web")
        || req.uri().to_string().contains("/assets")
        || req.uri().to_string().contains("/favicon.ico")
        || req.uri().to_string().contains("/index.html")
        || path == "/"
    {
        return next.run(req).await;
    }
    if let Some(query) = req.uri().query() {
        if let Ok(params) = serde_urlencoded::from_str::<AppkeyQueryParams>(query) {
            if gAuthManager.lock().await.verify_appkey(params.appkey).await {
                return next.run(req).await;
            }
        }
    }
    tracing::error!("appkey is valid: {}", req.uri());
    SpvrApiError::InvalidAppkey.into_response()
}