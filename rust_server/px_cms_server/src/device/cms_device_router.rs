use crate::cms_context::CmsContext;
use crate::device::cms_device_handler::{
    append_used_time, handle_count_devices, handle_create_new_device, handle_query_devices,
    handle_query_total_used_time, query_device_by_id, update_desktop_link, update_device_active,
    update_device_name, update_random_password, update_safety_password, verify_device_info,
};
use crate::filter::cms_appkey_filter;
use crate::filter::cms_device_filter::filter as cms_device_id_filter;
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_device_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/create/new/device",
            post(handle_create_new_device).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/verify/device/info",
            get(verify_device_info)
                .layer(middleware::from_fn(cms_device_id_filter))
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/devices",
            get(handle_query_devices).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/count/devices",
            get(handle_count_devices).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/device/by/id",
            get(query_device_by_id)
                .route_layer(middleware::from_fn(cms_device_id_filter))
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/append/used/time",
            post(append_used_time)
                .route_layer(middleware::from_fn(cms_device_id_filter))
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/update/random/pwd",
            post(update_random_password)
                .route_layer(middleware::from_fn(cms_device_id_filter))
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/update/safety/pwd",
            post(update_safety_password)
                .route_layer(middleware::from_fn(cms_device_id_filter))
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/update/desktop/link",
            post(update_desktop_link).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/update/device/name",
            post(update_device_name).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/update/device/active",
            post(update_device_active).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/total/used/time",
            get(handle_query_total_used_time).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
