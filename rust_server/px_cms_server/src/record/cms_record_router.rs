use crate::filter::cms_appkey_filter;
use crate::record::cms_record_handle::{
    handle_hello_world, handle_query_file_transfer_info, handle_query_update_info,
    handle_update_file_transfer_info, handle_update_visit_info, handle_upload_file_transfer_info,
    handle_upload_visit_info,
};
use crate::record::cms_render_record_handle::{
    handle_record_access, handle_record_delete, handle_record_download, handle_record_fetch,
    handle_record_list, handle_record_ticket, handle_record_upload,
};
use crate::cms_context::CmsContext;
use axum::routing::{delete, get, post};
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
        // ---- render records view (design doc 6.3 / 6.4) ----
        .route(
            "/access",
            get(handle_record_access).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/ticket",
            get(handle_record_ticket).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/list",
            get(handle_record_list).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/fetch",
            get(handle_record_fetch).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/upload",
            post(handle_record_upload).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/download",
            post(handle_record_download).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/{id}",
            delete(handle_record_delete).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
