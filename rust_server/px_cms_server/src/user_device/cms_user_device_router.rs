use crate::cms_context::CmsContext;
use crate::filter::cms_appkey_filter;
use crate::user_device::cms_user_device_handler::{
    handle_add_device_for_user, handle_query_user_devices, handle_remove_device_from_user,
};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_user_device_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/add/device/for/user",
            post(handle_add_device_for_user).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/remove/device/from/user",
            post(handle_remove_device_from_user)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/user/devices",
            get(handle_query_user_devices).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
