use crate::event::cms_event_handler::{
    handle_add_event, handle_add_log, handle_count_events, handle_query_events, handle_remove_event,
};
use crate::filter::cms_appkey_filter;
use crate::cms_context::CmsContext;
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_event_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/add",
            post(handle_add_event).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/remove",
            post(handle_remove_event).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query",
            get(handle_query_events).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/count/events",
            get(handle_count_events).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/add/log",
            post(handle_add_log).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
