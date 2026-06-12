use std::sync::Arc;
use axum::{middleware, Router};
use axum::routing::{get, post};
use tokio::sync::Mutex;
use crate::filter::spvr_appkey_filter;
use crate::spvr_context::SpvrContext;
use crate::record::spvr_record_handle::{handle_upload_visit_info, handle_hello_world, handle_query_update_info, handle_upload_file_transfer_info, handle_query_file_transfer_info, handle_update_visit_info, handle_update_file_transfer_info};

pub fn make_record_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        .route("/hello",
               get(handle_hello_world)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/upload_visit_info",
               post(handle_upload_visit_info)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/update_visit_info",
               post(handle_update_visit_info)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/query_visit_info",
               get(handle_query_update_info)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/upload_file_transfer_info",
               post(handle_upload_file_transfer_info)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/update_file_transfer_info",
               post(handle_update_file_transfer_info)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/query_file_transfer_info",
               get(handle_query_file_transfer_info)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .with_state(context)
}