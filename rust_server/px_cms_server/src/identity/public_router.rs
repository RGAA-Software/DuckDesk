use crate::cms_context::CmsContext;
use crate::connection_ticket::handler::issue_guest_instance_ticket;
use crate::identity::resource_handler::{
    list_guest_instances, list_public_apps, start_public_app, stop_guest_instance,
};
use crate::user::session_router::{require_guest, require_guest_write};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_public_resource_router(
    context: Arc<Mutex<CmsContext>>,
) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/apps",
            get(list_public_apps),
        )
        .route(
            "/apps/{app_id}/start",
            post(start_public_app).layer(middleware::from_fn(require_guest_write)),
        )
        .route(
            "/instances",
            get(list_guest_instances).layer(middleware::from_fn(require_guest)),
        )
        .route(
            "/instances/{instance_id}/ticket",
            post(issue_guest_instance_ticket).layer(middleware::from_fn(require_guest_write)),
        )
        .route(
            "/instances/{instance_id}/stop",
            post(stop_guest_instance).layer(middleware::from_fn(require_guest_write)),
        )
        .with_state(context)
}
