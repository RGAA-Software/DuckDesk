use crate::console_context::ConsoleContext;
use crate::event::console_event_handler::{
    handle_add_event, handle_add_log, handle_count_events, handle_query_events, handle_remove_event,
};
use crate::filter::console_appkey_filter;
use crate::user::session_router::{require_admin, require_admin_write};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_event_router(
    context: Arc<Mutex<ConsoleContext>>,
) -> Router<Arc<Mutex<ConsoleContext>>> {
    Router::new()
        .route(
            "/add",
            post(handle_add_event).layer(middleware::from_fn(console_appkey_filter::filter)),
        )
        .route(
            "/remove",
            post(handle_remove_event).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/query",
            get(handle_query_events).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/count/events",
            get(handle_count_events).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/add/log",
            post(handle_add_log).layer(middleware::from_fn(console_appkey_filter::filter)),
        )
        .with_state(context)
}
