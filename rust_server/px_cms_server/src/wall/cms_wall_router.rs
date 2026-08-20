use crate::cms_context::CmsContext;
use crate::filter::cms_appkey_filter;
use crate::wall::cms_wall_handler::create_wall_session;
use axum::routing::post;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_wall_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route(
            "/session",
            post(create_wall_session).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
