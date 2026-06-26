use crate::auth::spvr_auth_handler::{
    handle_auth_valid, handle_get_authorization, handle_get_used_time, handle_update_auth_password,
    handle_update_authorization, handle_verify_auth_account,
};
use crate::filter::spvr_appkey_filter;
use crate::spvr_context::SpvrContext;
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_auth_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    let appkey_filter = middleware::from_fn(spvr_appkey_filter::filter);

    Router::new()
        .route(
            "/update/authorization",
            post(handle_update_authorization).layer(appkey_filter.clone()),
        )
        .route(
            "/get/authorization",
            get(handle_get_authorization).layer(appkey_filter.clone()),
        )
        .route(
            "/get/used/time",
            get(handle_get_used_time).layer(appkey_filter.clone()),
        )
        .route(
            "/update/password",
            post(handle_update_auth_password).layer(appkey_filter.clone()),
        )
        .route(
            "/verify/auth/account",
            post(handle_verify_auth_account).layer(appkey_filter.clone()),
        )
        .route(
            "/auth/valid",
            get(handle_auth_valid).layer(appkey_filter.clone()),
        )
        .with_state(context)
}
