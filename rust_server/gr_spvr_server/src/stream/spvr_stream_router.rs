use std::sync::Arc;
use axum::{middleware, Router};
use axum::routing::{get, post};
use tokio::sync::Mutex;
use crate::filter::spvr_appkey_filter;
use crate::spvr_context::SpvrContext;
use crate::stream::spvr_stream_handler::{handle_delete_stream, handle_insert_stream, handle_query_stream_by_id, handle_query_stream_by_name, handle_query_streams, handle_update_stream};

pub fn make_stream_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        .route("/insert",
               post(handle_insert_stream)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/delete",
               post(handle_delete_stream)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/update",
               post(handle_update_stream)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/query/by/id",
               get(handle_query_stream_by_id)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/query/by/name",
               get(handle_query_stream_by_name)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/query/streams",
               get(handle_query_streams)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .with_state(context)
}
