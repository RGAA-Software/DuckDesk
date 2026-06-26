use crate::filter::spvr_appkey_filter;
use crate::net_client::spvr_client_handler::{
    handle_count_alive_conns, handle_query_alive_conns, handle_query_client_conns,
    handle_query_conns,
};
use crate::spvr_context::SpvrContext;
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_client_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        // connection history about specific client
        // use : device_id
        .route(
            "/query/client/conns",
            get(handle_query_client_conns).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        // connection history about all clients
        .route(
            "/query/conns",
            get(handle_query_conns).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        // total alive connections
        // NOT in DB, just connecting ...
        .route(
            "/query/alive/conns",
            get(handle_query_alive_conns).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        // total alive connections COUNT
        // NOT in DB, just connecting ...
        .route(
            "/count/alive/conns",
            get(handle_count_alive_conns).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .with_state(context)
}
