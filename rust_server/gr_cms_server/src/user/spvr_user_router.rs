use crate::filter::spvr_appkey_filter;
use crate::spvr_context::SpvrContext;
use crate::user::spvr_user_handler::{
    count_users, handle_active_user, handle_batch_generate_csv_users,
    handle_batch_generate_random_users, handle_delete_user, handle_login, handle_logout,
    handle_register_user, handle_update_avatar, handle_update_password, handle_update_user,
    query_user_by_id, query_user_by_name, query_users,
};
use axum::routing::{get, post};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_user_router(context: Arc<Mutex<SpvrContext>>) -> Router<Arc<Mutex<SpvrContext>>> {
    Router::new()
        .route(
            "/register",
            post(handle_register_user).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/login",
            post(handle_login).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/logout",
            post(handle_logout).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/delete",
            post(handle_delete_user).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/active",
            post(handle_active_user).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/update",
            post(handle_update_user).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/update/avatar",
            post(handle_update_avatar).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/update/password",
            post(handle_update_password).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/query/user/by/id",
            get(query_user_by_id).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/query/user/by/name",
            get(query_user_by_name).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/query/users",
            get(query_users).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/count/users",
            get(count_users).layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/batch/generate/random/users",
            post(handle_batch_generate_random_users)
                .layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .route(
            "/batch/generate/csv/users",
            post(handle_batch_generate_csv_users)
                .layer(middleware::from_fn(spvr_appkey_filter::filter)),
        )
        .with_state(context)
}
