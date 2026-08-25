use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use serde::Deserialize;

use crate::console_api_error::ConsoleApiError;
use crate::gAuthManager;
use crate::gConsoleClientConnMgr;
use crate::gUserSessionManager;
use crate::user::session_handler::admin_cookie_values;
use px_auth_mgr::auth_token::verify_connection_token;
use px_base::get_current_timestamp;

#[derive(Debug, Deserialize)]
struct WsTokenQueryParams {
    appkey: String,
    token: String,
    ts: i64,
    nonce: String,
}

/// Shared token verification logic for all Console WebSocket entry points.
///
/// - `check_max_streams`: only `/console/client` should enforce the authorization's
///   `max_streams` limit and pass a `StreamReservation` to the handler.
async fn verify_and_run(mut req: Request<Body>, next: Next, check_max_streams: bool) -> Response {
    let path = req.uri().path();
    // force_authorize=false: skip all token checks (local/test deployments).
    if crate::console_settings::is_auth_bypassed().await {
        tracing::info!("ws filter: BYPASS (force_authorize=false) path='{}'", path);
        if check_max_streams {
            // The client handler always extracts a StreamReservation. Bypass
            // authentication, not the handler's connection-slot contract.
            // Test/local deployments intentionally have no licensed limit.
            let reservation = gConsoleClientConnMgr
                .try_reserve_stream(i32::MAX)
                .expect("unlimited bypass stream reservation");
            req.extensions_mut().insert(reservation);
        }
        return next.run(req).await;
    }
    let query = req.uri().query().unwrap_or("");
    let params = match serde_urlencoded::from_str::<WsTokenQueryParams>(query) {
        Ok(p) => p,
        Err(e) => {
            // The raw query contains the connection token. Keep it out of logs.
            tracing::warn!(
                "ws filter: missing/malformed params path='{}' error='{}'",
                path,
                e
            );
            return ConsoleApiError::InvalidAppkey.into_response();
        }
    };

    let auth = gAuthManager.lock().await.get_auth().await;
    if auth.appkey.is_empty() || auth.app_secret.is_empty() {
        tracing::warn!("ws filter: no authorization loaded, path='{}'", path);
        return ConsoleApiError::InvalidAppkey.into_response();
    }

    if auth.appkey != params.appkey {
        tracing::warn!("ws filter: appkey mismatch, path='{}'", path);
        return ConsoleApiError::InvalidAppkey.into_response();
    }

    let now_ms = get_current_timestamp();
    if !verify_connection_token(
        &params.appkey,
        &auth.app_secret,
        &params.token,
        params.ts,
        &params.nonce,
        now_ms,
    ) {
        tracing::warn!(
            "ws filter: token verification failed, path='{}' ts={}",
            path,
            params.ts
        );
        return ConsoleApiError::InvalidAppkey.into_response();
    }

    tracing::info!("ws filter: OK path='{}'", path);

    if check_max_streams {
        let reservation = match crate::gConsoleClientConnMgr.try_reserve_stream(auth.max_streams) {
            Some(r) => r,
            None => {
                tracing::error!(
                    "max streams reached: {}/{}, rejecting client connection",
                    crate::gConsoleClientConnMgr.count_alive_connections().await,
                    auth.max_streams
                );
                return ConsoleApiError::MaxStreamsReached.into_response();
            }
        };
        req.extensions_mut().insert(reservation);
    }

    next.run(req).await
}

/// `/console/client` filter: verifies the HMAC token and enforces `max_streams`.
pub async fn client_filter(req: Request<Body>, next: Next) -> Response {
    verify_and_run(req, next, true).await
}

/// `/console/panel` filter: verifies the HMAC token only.
pub async fn panel_filter(req: Request<Body>, next: Next) -> Response {
    verify_and_run(req, next, false).await
}

