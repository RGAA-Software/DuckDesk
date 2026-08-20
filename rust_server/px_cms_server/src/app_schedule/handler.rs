use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::{
    AppInstance, AppNode, AppPlacement, AppRowVo, Application, CreateApplicationReq,
    CreatePlacementReq, SaveAppReq, SaveNodeReq, StartInstanceReq,
};
use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::event::audit;
use axum::extract::{Path, Query, State};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

fn err_msg<T: Default + serde::Serialize>(msg: String) -> Json<RespMessage<T>> {
    Json(RespMessage::new_message(600, msg, T::default()))
}

pub async fn handle_create_application(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Json(req): Json<CreateApplicationReq>,
) -> Result<Json<RespMessage<Application>>, CmsApiError> {
    let app = gAppScheduleManager
        .create_application(req)
        .await
        .map_err(|e| {
            tracing::warn!("create application failed: {e}");
            CmsApiError::InvalidParams
        })?;
    Ok(Json(ok_resp(app)))
}

pub async fn handle_list_applications(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<Vec<Application>>>, CmsApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_applications().await)))
}

pub async fn handle_list_app_rows(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<Vec<AppRowVo>>>, CmsApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_app_rows().await)))
}

pub async fn handle_save_app(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Json(req): Json<SaveAppReq>,
) -> Result<Json<RespMessage<AppRowVo>>, CmsApiError> {
    match gAppScheduleManager.save_app(req).await {
        Ok(row) => {
            audit::record(
                "admin",
                "license_owner",
                "application_save",
                "success",
                "application",
                &row.app_id,
                "",
            )
            .await;
            Ok(Json(ok_resp(row)))
        }
        Err(e) => {
            tracing::warn!("save app failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_delete_app(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Path(app_id): Path<String>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    match gAppScheduleManager.delete_app(&app_id).await {
        Ok(()) => Ok(Json(ok_resp("ok".to_string()))),
        Err(e) => {
            tracing::warn!("delete app failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_next_port(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Query(params): Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<i32>>, CmsApiError> {
    let device_id = params.get("device_id").cloned().unwrap_or_default();
    match gAppScheduleManager.suggest_next_port(&device_id).await {
        Ok(port) => Ok(Json(ok_resp(port))),
        Err(e) => {
            tracing::warn!("suggest next port failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_save_node(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Json(req): Json<SaveNodeReq>,
) -> Result<Json<RespMessage<AppNode>>, CmsApiError> {
    match gAppScheduleManager.save_node(req).await {
        Ok(node) => Ok(Json(ok_resp(node))),
        Err(e) => {
            tracing::warn!("save node failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_delete_node(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Path(node_id): Path<String>,
) -> Result<Json<RespMessage<String>>, CmsApiError> {
    match gAppScheduleManager.delete_node(&node_id).await {
        Ok(()) => Ok(Json(ok_resp("ok".to_string()))),
        Err(e) => {
            tracing::warn!("delete node failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_list_nodes(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Query(params): Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<AppNode>>>, CmsApiError> {
    let app_id = params.get("app_id").cloned();
    Ok(Json(ok_resp(
        gAppScheduleManager.list_nodes(app_id.as_deref()).await,
    )))
}

pub async fn handle_start_node(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Path(node_id): Path<String>,
) -> Result<Json<RespMessage<AppInstance>>, CmsApiError> {
    match gAppScheduleManager.start_node(&node_id).await {
        Ok(inst) => {
            audit::record(
                "admin",
                "license_owner",
                "app_start",
                "success",
                "app_instance",
                &inst.instance_id,
                "node",
            )
            .await;
            Ok(Json(ok_resp(inst)))
        }
        Err(e) => {
            tracing::warn!("start node failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_create_placement(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Json(req): Json<CreatePlacementReq>,
) -> Result<Json<RespMessage<AppPlacement>>, CmsApiError> {
    let p = gAppScheduleManager
        .create_placement(req)
        .await
        .map_err(|e| {
            tracing::warn!("create placement failed: {e}");
            CmsApiError::InvalidParams
        })?;
    Ok(Json(ok_resp(p)))
}

pub async fn handle_list_placements(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<Vec<AppPlacement>>>, CmsApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_placements().await)))
}

pub async fn handle_start_instance(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Json(req): Json<StartInstanceReq>,
) -> Result<Json<RespMessage<AppInstance>>, CmsApiError> {
    match gAppScheduleManager.start_instance(req).await {
        Ok(inst) => {
            audit::record(
                "admin",
                "license_owner",
                "app_start",
                "success",
                "app_instance",
                &inst.instance_id,
                "automatic_placement",
            )
            .await;
            Ok(Json(ok_resp(inst)))
        }
        Err(e) => {
            tracing::warn!("start instance failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_stop_instance(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
    Path(instance_id): Path<String>,
) -> Result<Json<RespMessage<AppInstance>>, CmsApiError> {
    match gAppScheduleManager.stop_instance(&instance_id).await {
        Ok(inst) => {
            audit::record(
                "admin",
                "license_owner",
                "app_stop",
                "success",
                "app_instance",
                &inst.instance_id,
                "forced_by_admin",
            )
            .await;
            Ok(Json(ok_resp(inst)))
        }
        Err(e) => {
            tracing::warn!("stop instance failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_list_instances(
    State(_ctx): State<Arc<Mutex<CmsContext>>>,
) -> Result<Json<RespMessage<Vec<AppInstance>>>, CmsApiError> {
    Ok(Json(ok_resp(gAppScheduleManager.list_instances().await)))
}
