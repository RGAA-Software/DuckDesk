use crate::gAuthManager;
use crate::spvr_api_error::SpvrApiError;
use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
struct AppkeyQueryParams {
    appkey: String,
}

/// Whitelisted paths that bypass appkey validation.
/// These must be exact matches to avoid substring bypasses.
/// Auth endpoints are intentionally NOT whitelisted; they must provide a valid appkey.
const APPKEY_FILTER_WHITELIST: &[&str] = &[
    "/api/v1/spvr/control/servers/config",
    "/api/v1/spvr/control/gen/access/info",
    "/api/v1/spvr/control/gen/raw/access/info",
    "/",
    "/favicon.ico",
    "/index.html",
];

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    let path = req.uri().path();
    if APPKEY_FILTER_WHITELIST.contains(&path)
        || path.starts_with("/static/")
        || path.starts_with("/web/")
        || path.starts_with("/assets/")
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
    tracing::error!("appkey invalid for path: {}", path);
    SpvrApiError::InvalidAppkey.into_response()
}
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn whitelist_uses_exact_paths() {
        // Exact whitelisted paths must match.
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/api/v1/spvr/control/servers/config"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/"));

        // Substring or prefix variants must NOT be whitelisted (defense against bypasses).
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/api/v1/auth/control/get/authorization"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/api/v1/spvr/control/servers/config/extra"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/prefix/api/v1/spvr/control/servers/config"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/index.html/extra"));
    }
}
