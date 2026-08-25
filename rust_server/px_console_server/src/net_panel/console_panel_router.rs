use crate::console_context::ConsoleContext;
use crate::net_panel::console_panel_handler::{
    handle_query_all_panel_conn, handle_query_online_panel_count, handle_query_panel_conn_by_id,
};
use crate::user::session_router::require_admin;
use axum::routing::get;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_panel_router(
    context: Arc<Mutex<ConsoleContext>>,
) -> Router<Arc<Mutex<ConsoleContext>>> {
    Router::new()
        .route(
            "/query/panel/conn/by/device/id",
            get(handle_query_panel_conn_by_id).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/query/all/panel/conn",
            get(handle_query_all_panel_conn).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/query/online/panel/count",
            get(handle_query_online_panel_count).layer(middleware::from_fn(require_admin)),
        )
        .with_state(context)
}
