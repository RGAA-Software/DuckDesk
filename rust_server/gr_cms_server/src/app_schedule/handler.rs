use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::{
    AppInstance, AppPlacement, Application, CreateApplicationReq, CreatePlacementReq,
    StartInstanceReq,
};
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use axum::extract::{Path, State};
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use std::sync::Arc;
use tokio::sync::Mutex;

pub async fn handle_create_application(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Json(req): Json<CreateApplicationReq>,
) -> Result<Json<RespMessage<Application>>, SpvrApiError> {
    let app = gAppScheduleManager
        .create_application(req)
        .await
        .map_err(|e| {
            tracing::warn!("create application failed: {e}");
            SpvrApiError::InvalidParams
        })?;
    Ok(Json(ok_resp(app)))
}

pub async fn handle_list_applications(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<Vec<Application>>>, SpvrApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_applications().await)))
}

pub async fn handle_create_placement(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Json(req): Json<CreatePlacementReq>,
) -> Result<Json<RespMessage<AppPlacement>>, SpvrApiError> {
    let p = gAppScheduleManager
        .create_placement(req)
        .await
        .map_err(|e| {
            tracing::warn!("create placement failed: {e}");
            SpvrApiError::InvalidParams
        })?;
    Ok(Json(ok_resp(p)))
}

pub async fn handle_list_placements(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<Vec<AppPlacement>>>, SpvrApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_placements().await)))
}

pub async fn handle_start_instance(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Json(req): Json<StartInstanceReq>,
) -> Result<Json<RespMessage<AppInstance>>, SpvrApiError> {
    let inst = gAppScheduleManager
        .start_instance(req)
        .await
        .map_err(|e| {
            tracing::warn!("start instance failed: {e}");
            SpvrApiError::InvalidParams
        })?;
    Ok(Json(ok_resp(inst)))
}

pub async fn handle_stop_instance(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Path(instance_id): Path<String>,
) -> Result<Json<RespMessage<AppInstance>>, SpvrApiError> {
    let inst = gAppScheduleManager
        .stop_instance(&instance_id)
        .await
        .map_err(|e| {
            tracing::warn!("stop instance failed: {e}");
            SpvrApiError::InvalidParams
        })?;
    Ok(Json(ok_resp(inst)))
}

pub async fn handle_list_instances(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<Vec<AppInstance>>>, SpvrApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_instances().await)))
}
