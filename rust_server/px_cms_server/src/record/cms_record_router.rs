use crate::filter::cms_appkey_filter;
use crate::record::cms_record_handle::{
    handle_hello_world, handle_query_file_transfer_info, handle_query_update_info,
    handle_update_file_transfer_info, handle_update_visit_info, handle_upload_file_transfer_info,
    handle_upload_visit_info,
};
use crate::cms_context::CmsContext;
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_record_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/hello",
            get(handle_hello_world).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/upload_visit_info",
            post(handle_upload_visit_info).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/update_visit_info",
            post(handle_update_visit_info).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query_visit_info",
            get(handle_query_update_info).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/upload_file_transfer_info",
            post(handle_upload_file_transfer_info)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/update_file_transfer_info",
            post(handle_update_file_transfer_info)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query_file_transfer_info",
            get(handle_query_file_transfer_info)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
