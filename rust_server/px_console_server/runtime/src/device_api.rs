use crate::{
    error::ApiError,
    request::{self, Input, Page, Params as Query, Revision, Route as Path},
    StateData,
};
use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    routing::{get, patch, post},
    Json, Router,
};
use px_console_store::{DeviceAccess, DevicePlatform};
use serde::Deserialize;
use serde_json::{json, Value};
use std::sync::Arc;
use uuid::Uuid;

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/managed/devices", get(managed).post(create))
        .route(
            "/api/console/managed/devices/{id}",
            patch(update).delete(remove),
        )
        .route(
            "/api/console/managed/devices/{id}/access",
            get(access).put(replace_access),
        )
        .route("/api/console/managed/devices/{id}/credential", post(rotate))
        .route("/api/console/devices", get(visible))
        .route("/api/console/devices/{id}", get(device))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NewDevice {
    name: String,
    platform: DevicePlatform,
}
async fn create(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(input): Input<NewDevice>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    let actor = request::administrator(&state, &headers)?;
    let (secret, digest) = request::mint();
    let device = state
        .db
        .devices()
        .create(&actor, &input.name, input.platform, &digest)
        .await?;
    Ok((
        StatusCode::CREATED,
        Json(json!({"device":device,"enrollment_token":secret.as_str()})),
    ))
}
async fn managed(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .devices()
            .list_managed(
                &request::administrator(&state, &headers)?,
                page.after,
                page.limit
            )
            .await?
    )))
}
async fn visible(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(json!(
        state
            .db
            .devices()
            .list_visible(&token, client, page.after, page.limit)
            .await?
    )))
}
async fn device(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<Value>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(json!(
        state.db.devices().get_visible(&token, client, id).await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeviceChange {
    revision: i64,
    name: String,
    disabled: bool,
}
async fn update(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<DeviceChange>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .devices()
            .update(
                &request::administrator(&state, &headers)?,
                id,
                input.revision,
                &input.name,
                input.disabled
            )
            .await?
    )))
}
async fn remove(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Query(input): Query<Revision>,
) -> Result<StatusCode, ApiError> {
    state
        .db
        .devices()
        .delete(
            &request::administrator(&state, &headers)?,
            id,
            input.revision,
        )
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
async fn rotate(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<Revision>,
) -> Result<Json<Value>, ApiError> {
    let actor = request::administrator(&state, &headers)?;
    let (secret, digest) = request::mint();
    let device = state
        .db
        .devices()
        .rotate_key(&actor, id, input.revision, &digest)
        .await?;
    Ok(Json(
        json!({"device":device,"enrollment_token":secret.as_str()}),
    ))
}
async fn access(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<Value>, ApiError> {
    let access = state
        .db
        .devices()
        .access(&request::administrator(&state, &headers)?, id)
        .await?;
    Ok(Json(json!({"users":access.users,"groups":access.groups})))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct AccessChange {
    revision: i64,
    users: Vec<Uuid>,
    groups: Vec<Uuid>,
}
async fn replace_access(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<AccessChange>,
) -> Result<Json<Value>, ApiError> {
    let access = DeviceAccess {
        users: input.users,
        groups: input.groups,
    };
    Ok(Json(json!(
        state
            .db
            .devices()
            .replace_access(
                &request::administrator(&state, &headers)?,
                id,
                input.revision,
                &access
            )
            .await?
    )))
}
