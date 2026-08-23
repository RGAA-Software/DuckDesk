use crate::console_context::ConsoleContext;
use crate::filter::console_appkey_filter;
use crate::rtc::handler::{
    get_admin_config, get_node_config, get_turn_status, update_admin_config,
    validate_admin_config,
};
use crate::user::session_router::{require_admin, require_admin_write};
use axum::routing::{get, post, put};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_admin_rtc_router(
    context: Arc<Mutex<ConsoleContext>>,
) -> Router<Arc<Mutex<ConsoleContext>>> {
    Router::new()
        .route(
            "/rtc/ice-config",
            get(get_admin_config).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/rtc/ice-config",
            put(update_admin_config).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/rtc/ice-config/test",
            post(validate_admin_config).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/rtc/turn-status",
            get(get_turn_status).layer(middleware::from_fn(require_admin)),
        )
        .with_state(context)
}

pub fn make_node_rtc_router(
    context: Arc<Mutex<ConsoleContext>>,
) -> Router<Arc<Mutex<ConsoleContext>>> {
    Router::new()
        .route(
            "/ice-config",
            get(get_node_config).layer(middleware::from_fn(console_appkey_filter::filter)),
        )
        .with_state(context)
}
