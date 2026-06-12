use std::sync::Arc;
use axum::{middleware, Router};
use axum::routing::{get, post};
use tokio::sync::Mutex;
use tower_http::limit::RequestBodyLimitLayer;
use crate::spvr_context::SpvrContext;
use crate::event::spvr_event_handler::{handle_add_event, handle_add_log, handle_count_events, handle_query_events, handle_remove_event};
use crate::filter::spvr_appkey_filter;

pub fn make_event_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        .route("/add",
            post(handle_add_event)
                .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/remove",
           post(handle_remove_event)
               .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/query",
           get(handle_query_events)
               .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/count/events",
               get(handle_count_events)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/add/log",
           post(handle_add_log)
               .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .with_state(context)
}