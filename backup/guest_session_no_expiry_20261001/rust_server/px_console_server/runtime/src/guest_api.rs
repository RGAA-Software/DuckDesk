use crate::{
    error::ApiError,
    request::{self, Input, Page, Params as Query, Revision, Route as Path},
    StateData,
};
use axum::{
    extract::{ConnectInfo, State},
    http::{header, HeaderMap, StatusCode},
    routing::{get, post},
    Json, Router,
};
use px_console_store::{ClientType, GuestBlockReason, GuestSession, ManagedGuest};
use serde::Deserialize;
use serde_json::{json, Value};
use std::{net::SocketAddr, sync::Arc, time::Duration};
use uuid::Uuid;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Empty {}

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/guest-sessions", post(issue))
        .route("/api/console/guest-session", get(profile).delete(logout))
        .route("/api/console/guest/applications", get(applications))
        .route("/api/console/guest/applications/{id}", get(application))
        .route("/api/console/managed/guests", get(managed))
        .route("/api/console/managed/guests/{id}/block", post(block))
        .route(
            "/api/console/managed/guests/{id}/block-source",
            post(block_source),
        )
}

async fn issue(
    State(state): State<Arc<StateData>>,
    ConnectInfo(peer): ConnectInfo<SocketAddr>,
    headers: HeaderMap,
    Input(_): Input<Empty>,
) -> Result<(StatusCode, Json<Value>), ApiError> {
    let client = request::client(&headers)?;
    state.policy.check(&headers, client)?;
    if client == ClientType::AdminWeb || headers.contains_key(header::AUTHORIZATION) {
        return Err(ApiError::Rejected);
    }
    let source = state.guests.admit(peer.ip())?;
    let (token, digest) = request::mint();
    let guest = state
        .db
        .guests()
        .issue(&source, &digest, client, state.guests.lifetime)
        .await?;
    Ok((
        StatusCode::CREATED,
        Json(json!({"session":guest,"token":token.as_str()})),
    ))
}

async fn profile(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<Json<GuestSession>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(state.db.guests().authenticate(&token, client).await?))
}

async fn logout(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<StatusCode, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    state.db.guests().logout(&token, client).await?;
    Ok(StatusCode::NO_CONTENT)
}

async fn applications(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Value>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(json!(
        state
            .db
            .applications()
            .list_visible_guest(&token, client, page.after, page.limit)
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
            .get_visible_guest(&token, client, id)
            .await?
    )))
}

async fn managed(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Vec<ManagedGuest>>, ApiError> {
    let admin = request::administrator(&state, &headers)?;
    Ok(Json(
        state
            .db
            .guests()
            .list_managed(&admin, page.after, page.limit)
            .await?,
    ))
}

async fn block(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(value): Input<Revision>,
) -> Result<Json<GuestSession>, ApiError> {
    let admin = request::administrator(&state, &headers)?;
    Ok(Json(
        state
            .db
            .guests()
            .block(&admin, id, value.revision, GuestBlockReason::Operator)
            .await?,
    ))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct SourceBan {
    revision: i64,
    lifetime_seconds: u32,
}

async fn block_source(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(value): Input<SourceBan>,
) -> Result<Json<GuestSession>, ApiError> {
    let admin = request::administrator(&state, &headers)?;
    Ok(Json(
        state
            .db
            .guests()
            .block_source(
                &admin,
                id,
                value.revision,
                Duration::from_secs(u64::from(value.lifetime_seconds)),
                GuestBlockReason::Operator,
            )
            .await?,
    ))
}
