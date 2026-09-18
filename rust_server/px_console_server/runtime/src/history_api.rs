use crate::{
    error::ApiError,
    request::{self, Params as Query},
    StateData,
};
use axum::{extract::State, http::HeaderMap, routing::get, Json, Router};
use px_console_store::{ChannelRecord, FileTransferRecord, RecordingProfile, VisitRecord};
use serde::Deserialize;
use std::sync::Arc;
use uuid::Uuid;

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/activity/visits", get(owned_visits))
        .route("/api/console/activity/channels", get(owned_channels))
        .route("/api/console/file-transfers", get(owned_transfers))
        .route("/api/console/recordings", get(visible_recordings))
        .route("/api/console/managed/activity/visits", get(managed_visits))
        .route(
            "/api/console/managed/activity/channels",
            get(managed_channels),
        )
        .route(
            "/api/console/managed/file-transfers",
            get(managed_transfers),
        )
        .route("/api/console/managed/recordings", get(managed_recordings))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct HistoryPage {
    after: Option<Uuid>,
    limit: u32,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct ChannelPage {
    session: Option<Uuid>,
    after: Option<Uuid>,
    limit: u32,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct ManagedTransferPage {
    node: Option<Uuid>,
    after: Option<Uuid>,
    limit: u32,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct VisibleRecordingPage {
    node: Uuid,
    after: Option<Uuid>,
    limit: u32,
}

async fn owned_visits(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<HistoryPage>,
) -> Result<Json<Vec<VisitRecord>>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .activity()
            .visits_owned(context.credential(), context.client, page.after, page.limit)
            .await?,
    ))
}

async fn owned_channels(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<ChannelPage>,
) -> Result<Json<Vec<ChannelRecord>>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .activity()
            .channels_owned(
                context.credential(),
                context.client,
                page.session,
                page.after,
                page.limit,
            )
            .await?,
    ))
}

async fn owned_transfers(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<HistoryPage>,
) -> Result<Json<Vec<FileTransferRecord>>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .file_transfers()
            .list_owned(context.credential(), context.client, page.after, page.limit)
            .await?,
    ))
}

async fn visible_recordings(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<VisibleRecordingPage>,
) -> Result<Json<Vec<RecordingProfile>>, ApiError> {
    let context = request::resource_context(&state, &headers)?;
    Ok(Json(
        state
            .db
            .recordings()
            .list_visible(
                context.user_token()?,
                context.client,
                page.node,
                page.after,
                page.limit,
            )
            .await?,
    ))
}

async fn managed_visits(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<HistoryPage>,
) -> Result<Json<Vec<VisitRecord>>, ApiError> {
    Ok(Json(
        state
            .db
            .activity()
            .visits_managed(
                &request::administrator(&state, &headers)?,
                page.after,
                page.limit,
            )
            .await?,
    ))
}

async fn managed_channels(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<ChannelPage>,
) -> Result<Json<Vec<ChannelRecord>>, ApiError> {
    Ok(Json(
        state
            .db
            .activity()
            .channels_managed(
                &request::administrator(&state, &headers)?,
                page.session,
                page.after,
                page.limit,
            )
            .await?,
    ))
}

async fn managed_transfers(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<ManagedTransferPage>,
) -> Result<Json<Vec<FileTransferRecord>>, ApiError> {
    Ok(Json(
        state
            .db
            .file_transfers()
            .list_managed(
                &request::administrator(&state, &headers)?,
                page.after,
                page.node,
                page.limit,
            )
            .await?,
    ))
}

async fn managed_recordings(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<ManagedTransferPage>,
) -> Result<Json<Vec<RecordingProfile>>, ApiError> {
    Ok(Json(
        state
            .db
            .recordings()
            .list_managed(
                &request::administrator(&state, &headers)?,
                page.node,
                page.after,
                page.limit,
            )
            .await?,
    ))
}
