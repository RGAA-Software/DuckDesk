use std::sync::Arc;
use axum::{middleware, Router};
use axum::routing::{get, post};
use tokio::sync::Mutex;
use crate::filter::spvr_appkey_filter;
use crate::spvr_context::SpvrContext;
use crate::update::update_handle::{handle_download_install_package, handle_hello_world, handle_query_update_info, handle_upload_update_info};

pub fn make_update_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        .route("/hello", 
               get(handle_hello_world)
        )
        
        .route("/upload_update_info", 
               post(handle_upload_update_info)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )
        
        .route("/query_update_info", 
               get(handle_query_update_info)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )
        
        .route("/download", 
               get(handle_download_install_package)
                   .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )
        
        .with_state(context)
}