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
///
/// Note: when a router is mounted via axum's `nest()`, the middleware layered
/// inside the nested router sees the path **with the nest prefix stripped**.
/// For example `/api/v1/auth/control/update/authorization` is seen as
/// `/update/authorization` by this filter. Therefore the whitelist stores the
/// stripped (post-nest) paths for nested routes, and full paths for routes
/// mounted directly on the root router.
///
/// `update/authorization` is whitelisted because it is the endpoint used to
/// upload a new license when the current one is expired or invalid. Requiring
/// a valid appkey here creates a dead loop: the old appkey is invalid (expired),
/// so the update is rejected, but the only way to get a valid appkey is to
/// update the authorization. The endpoint instead authenticates via Ed25519
/// signature verification on the uploaded license.
const APPKEY_FILTER_WHITELIST: &[&str] = &[
    // Nested under /api/v1/spvr/control -> stripped path
    "/servers/config",
    "/gen/access/info",
    "/gen/raw/access/info",
    // Nested under /api/v1/auth/control -> stripped path
    "/update/authorization",
    // Root-level static paths
    "/",
    "/favicon.ico",
    "/index.html",
];

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    let path = req.uri().path();
    let full_uri = req.uri().to_string();

    if APPKEY_FILTER_WHITELIST.contains(&path)
        || path.starts_with("/static/")
        || path.starts_with("/web/")
        || path.starts_with("/assets/")
    {
        tracing::info!("appkey filter: whitelisted path='{}' uri='{}'", path, full_uri);
        return next.run(req).await;
    }

    let query = req.uri().query().unwrap_or("");
    let parsed = serde_urlencoded::from_str::<AppkeyQueryParams>(query);
    let req_appkey = parsed.as_ref().map(|p| p.appkey.clone()).unwrap_or_default();

    if req_appkey.is_empty() {
        tracing::warn!(
            "appkey filter: no appkey in query, path='{}' uri='{}'",
            path,
            full_uri
        );
        return SpvrApiError::InvalidAppkey.into_response();
    }

    let auth = gAuthManager.lock().await.get_auth().await;
    if auth.auth_id.is_empty() {
        tracing::warn!(
            "appkey filter: no authorization loaded in CMS, \
             path='{}' req_appkey='{}' stored_appkey='(empty)'",
            path,
            req_appkey
        );
        return SpvrApiError::InvalidAppkey.into_response();
    }

    let stored_appkey = auth.appkey.clone();
    let stored_app_secret = auth.app_secret.clone();
    let expected_app_secret = calculate_app_secret_helper(&req_appkey);

    if stored_appkey != req_appkey {
        tracing::warn!(
            "appkey filter: appkey mismatch, path='{}' \
             req_appkey='{}' stored_appkey='{}'",
            path,
            req_appkey,
            stored_appkey
        );
        return SpvrApiError::InvalidAppkey.into_response();
    }

    if stored_app_secret != expected_app_secret {
        tracing::warn!(
            "appkey filter: app_secret mismatch, path='{}' \
             req_appkey='{}' expected_secret='{}' stored_secret='{}'",
            path,
            req_appkey,
            expected_app_secret,
            stored_app_secret
        );
        return SpvrApiError::InvalidAppkey.into_response();
    }

    tracing::info!(
        "appkey filter: OK path='{}' appkey='{}'",
        path,
        req_appkey
    );
    next.run(req).await
}

fn calculate_app_secret_helper(appkey: &str) -> String {
    use gr_auth_mgr::app_secret_util::calculate_app_secret;
    calculate_app_secret(appkey.to_string())
}
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn whitelist_uses_exact_paths() {
        // Exact whitelisted (stripped) paths must match.
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/servers/config"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/update/authorization"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/"));

        // Other auth endpoints must NOT be whitelisted.
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/get/authorization"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/update/password"));

        // Substring or prefix variants must NOT be whitelisted (defense against bypasses).
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/servers/config/extra"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/prefix/servers/config"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/index.html/extra"));
    }
}
