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
use px_console_store::UpdateDecision;
use px_license::Distribution as LicenseDistribution;
use px_release_catalog::{Distribution, ReleaseQuery, ReleaseSpec};
use serde::Deserialize;
use std::sync::Arc;
use uuid::Uuid;

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/managed/updates", get(managed).post(register))
        .route("/api/console/managed/updates/{id}", patch(decide))
        .route(
            "/api/console/managed/updates/{id}/node-trust",
            get(node_trust),
        )
        .route(
            "/api/console/managed/updates/{id}/node-trust/nodes",
            get(node_trust_nodes),
        )
        .route("/api/console/updates/latest", get(latest))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct NewRelease {
    request_id: Uuid,
    repository_publication_sha256: String,
    repository_root_version: i64,
    artifact: ReleaseSpec,
}

async fn register(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Input(input): Input<NewRelease>,
) -> Result<(StatusCode, Json<px_console_store::UpdateRelease>), ApiError> {
    let release = state
        .db
        .updates()
        .register(
            &request::administrator(&state, &headers)?,
            input.request_id,
            &input.repository_publication_sha256,
            input.repository_root_version,
            &input.artifact,
        )
        .await?;
    Ok((StatusCode::CREATED, Json(release)))
}

async fn managed(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<Page>,
) -> Result<Json<Vec<px_console_store::UpdateRelease>>, ApiError> {
    Ok(Json(
        state
            .db
            .updates()
            .list_managed(
                &request::administrator(&state, &headers)?,
                page.after,
                page.limit,
            )
            .await?,
    ))
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct ReleaseDecision {
    revision: i64,
    decision: UpdateDecision,
}

async fn decide(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Input(input): Input<ReleaseDecision>,
) -> Result<Json<px_console_store::UpdateRelease>, ApiError> {
    Ok(Json(
        state
            .db
            .updates()
            .decide(
                &request::administrator(&state, &headers)?,
                id,
                input.revision,
                input.decision,
            )
            .await?,
    ))
}

async fn node_trust(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
) -> Result<Json<px_console_store::NodeUpdateTrustSummary>, ApiError> {
    Ok(Json(
        state
            .db
            .updates()
            .node_trust_summary(
                &request::administrator(&state, &headers)?,
                id,
                release_distribution(&state),
            )
            .await?,
    ))
}

async fn node_trust_nodes(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(id): Path<Uuid>,
    Query(page): Query<Page>,
) -> Result<Json<Vec<px_console_store::NodeUpdateTrustStatus>>, ApiError> {
    Ok(Json(
        state
            .db
            .updates()
            .node_trust_statuses(
                &request::administrator(&state, &headers)?,
                id,
                release_distribution(&state),
                page.after,
                page.limit,
            )
            .await?,
    ))
}

fn release_distribution(state: &StateData) -> Distribution {
    match state.license.payload.distribution {
        LicenseDistribution::Official => Distribution::Official,
        LicenseDistribution::Customer => Distribution::Customer,
    }
}

async fn latest(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(target): Query<ReleaseQuery>,
) -> Result<Json<px_console_store::UpdateRelease>, ApiError> {
    let (token, client) = request::context(&state, &headers)?;
    Ok(Json(
        state.db.updates().latest(&token, client, &target).await?,
    ))
}
