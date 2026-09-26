use crate::{error::ApiError, request, StateData};
use axum::{extract::State, http::HeaderMap, routing::get, Json, Router};
use px_console_store::ClientType;
use serde::Deserialize;
use std::sync::Arc;

pub(crate) fn routes() -> Router<Arc<StateData>> {
    Router::new().route("/api/console/managed/license", get(status).put(install))
}

async fn administrator(state: &StateData, headers: &HeaderMap) -> Result<(), ApiError> {
    let token = request::administrator(state, headers)?;
    let profile = state
        .db
        .identity()
        .profile(&token, ClientType::AdminWeb)
        .await?;
    if profile.role != "admin" {
        return Err(ApiError::Rejected);
    }
    Ok(())
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct LicenseInput {
    wire: String,
}

async fn install(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
    Json(input): Json<LicenseInput>,
) -> Result<Json<crate::LicenseStatus>, ApiError> {
    administrator(&state, &headers).await?;
    let source = state.license_config.as_ref().ok_or(ApiError::Unavailable)?;
    let mut active_license = state.license.write().map_err(|_| ApiError::Unavailable)?;
    let installed = source
        .install(state.deployment, &input.wire)
        .map_err(|_| ApiError::Invalid)?;
    let status = installed.status();
    *active_license = Some(installed);
    Ok(Json(status))
}

async fn status(
    State(state): State<Arc<StateData>>,
    headers: HeaderMap,
) -> Result<Json<Option<crate::LicenseStatus>>, ApiError> {
    administrator(&state, &headers).await?;
    let license = state.license.read().map_err(|_| ApiError::Unavailable)?;
    Ok(Json(
        license.as_ref().map(crate::LicenseEntitlement::status),
    ))
}
