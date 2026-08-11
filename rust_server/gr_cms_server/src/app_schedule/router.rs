use crate::app_schedule::handler::{
    handle_create_application, handle_create_placement, handle_delete_app, handle_delete_node,
    handle_list_app_rows, handle_list_applications, handle_list_instances, handle_list_nodes,
    handle_list_placements, handle_next_port, handle_save_app, handle_save_node,
    handle_start_instance, handle_start_node, handle_stop_instance,
};
use crate::filter::spvr_appkey_filter;
use crate::spvr_context::SpvrContext;
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_app_schedule_router(
    context: Arc<Mutex<SpvrContext>>,
) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        .route(
            "/app/create",
            post(handle_create_application).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/list",
            get(handle_list_applications).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/rows",
            get(handle_list_app_rows).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/save",
            post(handle_save_app).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/delete/{app_id}",
            post(handle_delete_app).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/next-port",
            get(handle_next_port).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/node/save",
            post(handle_save_node).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/node/delete/{node_id}",
            post(handle_delete_node).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/node/list",
            get(handle_list_nodes).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/node/start/{node_id}",
            post(handle_start_node).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/placement/create",
            post(handle_create_placement).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/placement/list",
            get(handle_list_placements).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/instance/start",
            post(handle_start_instance).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/instance/stop/{instance_id}",
            post(handle_stop_instance).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/app/instance/list",
            get(handle_list_instances).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .with_state(context)
}