/// `/console/website` filter: verifies the HMAC token only.
pub async fn website_filter(req: Request<Body>, next: Next) -> Response {
    if crate::console_settings::is_auth_bypassed().await {
        return next.run(req).await;
    }
    for token in admin_cookie_values(req.headers()) {
        if gUserSessionManager.authenticate_admin(&token).await.is_ok() {
            return next.run(req).await;
        }
    }
    ConsoleApiError::AuthenticationRequired.into_response()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::gAuthManager;
    use crate::net_client::console_client_conn_mgr::StreamReservation;
    use axum::body::Body;
    use axum::extract::Extension;
    use axum::http::{Request, StatusCode};
    use axum::routing::get;
    use axum::Router;
    use px_auth_mgr::auth_token::generate_connection_token;
    use px_auth_mgr::authorization::Authorization;
    use std::sync::Mutex;
    use tower::ServiceExt;

    static TOKEN_TEST_LOCK: Mutex<()> = Mutex::new(());

    fn test_auth(appkey: &str, app_secret: &str, max_streams: i32) -> Authorization {
        Authorization {
            auth_id: "auth-1".to_string(),
            auth_name: "name".to_string(),
            machine_code: "mc-1".to_string(),
            appkey: appkey.to_string(),
            app_secret: app_secret.to_string(),
            max_streams,
            ..Default::default()
        }
    }

    fn client_router() -> Router {
        Router::new().route(
            "/console/client",
            get(|Extension(_reservation): Extension<StreamReservation>| async { "ok" })
                .layer(axum::middleware::from_fn(client_filter)),
        )
    }

    fn panel_router() -> Router {
        Router::new().route(
            "/console/panel",
            get(|| async { "ok" }).layer(axum::middleware::from_fn(panel_filter)),
        )
    }

    fn website_router() -> Router {
        Router::new().route(
            "/console/website",
            get(|| async { "ok" }).layer(axum::middleware::from_fn(website_filter)),
        )
    }

    #[test]
    fn accepts_valid_token_params() {
        let token = generate_connection_token("appkey-1", "secret-1");
        let query = format!(
            "appkey=appkey-1&token={}&ts={}&nonce={}",
            token.token, token.ts, token.nonce
        );
        let parsed = serde_urlencoded::from_str::<WsTokenQueryParams>(&query).unwrap();
        assert_eq!(parsed.appkey, "appkey-1");
        assert_eq!(parsed.token, token.token);
        assert_eq!(parsed.ts, token.ts);
        assert_eq!(parsed.nonce, token.nonce);
    }

    #[test]
    fn rejects_missing_params() {
        assert!(serde_urlencoded::from_str::<WsTokenQueryParams>("appkey=1&token=2&ts=3").is_err());
        assert!(
            serde_urlencoded::from_str::<WsTokenQueryParams>("appkey=1&token=2&nonce=n").is_err()
        );
    }

    #[tokio::test]
    async fn client_rejects_when_auth_not_loaded() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = true;
        gAuthManager
            .lock()
            .await
            .update_auth(Default::default())
            .await;

        let response = client_router()
            .oneshot(
                Request::builder()
                    .uri("/console/client?appkey=k&token=t&ts=1&nonce=n")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::UNAUTHORIZED);
    }

    #[tokio::test]
    async fn client_rejects_invalid_token() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = true;
        gAuthManager
            .lock()
            .await
            .update_auth(test_auth("appkey-1", "secret-1", 10))
            .await;

        let response = client_router()
            .oneshot(
                Request::builder()
                    .uri("/console/client?appkey=appkey-1&token=bad-token&ts=1&nonce=n")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::UNAUTHORIZED);
    }

    #[tokio::test]
    async fn client_accepts_valid_token() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = true;
        gAuthManager
            .lock()
            .await
            .update_auth(test_auth("appkey-1", "secret-1", 10))
            .await;

        let token = generate_connection_token("appkey-1", "secret-1");
        let uri = format!(
            "/console/client?appkey=appkey-1&token={}&ts={}&nonce={}",
            token.token, token.ts, token.nonce
        );

        let response = client_router()
            .oneshot(Request::builder().uri(&uri).body(Body::empty()).unwrap())
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn client_rejects_when_max_streams_reached() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = true;
        gAuthManager
            .lock()
            .await
            .update_auth(test_auth("appkey-1", "secret-1", 1))
            .await;

        let reservation = crate::gConsoleClientConnMgr.try_reserve_stream(1).unwrap();

        let token = generate_connection_token("appkey-1", "secret-1");
        let uri = format!(
            "/console/client?appkey=appkey-1&token={}&ts={}&nonce={}",
            token.token, token.ts, token.nonce
        );

        let response = client_router()
            .oneshot(Request::builder().uri(&uri).body(Body::empty()).unwrap())
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::FORBIDDEN);

        drop(reservation);
    }

    #[tokio::test]
    async fn panel_accepts_valid_token() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = true;
        gAuthManager
            .lock()
            .await
            .update_auth(test_auth("appkey-1", "secret-1", 10))
            .await;

        let token = generate_connection_token("appkey-1", "secret-1");
        let uri = format!(
            "/console/panel?appkey=appkey-1&token={}&ts={}&nonce={}&device_id=d&user_id=u",
            token.token, token.ts, token.nonce
        );

        let response = panel_router()
            .oneshot(Request::builder().uri(&uri).body(Body::empty()).unwrap())
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn panel_rejects_missing_token() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = true;
        gAuthManager
            .lock()
            .await
            .update_auth(test_auth("appkey-1", "secret-1", 10))
            .await;

        let response = panel_router()
            .oneshot(
                Request::builder()
                    .uri("/console/panel?appkey=appkey-1&device_id=d&user_id=u")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::UNAUTHORIZED);
    }

    #[tokio::test]
    async fn website_rejects_native_appkey_token() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = true;
        gAuthManager
            .lock()
            .await
            .update_auth(test_auth("appkey-1", "secret-1", 10))
            .await;

        let token = generate_connection_token("appkey-1", "secret-1");
        let uri = format!(
            "/console/website?appkey=appkey-1&token={}&ts={}&nonce={}",
            token.token, token.ts, token.nonce
        );

        let response = website_router()
            .oneshot(Request::builder().uri(&uri).body(Body::empty()).unwrap())
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::UNAUTHORIZED);
    }

    #[tokio::test]
    async fn website_rejects_invalid_token() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = true;
        gAuthManager
            .lock()
            .await
            .update_auth(test_auth("appkey-1", "secret-1", 10))
            .await;

        let response = website_router()
            .oneshot(
                Request::builder()
                    .uri("/console/website?appkey=appkey-1&token=bad&ts=1&nonce=n")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::UNAUTHORIZED);
    }

    #[tokio::test]
    async fn client_bypassed_when_force_authorize_false() {
        let _guard = TOKEN_TEST_LOCK.lock().unwrap();
        crate::gConsoleSettings.lock().await.force_authorize = false;
        // No authorization loaded, no appkey/token params: must still pass.
        gAuthManager
            .lock()
            .await
            .update_auth(Default::default())
            .await;

        let response = client_router()
            .oneshot(
                Request::builder()
                    .uri("/console/client")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        crate::gConsoleSettings.lock().await.force_authorize = true;
    }
}
