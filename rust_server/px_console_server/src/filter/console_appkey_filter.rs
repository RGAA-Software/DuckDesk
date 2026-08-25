use crate::connection_ticket::manager::ConnectionTicketManager;
use crate::console_api_error::ConsoleApiError;
use crate::gAuthManager;
use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;
use std::collections::HashMap;

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
/// `get/auth/status` is whitelisted so the login page can show the machine
/// code / authorization state before any authorization exists. It returns a
/// safe view without any credential fields.
const APPKEY_FILTER_WHITELIST: &[&str] = &[
    // Nested under /api/v1/console/control -> stripped path
    "/servers/config",
    "/gen/access/info",
    "/gen/raw/access/info",
    // Nested under /api/v1/auth/control -> stripped path
    "/pull/authorization",
    "/get/auth/status",
    // Root-level static paths
    "/",
    "/favicon.ico",
    "/index.html",
];

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    let path = req.uri().path();

    // force_authorize=false: skip all appkey checks (local/test deployments).
    if crate::console_settings::is_auth_bypassed().await {
        return next.run(req).await;
    }

    if APPKEY_FILTER_WHITELIST.contains(&path)
        || path.starts_with("/static/")
        || path.starts_with("/web/")
        || path.starts_with("/assets/")
    {
        tracing::info!("appkey filter: whitelisted path='{}'", path);
        return next.run(req).await;
    }

    // Browser standard WebRTC uses the application Relay only for signaling.
    // It cannot receive the installation appkey, so accept a fully bound,
    // short-lived ticket without consuming it. The Render atomically consumes
    // the same ticket when it processes the SDP offer.
    if path == "/relay" {
        let query = req.uri().query().unwrap_or("");
        let params =
            serde_urlencoded::from_str::<HashMap<String, String>>(query).unwrap_or_default();
        if params.get("rtc_signal").is_some_and(|value| value == "1") {
            let valid = match (
                params.get("ticket"),
                params.get("client_nonce"),
                params.get("remote_device_id"),
            ) {
                (Some(ticket), Some(nonce), Some(device_id)) => {
                    ConnectionTicketManager::lookup_active(
                        ticket,
                        device_id,
                        nonce,
                        params.get("instance_id").map(String::as_str),
                    )
                    .await
                    .is_ok()
                }
                _ => false,
            };
            if valid {
                return next.run(req).await;
            }
            return ConsoleApiError::TicketExpiredOrUsed.into_response();
        }
    }

    // Prefer a header so credentials are not copied into URLs, proxy logs or
    // browser history. Query authentication remains as a compatibility path
    // for older deployed native clients.
    let header_appkey = req
        .headers()
        .get("x-px-appkey")
        .and_then(|value| value.to_str().ok())
        .unwrap_or_default();
    let req_appkey = if header_appkey.is_empty() {
        let query = req.uri().query().unwrap_or("");
        serde_urlencoded::from_str::<AppkeyQueryParams>(query)
            .map(|params| params.appkey)
            .unwrap_or_default()
    } else {
        header_appkey.to_string()
    };

    if req_appkey.is_empty() {
        tracing::warn!("appkey filter: no appkey in query, path='{}'", path);
        return ConsoleApiError::InvalidAppkey.into_response();
    }

    let auth = gAuthManager.lock().await.get_auth().await;
    if auth.auth_id.is_empty() {
        tracing::warn!(
            "appkey filter: no authorization loaded in Console, path='{}'",
            path
        );
        return ConsoleApiError::InvalidAppkey.into_response();
    }

    let stored_appkey = auth.appkey.clone();
    let stored_app_secret = auth.app_secret.clone();
    let expected_app_secret = calculate_app_secret_helper(&req_appkey);

    if stored_appkey != req_appkey {
        tracing::warn!("appkey filter: appkey mismatch, path='{}'", path);
        return ConsoleApiError::InvalidAppkey.into_response();
    }

    if stored_app_secret != expected_app_secret {
        tracing::warn!("appkey filter: app_secret mismatch, path='{}'", path);
        return ConsoleApiError::InvalidAppkey.into_response();
    }

    tracing::info!("appkey filter: OK path='{}'", path);
    next.run(req).await
}

fn calculate_app_secret_helper(appkey: &str) -> String {
    use px_auth_mgr::app_secret_util::calculate_app_secret;
    calculate_app_secret(appkey.to_string())
}
#[cfg(test)]
mod tests {
    use super::*;
    use axum::http::HeaderMap;

    #[test]
    fn whitelist_uses_exact_paths() {
        // Exact whitelisted (stripped) paths must match.
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/servers/config"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/pull/authorization"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/"));

        // Other auth endpoints must NOT be whitelisted.
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/get/authorization"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/update/password"));
        // Safe status is public, credential verification is not.
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/verify/auth/account"));
        assert!(APPKEY_FILTER_WHITELIST.contains(&"/get/auth/status"));

        // Substring or prefix variants must NOT be whitelisted (defense against bypasses).
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/servers/config/extra"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/prefix/servers/config"));
        assert!(!APPKEY_FILTER_WHITELIST.contains(&"/index.html/extra"));
    }

    #[test]
    fn appkey_header_is_valid_http_header() {
        let mut headers = HeaderMap::new();
        headers.insert("x-px-appkey", "key-1".parse().unwrap());
        assert_eq!(headers.get("x-px-appkey").unwrap(), "key-1");
    }
}
