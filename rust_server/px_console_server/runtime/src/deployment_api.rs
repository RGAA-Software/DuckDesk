use crate::{
    error::ApiError,
    request::{self, Input, Page, Params as Query, Route as Path},
    StateData,
};
use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    routing::{get, patch},
    Json, Router,
};
use px_console_store::DeploymentConfiguration;
use serde::Deserialize;
use serde_json::{json, Value};
use std::sync::Arc;
use uuid::Uuid;
pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route(
            "/api/console/managed/deployments",
            get(managed).post(create),
        )
        .route("/api/console/managed/deployments/{id}", patch(configure))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NewDeployment {
    application_id: Uuid,
    node_id: Uuid,
    configuration: DeploymentConfiguration,
}
async fn create(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(input): Input<NewDeployment>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    Ok((
        StatusCode::CREATED,
        Json(json!(
            state
                .db
                .deployments()
                .create(
                    &request::administrator(&state, &headers)?,
                    input.application_id,
                    input.node_id,
                    &input.configuration
                )
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
            .deployments()
            .list_managed(
                &request::administrator(&state, &headers)?,
                page.after,
                page.limit
            )
            .await?
    )))
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeploymentChange {
    revision: i64,
    configuration: DeploymentConfiguration,
}
async fn configure(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<DeploymentChange>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .deployments()
            .configure(
                &request::administrator(&state, &headers)?,
                id,
                input.revision,
                &input.configuration
            )
            .await?
    )))
}
