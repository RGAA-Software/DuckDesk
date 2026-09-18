use crate::{
    error::ApiError,
    request::{self, Input, Params as Query, Revision, Route as Path},
    StateData,
};
use axum::{
    extract::State,
    http::HeaderMap,
    routing::{get, patch},
    Json, Router,
};
use chrono::{DateTime, Utc};
use px_console_store::{
    TelemetryAlertCursor, TelemetryAlertFilter, TelemetryAlertMetric, TelemetryAlertPolicy,
    TelemetryAlertSeverity, TelemetryAlertState,
};
use serde::Deserialize;
use serde_json::{json, Value};
use std::sync::Arc;
use uuid::Uuid;

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new()
        .route("/api/console/managed/telemetry-alerts", get(list))
        .route("/api/console/managed/telemetry-alerts/{id}", get(detail))
        .route(
            "/api/console/managed/telemetry-alerts/{id}/acknowledgement",
            patch(acknowledge),
        )
        .route(
            "/api/console/managed/nodes/{id}/telemetry-alert-policy",
            get(policy).patch(configure_policy),
        )
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct AlertPage {
    node_id: Option<Uuid>,
    metric: Option<TelemetryAlertMetric>,
    severity: Option<TelemetryAlertSeverity>,
    state: Option<TelemetryAlertState>,
    before_updated_at: Option<DateTime<Utc>>,
    before_id: Option<Uuid>,
    limit: u32,
}

impl AlertPage {
    fn filter(&self) -> Result<TelemetryAlertFilter, ApiError> {
        let before = match (self.before_updated_at, self.before_id) {
            (None, None) => None,
            (Some(updated_at), Some(id)) => Some(TelemetryAlertCursor { updated_at, id }),
            _ => return Err(ApiError::Invalid),
        };
        Ok(TelemetryAlertFilter {
            node_id: self.node_id,
            metric: self.metric,
            severity: self.severity,
            state: self.state,
            before,
        })
    }
}

async fn list(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Query(page): Query<AlertPage>,
) -> Result<Json<Value>, ApiError> {
    let filter = page.filter()?;
    Ok(Json(json!(
        state
            .db
            .telemetry_alerts()
            .list(
                &request::administrator(&state, &headers)?,
                filter,
                page.limit,
            )
            .await?
    )))
}

async fn detail(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(event_id): Path<Uuid>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .telemetry_alerts()
            .get(&request::administrator(&state, &headers)?, event_id)
            .await?
    )))
}

async fn acknowledge(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(event_id): Path<Uuid>,
    Input(input): Input<Revision>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .telemetry_alerts()
            .acknowledge(
                &request::administrator(&state, &headers)?,
                event_id,
                input.revision,
            )
            .await?
    )))
}

async fn policy(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(node_id): Path<Uuid>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .telemetry_alerts()
            .policy(&request::administrator(&state, &headers)?, node_id)
            .await?
    )))
}

async fn configure_policy(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Path(node_id): Path<Uuid>,
    Input(input): Input<TelemetryAlertPolicy>,
) -> Result<Json<Value>, ApiError> {
    Ok(Json(json!(
        state
            .db
            .telemetry_alerts()
            .configure_policy(&request::administrator(&state, &headers)?, node_id, input,)
            .await?
    )))
}
