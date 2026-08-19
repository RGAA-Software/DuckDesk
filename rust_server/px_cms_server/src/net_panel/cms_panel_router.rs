use crate::cms_context::CmsContext;
use crate::filter::cms_appkey_filter;
use crate::net_panel::cms_panel_handler::{
    handle_query_all_panel_conn, handle_query_online_panel_count, handle_query_panel_conn_by_id,
};
use axum::routing::get;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_panel_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/query/panel/conn/by/device/id",
            get(handle_query_panel_conn_by_id)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/all/panel/conn",
            get(handle_query_all_panel_conn).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/online/panel/count",
            get(handle_query_online_panel_count)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
