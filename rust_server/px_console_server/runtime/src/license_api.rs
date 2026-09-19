use crate::{error::ApiError, request, StateData};
use axum::{extract::State, http::HeaderMap, routing::get, Json, Router};
use std::sync::Arc;

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new().route("/api/console/managed/license", get(status))
}

async fn status(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<Json<crate::LicenseStatus>, ApiError> {
    request::administrator(&state, &headers)?;
    Ok(Json(state.license.status()))
}
