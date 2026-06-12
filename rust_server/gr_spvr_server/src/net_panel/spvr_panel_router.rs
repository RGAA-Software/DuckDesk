use std::sync::Arc;
use axum::{middleware, Router};
use axum::routing::{get, post};
use tokio::sync::Mutex;
use crate::filter::spvr_appkey_filter;
use crate::net_panel::spvr_panel_handler::{handle_query_all_panel_conn, handle_query_panel_conn_by_id, handle_query_online_panel_count};
use crate::spvr_context::SpvrContext;

pub fn make_panel_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        .route("/query/panel/conn/by/device/id",
           get(handle_query_panel_conn_by_id)
               .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/query/all/panel/conn",
           get(handle_query_all_panel_conn)
               .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )

        .route("/query/online/panel/count",
           get(handle_query_online_panel_count)
               .layer(middleware::from_fn(spvr_appkey_filter::filter))
        )
        
        .with_state(context)
}
