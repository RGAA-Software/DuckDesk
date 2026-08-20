use crate::cms_context::CmsContext;
use crate::user::session_router::require_admin_write;
use crate::wall::cms_wall_handler::create_wall_session;
use axum::routing::post;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_wall_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/session",
            post(create_wall_session).layer(middleware::from_fn(require_admin_write)),
        )
        .with_state(context)
}
