use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::gUserSessionManager;
use crate::user::session_handler::{change_password, login, logout, me};
use axum::body::Body;
use axum::http::{header, Request};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn require_user(mut request: Request<Body>, next: Next) -> Response {
    let token = request
        .headers()
        .get(header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
        .map(str::trim)
        .filter(|value| !value.is_empty());
    let Some(token) = token else {
        return CmsApiError::AuthenticationRequired.into_response();
    };
    match gUserSessionManager.authenticate(token).await {
        Ok(subject) => {
            request.extensions_mut().insert(subject);
            next.run(request).await
        }
        Err(error) => error.into_response(),
    }
}

pub fn make_session_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route("/user/login", post(login))
        .route("/user/logout", post(logout))
        .with_state(context)
}

pub fn make_user_self_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route("/me", get(me).layer(middleware::from_fn(require_user)))
        .route(
            "/me/password",
            post(change_password).layer(middleware::from_fn(require_user)),
        )
        .with_state(context)
}
