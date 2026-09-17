use crate::{
    error::ApiError,
    request::{self, Input, Page, Params as Query, Revision, Route as Path},
    StateData,
};
use axum::{
    extract::State,
    http::{HeaderMap, StatusCode},
    routing::get,
    Json, Router,
};
use px_console_store::{CreateSavedConnection, SavedConnection, SavedConnectionSettings};
use serde::Deserialize;
use std::sync::Arc;
use uuid::Uuid;

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/saved-connections", get(list).post(create))
        .route(
            "/api/console/saved-connections/{id}",
            get(connection).patch(update).delete(remove),
        )
}

async fn create(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(input): Input<CreateSavedConnection>,
) -> Result<(StatusCode, Json<SavedConnection>), ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    let connection = state
        .db
        .saved_connections()
        .create(&token, client, &input)
        .await?;
    Ok((StatusCode::CREATED, Json(connection)))
}

async fn list(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Vec<SavedConnection>>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .saved_connections()
            .list(&token, client, page.after, page.limit)
            .await?,
    ))
}

async fn connection(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<SavedConnection>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(
        state.db.saved_connections().get(&token, client, id).await?,
    ))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct SavedConnectionChange {
    revision: i64,
    settings: SavedConnectionSettings,
}

async fn update(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<SavedConnectionChange>,
) -> Result<Json<SavedConnection>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .saved_connections()
            .update(&token, client, id, input.revision, &input.settings)
            .await?,
    ))
}

async fn remove(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Query(input): Query<Revision>,
) -> Result<Json<SavedConnection>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .saved_connections()
            .delete(&token, client, id, input.revision)
            .await?,
    ))
}
