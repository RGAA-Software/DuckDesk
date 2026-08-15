use crate::filter::cms_appkey_filter;
use crate::net_service::cms_service_handler::{
    handle_query_all_service_conn, handle_query_online_service_count,
};
use crate::cms_context::CmsContext;
use axum::routing::get;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_service_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/query/all/service/conn",
            get(handle_query_all_service_conn)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/online/service/count",
            get(handle_query_online_service_count)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
