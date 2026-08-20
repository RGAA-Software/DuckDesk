use crate::cms_context::CmsContext;
use crate::net_client::cms_client_handler::{
    handle_count_alive_conns, handle_query_alive_conns, handle_query_client_conns,
    handle_query_conns,
};
use crate::user::session_router::require_admin;
use axum::routing::get;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_client_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        // connection history about specific client
        // use : device_id
        .route(
            "/query/client/conns",
            get(handle_query_client_conns).layer(middleware::from_fn(require_admin)),
        )
        // connection history about all clients
        .route(
            "/query/conns",
            get(handle_query_conns).layer(middleware::from_fn(require_admin)),
        )
        // total alive connections
        // NOT in DB, just connecting ...
        .route(
            "/query/alive/conns",
            get(handle_query_alive_conns).layer(middleware::from_fn(require_admin)),
        )
        // total alive connections COUNT
        // NOT in DB, just connecting ...
        .route(
            "/count/alive/conns",
            get(handle_count_alive_conns).layer(middleware::from_fn(require_admin)),
        )
        .with_state(context)
}
