use crate::cms_context::CmsContext;
use crate::filter::cms_appkey_filter;
use crate::stream::cms_stream_handler::{
    handle_delete_stream, handle_insert_stream, handle_query_stream_by_id,
    handle_query_stream_by_name, handle_query_streams, handle_update_stream,
};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_stream_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/insert",
            post(handle_insert_stream).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/delete",
            post(handle_delete_stream).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/update",
            post(handle_update_stream).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/by/id",
            get(handle_query_stream_by_id).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/by/name",
            get(handle_query_stream_by_name).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/streams",
            get(handle_query_streams).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
