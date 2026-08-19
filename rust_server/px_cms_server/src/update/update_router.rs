use crate::cms_context::CmsContext;
use crate::filter::cms_appkey_filter;
use crate::update::update_handle::{
    handle_download_install_package, handle_hello_world, handle_query_update_info,
    handle_upload_update_info,
};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_update_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route("/hello", get(handle_hello_world))
        .route(
            "/upload_update_info",
            post(handle_upload_update_info).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query_update_info",
            get(handle_query_update_info).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/download",
            get(handle_download_install_package)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
