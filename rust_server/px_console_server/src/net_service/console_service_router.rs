use crate::console_context::ConsoleContext;
use crate::net_service::console_service_handler::{
    handle_query_all_service_conn, handle_query_online_service_count,
};
use crate::user::session_router::require_admin;
use axum::routing::get;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_service_router(context: Arc<Mutex<ConsoleContext>>) -> Router<Arc<Mutex<ConsoleContext>>> {
    Router::new()
        .route(
            "/query/all/service/conn",
            get(handle_query_all_service_conn).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/query/online/service/count",
            get(handle_query_online_service_count).layer(middleware::from_fn(require_admin)),
        )
        .with_state(context)
}
