use crate::filter::cms_appkey_filter;
use crate::cms_context::CmsContext;
use crate::cms_handler::{
    clear_cached_data, gen_access_info, gen_cached_data_size, gen_raw_access_info,
    get_servers_config, handle_get_machine_code, query_alive_connections_count,
    query_available_new_connection,
};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_cms_router(context: Arc<Mutex<CmsContext>>) -> Router<Arc<Mutex<CmsContext>>> {
    Router::new()
        .route("/servers/config", get(get_servers_config))
        .route("/gen/access/info", get(gen_access_info))
        .route("/gen/raw/access/info", get(gen_raw_access_info))
        .route("/get/machine/code", get(handle_get_machine_code))
        .route(
            "/get/cache/data/size",
            get(gen_cached_data_size).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/clear/cache/data",
            post(clear_cached_data).layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/query/alive/connections",
            get(query_alive_connections_count)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .route(
            "/available/new/connection",
            get(query_available_new_connection)
                .layer(middleware::from_fn(cms_appkey_filter::filter)),
        )
        .with_state(context)
}
