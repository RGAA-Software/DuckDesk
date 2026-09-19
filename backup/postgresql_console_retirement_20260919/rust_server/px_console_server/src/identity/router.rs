use crate::console_context::ConsoleContext;
use crate::identity::catalog_handler::{list_app_catalog, list_device_catalog, update_app_access};
use crate::identity::handler::{
    create_group, delete_group, list_app_ids, list_device_ids, list_groups, list_member_ids,
    replace_apps, replace_devices, replace_members, update_group,
};
use crate::identity::user_handler::{
    batch_create_users_csv, block_guest_session, create_user, delete_user, list_guest_sessions,
    list_personal_devices, list_user_sessions, list_users, replace_personal_devices,
    reset_password, revoke_all_sessions, update_user, view_password,
};
use crate::user::session_router::{require_admin, require_admin_write};
use axum::routing::{delete, get, patch, post, put};
use axum::{middleware, Router};
use std::sync::Arc;
use tokio::sync::Mutex;

pub fn make_admin_identity_router(
    context: Arc<Mutex<ConsoleContext>>,
) -> Router<Arc<Mutex<ConsoleContext>>> {
    Router::new()
        .route(
            "/groups",
            get(list_groups).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/groups",
            post(create_group).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/groups/{gid}",
            patch(update_group).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/groups/{gid}",
            delete(delete_group).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/groups/{gid}/members",
            get(list_member_ids).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/groups/{gid}/members",
            put(replace_members).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/groups/{gid}/devices",
            get(list_device_ids).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/groups/{gid}/devices",
            put(replace_devices).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/groups/{gid}/apps",
            get(list_app_ids).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/groups/{gid}/apps",
            put(replace_apps).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/users",
            get(list_users).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/users",
            post(create_user).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/users/batch.csv",
            post(batch_create_users_csv).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/users/{uid}",
            patch(update_user).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/users/{uid}",
            delete(delete_user).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/users/{uid}/password/reset",
            post(reset_password).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/users/{uid}/password",
            get(view_password).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/users/{uid}/sessions/revoke-all",
            post(revoke_all_sessions).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/users/{uid}/sessions",
            get(list_user_sessions).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/guest-sessions",
            get(list_guest_sessions).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/guest-sessions/{sid}/block",
            post(block_guest_session).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/users/{uid}/devices",
            get(list_personal_devices).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/users/{uid}/devices",
            put(replace_personal_devices).layer(middleware::from_fn(require_admin_write)),
        )
        .route(
            "/catalog/devices",
            get(list_device_catalog).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/catalog/apps",
            get(list_app_catalog).layer(middleware::from_fn(require_admin)),
        )
        .route(
            "/apps/{app_id}/access",
            patch(update_app_access).layer(middleware::from_fn(require_admin_write)),
        )
        .with_state(context)
}
