use crate::{
    auth::authorize,
    error::ApiError,
    model::{Kind, Mark, Page, Release, ReleaseInput, ReleaseQuery, Submission},
    store, AppState, MIGRATIONS,
};
use axum::{
    extract::{Path, Query, State},
    http::{HeaderMap, StatusCode},
    Json,
};
use serde_json::{json, Value};
use std::sync::Arc;
use uuid::Uuid;

pub async fn ready(State(state): State<Arc<AppState>>) -> Result<StatusCode, ApiError> {
    px_pg::runtime_readiness(
        &state.pool,
        px_pg::Service::Desk,
        state.deployment,
        &MIGRATIONS,
    )
    .await?;
    Ok(StatusCode::NO_CONTENT)
}
pub async fn create_consult(
    State(state): State<Arc<AppState>>,
    Json(input): Json<Submission>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(
        json!({"id": store::submit(&state.pool, Kind::Consult, input).await?}),
    ))
}
pub async fn create_issue(
    State(state): State<Arc<AppState>>,
    Json(input): Json<Submission>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(
        json!({"id": store::submit(&state.pool, Kind::Issue, input).await?}),
    ))
}
pub async fn consults(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    authorize(&state, &headers).await?;
    Ok(Json(
        json!({"items": store::list(&state.pool, Kind::Consult, page).await?}),
    ))
}
pub async fn issues(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    authorize(&state, &headers).await?;
    Ok(Json(
        json!({"items": store::list(&state.pool, Kind::Issue, page).await?}),
    ))
}
pub async fn mark_consult(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Json(input): Json<Mark>,
) -> Result<Json<crate::model::Feedback>, ApiError> {
    authorize(&state, &headers).await?;
    Ok(Json(
        store::mark(&state.pool, Kind::Consult, id, input).await?,
    ))
}
pub async fn mark_issue(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Json(input): Json<Mark>,
) -> Result<Json<crate::model::Feedback>, ApiError> {
    authorize(&state, &headers).await?;
    Ok(Json(
        store::mark(&state.pool, Kind::Issue, id, input).await?,
    ))
}
pub async fn version(
    State(state): State<Arc<AppState>>,
    Query(input): Query<ReleaseQuery>,
) -> Result<Json<Release>, ApiError> {
    Ok(Json(store::latest(&state.pool, input).await?))
}
pub async fn publish(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Json(input): Json<ReleaseInput>,
) -> Result<Json<Release>, ApiError> {
    authorize(&state, &headers).await?;
    Ok(Json(store::publish(&state.pool, input).await?))
}
