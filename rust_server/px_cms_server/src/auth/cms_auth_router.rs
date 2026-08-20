use crate::auth::cms_auth_handler::{
    handle_auth_valid, handle_get_auth_status, handle_get_authorization, handle_get_used_time,
    handle_pull_authorization, handle_update_auth_password,
};
use crate::cms_context::CmsContext;
use crate::user::session_router::{require_admin, require_admin_write};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_auth_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route("/pull/authorization", post(handle_pull_authorization))
        .route("/get/auth/status", get(handle_get_auth_status))
        .route(
            "/get/authorization",
            get(handle_get_authorization).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/get/used/time",
            get(handle_get_used_time).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/update/password",
            post(handle_update_auth_password).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/auth/valid",
            get(handle_auth_valid).layer(middleware::from_fn(require_admin)),
        )
        .with_state(context)
}
