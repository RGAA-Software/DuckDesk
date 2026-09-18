use crate::console_context::ConsoleContext;
use crate::user::session_router::require_admin_write;
use crate::wall::console_wall_handler::create_wall_session;
use axum::routing::post;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_wall_router(context: Arc<Mutex<ConsoleContext>>) -> Router<Arc<Mutex<ConsoleContext>>> {
    Router::new()
        .route(
            "/session",
            post(create_wall_session).layer(middleware::from_fn(require_admin_write)),
        )
        .with_state(context)
}
