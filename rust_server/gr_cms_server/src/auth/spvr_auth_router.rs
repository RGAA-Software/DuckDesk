use crate::auth::spvr_auth_handler::{handle_auth_valid, handle_get_authorization, handle_get_used_time, handle_update_auth_password, handle_update_authorization, handle_verify_auth_account};
use crate::spvr_context::SpvrContext;
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;
use crate::filter::spvr_appkey_filter;

pub fn make_auth_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        .route("/update/authorization",
            post(handle_update_authorization)
        )

        .route("/get/authorization",
            get(handle_get_authorization)
        )

        .route("/get/used/time",
            get(handle_get_used_time)
        )

        .route("/update/password",
            post(handle_update_auth_password)
                .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/verify/auth/account",
            post(handle_verify_auth_account)
                .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/auth/valid",
            get(handle_auth_valid)
                .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .with_state(context)
}