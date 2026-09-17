use crate::{
    error::ApiError,
    request::{self, Input, Page, Params as Query, Revision, Route as Path},
    StateData,
};
use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    routing::{get, patch},
    Json, Router,
};
use px_console_store::ApplicationSpec;
use serde::Deserialize;
use serde_json::{json, Value};
use std::sync::Arc;
use uuid::Uuid;
pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route(
            "/api/console/managed/applications",
            get(managed).post(create),
        )
        .route(
            "/api/console/managed/applications/{id}",
            patch(update).delete(remove),
        )
        .route(
            "/api/console/managed/applications/{id}/groups",
            get(groups).put(replace_groups),
        )
        .route("/api/console/applications", get(visible))
        .route("/api/console/applications/{id}", get(application))
}
async fn create(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(spec): Input<ApplicationSpec>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    Ok((
        StatusCode::CREATED,
        Json(json!(
            state
                .db
                .applications()
                .create(&request::administrator(&state, &headers)?, &spec)
                .await?
        )),
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
            .applications()
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
            .applications()
            .list_visible(&token, client, page.after, page.limit)
            .await?
    )))
}
async fn application(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<Value>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(json!(
        state
            .db
            .applications()
            .get_visible(&token, client, id)
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct AppChange {
    revision: i64,
    spec: ApplicationSpec,
}
async fn update(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<AppChange>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .applications()
            .update(
                &request::administrator(&state, &headers)?,
                id,
                input.revision,
                &input.spec
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
        .applications()
        .delete(
            &request::administrator(&state, &headers)?,
            id,
            input.revision,
        )
        .await?;
    Ok(StatusCode::NO_CONTENT)
}
async fn groups(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .applications()
            .groups(&request::administrator(&state, &headers)?, id)
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct AppGroups {
    revision: i64,
    groups: Vec<Uuid>,
}
async fn replace_groups(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<AppGroups>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .applications()
            .replace_groups(
                &request::administrator(&state, &headers)?,
                id,
                input.revision,
                &input.groups
            )
            .await?
    )))
}
