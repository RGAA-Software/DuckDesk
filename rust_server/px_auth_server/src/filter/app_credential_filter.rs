//! 开放接口（device/pull、gopico/verify/online、gopico/report）的接入凭据校验。
//! settings.require_app_credential 为 false 时直接放行（灰度切换用）。

use crate::app_credential as cred;
use crate::author_api_error::AuthorApiError;
use crate::gAuthorSettings;
use axum::body::Body;
use axum::http::Request;
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use futures_util::StreamExt;

pub async fn filter(req: Request<Body>, next: Next) -> Response {
    let (require, appkey, app_secret) = {
        let st = gAuthorSettings.lock().await;
        (
            st.require_app_credential,
            st.app_credential.as_ref().map(|c| c.appkey.clone()),
            st.app_credential.as_ref().map(|c| c.app_secret.clone()),
        )
    };
    if !require {
        return next.run(req).await;
    }
    let (Some(appkey), Some(app_secret)) = (appkey, app_secret) else {
        tracing::error!("require_app_credential is true but app_credential is not configured");
        return AuthorApiError::InvalidAppCredential.into_response();
    };

    let (parts, body) = req.into_parts();
    let headers = &parts.headers;
    let key = headers
        .get(cred::HEADER_APP_KEY)
        .and_then(|v| v.to_str().ok())
        .map(str::to_string);
    let ts = headers
        .get(cred::HEADER_APP_TIMESTAMP)
        .and_then(|v| v.to_str().ok())
        .and_then(|s| s.trim().parse::<i64>().ok());
    let sign_hex = headers
        .get(cred::HEADER_APP_SIGN)
        .and_then(|v| v.to_str().ok())
        .map(str::to_string);
    let (Some(key), Some(ts), Some(sign_hex)) = (key, ts, sign_hex) else {
        return AuthorApiError::InvalidAppCredential.into_response();
    };
    if key != appkey {
        return AuthorApiError::InvalidAppCredential.into_response();
    }

    let mut bytes = Vec::new();
    let mut stream = body.into_data_stream();
    while let Some(chunk) = stream.next().await {
        let Ok(chunk) = chunk else {
            return AuthorApiError::InvalidParams.into_response();
        };
        bytes.extend_from_slice(&chunk);
    }

    let now_ms = px_base::get_current_timestamp();
    if !cred::verify(&appkey, &app_secret, ts, &bytes, &sign_hex, now_ms) {
        return AuthorApiError::InvalidAppCredential.into_response();
    }

    next.run(Request::from_parts(parts, Body::from(bytes))).await
}
