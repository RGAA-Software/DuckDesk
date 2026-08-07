use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::{
    AppInstance, AppPlacement, AppRowVo, Application, CreateApplicationReq, CreatePlacementReq,
    SaveAppReq, StartInstanceReq,
};
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use axum::extract::{Path, State};
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use std::sync::Arc;
use tokio::sync::Mutex;

fn err_msg<T: Default + serde::Serialize>(msg: String) -> Json<RespMessage<T>> {
    Json(RespMessage::new_message(600, msg, T::default()))
}

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

pub async fn handle_list_app_rows(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<Vec<AppRowVo>>>, SpvrApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_app_rows().await)))
}

pub async fn handle_save_app(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Json(req): Json<SaveAppReq>,
) -> Result<Json<RespMessage<AppRowVo>>, SpvrApiError> {
    match gAppScheduleManager.save_app(req).await {
        Ok(row) => Ok(Json(ok_resp(row))),
        Err(e) => {
            tracing::warn!("save app failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_delete_app(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Path(app_id): Path<String>,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    match gAppScheduleManager.delete_app(&app_id).await {
        Ok(()) => Ok(Json(ok_resp("ok".to_string()))),
        Err(e) => {
            tracing::warn!("delete app failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_next_port(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<i32>>, SpvrApiError> {
    Ok(Json(ok_resp(
        gAppScheduleManager.suggest_next_port().await,
    )))
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
    match gAppScheduleManager.start_instance(req).await {
        Ok(inst) => Ok(Json(ok_resp(inst))),
        Err(e) => {
            tracing::warn!("start instance failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_stop_instance(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Path(instance_id): Path<String>,
) -> Result<Json<RespMessage<AppInstance>>, SpvrApiError> {
    match gAppScheduleManager.stop_instance(&instance_id).await {
        Ok(inst) => Ok(Json(ok_resp(inst))),
        Err(e) => {
            tracing::warn!("stop instance failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_list_instances(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
) -> Result<Json<RespMessage<Vec<AppInstance>>>, SpvrApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_instances().await)))
}
