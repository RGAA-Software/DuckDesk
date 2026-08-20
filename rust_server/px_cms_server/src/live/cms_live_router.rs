use crate::cms_context::CmsContext;
use crate::live::cms_live_handler::{handle_live_flv, handle_live_play, handle_live_status};
use crate::user::session_router::require_admin;
use axum::routing::get;
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_live_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        // Discovery is an administrator operation. Playback subresources use
        // the short-lived stream ticket issued by the status handler.
        .route(
            "/status",
            get(handle_live_status).layer(middleware::from_fn(require_admin)),
        )
        .route("/play/{stream_id}/flv", get(handle_live_flv))
        // HLS subresources cannot carry the appkey safely. They are instead
        // protected by the short-lived, stream-scoped ticket issued by status.
        .route("/play/{stream_id}/{*asset}", get(handle_live_play))
        .with_state(context)
}
