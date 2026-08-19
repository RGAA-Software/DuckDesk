use crate::cms_api_error::CmsApiError;
use crate::gAuthManager;
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
/// For example `/api/v1/auth/control/pull/authorization` is seen as
/// `/pull/authorization` by this filter. Therefore the whitelist stores the
/// stripped (post-nest) paths for nested routes, and full paths for routes
/// mounted directly on the root router.
///
/// `pull/authorization` is whitelisted because it is the endpoint used to
/// fetch a license from the auth server when the current one is missing or
/// invalid. Requiring a valid appkey here creates a dead loop: there is no
/// valid appkey until an authorization is pulled. The endpoint instead relies
/// on the auth server being the authority for issuing signed licenses, and it
/// MUST NOT return credential fields (see AuthStatus).
///
/// `verify/auth/account` is whitelisted because it IS the credential check
/// (license username/password); on success it returns the appkey so the web
/// UI can call appkey-protected endpoints afterwards.
///
/// `get/auth/status` is whitelisted so the login page can show the machine
/// code / authorization state before any authorization exists. It returns a
/// safe view without any credential fields.
const APPKEY_FILTER_WHITELIST: &[&str] = &[
    // Nested under /api/v1/cms/control -> stripped path
    "/servers/config",
    "/gen/access/info",
    "/gen/raw/access/info",
    // Nested under /api/v1/auth/control -> stripped path
    "/pull/authorization",
    "/verify/auth/account",
    "/get/auth/status",
    // Root-level static paths
    "/",
    "/favicon.ico",
    "/index.html",
];

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    let path = req.uri().path();
    let full_uri = req.uri().to_string();

    // force_authorize=false: skip all appkey checks (local/test deployments).
    if crate::cms_settings::is_auth_bypassed().await {
        return next.run(req).await;
    }

    if APPKEY_FILTER_WHITELIST.contains(&path)
        || path.starts_with("/static/")
        || path.starts_with("/web/")
        || path.starts_with("/assets/")
    {
        tracing::info!(
            "appkey filter: whitelisted path='{}' uri='{}'",
            path,
            full_uri
        );
        return next.run(req).await;
    }

    let query = req.uri().query().unwrap_or("");
    let parsed = serde_urlencoded::from_str::<AppkeyQueryParams>(query);
    let req_appkey = parsed
        .as_ref()
        .map(|p| p.appkey.clone())
        .unwrap_or_default();

    if req_appkey.is_empty() {
        tracing::warn!(
            "appkey filter: no appkey in query, path='{}' uri='{}'",
            path,
            full_uri
        );
        return CmsApiError::InvalidAppkey.into_response();
    }

    let auth = gAuthManager.lock().await.get_auth().await;
    if auth.auth_id.is_empty() {
        tracing::warn!(
            "appkey filter: no authorization loaded in CMS, \
             path='{}' req_appkey='{}' stored_appkey='(empty)'",
            path,
            req_appkey
        );
        return CmsApiError::InvalidAppkey.into_response();
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
        return CmsApiError::InvalidAppkey.into_response();
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
        return CmsApiError::InvalidAppkey.into_response();
    }

    tracing::info!("appkey filter: OK path='{}' appkey='{}'", path, req_appkey);
    next.run(req).await
}

fn calculate_app_secret_helper(appkey: &str) -> String {
    use px_auth_mgr::app_secret_util::calculate_app_secret;
    calculate_app_secret(appkey.to_string())
}
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn whitelist_uses_exact_paths() {
        // Exact whitelisted (stripped) paths must match.
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/servers/config"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/pull/authorization"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/"));

        // Other auth endpoints must NOT be whitelisted.
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/get/authorization"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/update/password"));
        // Login & safe status endpoints ARE whitelisted (see doc comment).
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/verify/auth/account"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/get/auth/status"));

        // Substring or prefix variants must NOT be whitelisted (defense against bypasses).
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/servers/config/extra"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/prefix/servers/config"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/index.html/extra"));
    }
}
