use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::{
    AppInstance, AppNode, AppPlacement, AppRowVo, Application, CreateApplicationReq,
    CreatePlacementReq, InstanceState, SaveAppReq, SaveNodeReq, StartInstanceReq,
};
use crate::gDeviceManager;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use axum::extract::{Path, Query, State};
use axum::response::{IntoResponse, Redirect, Response};
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

fn err_msg<T: Default + serde::Serialize>(msg: String) -> Json<RespMessage<T>> {
    Json(RespMessage::new_message(600, msg, T::default()))
}

/// 启动完成后的落地页:302 到该实例 render 的 web_client。
/// 浏览器直接打开 launch 链接 = 选节点/指定节点 → 启动 → 进流。
async fn launch_redirect(inst: AppInstance) -> Response {
    if inst.state == InstanceState::Failed {
        let msg = if inst.error.is_empty() {
            "启动失败".to_string()
        } else {
            inst.error
        };
        return err_msg::<String>(msg).into_response();
    }
    let ip = match gDeviceManager.query_device_by_id(inst.device_id.clone()).await {
        Ok(d) => {
            let ip = d.get_ip_from_link();
            if ip.is_empty() {
                "127.0.0.1".to_string()
            } else {
                ip
            }
        }
        Err(e) => {
            tracing::warn!("query device {} for launch redirect failed: {e}", inst.device_id);
            "127.0.0.1".to_string()
        }
    };
    let hint = if inst.web_client_hint.is_empty() {
        format!(
            "/web_client/?deviceId={}&instanceId={}",
            inst.device_id, inst.instance_id
        )
    } else {
        inst.web_client_hint.clone()
    };
    Redirect::to(&format!("http://{ip}:{}{hint}", inst.listen_port)).into_response()
}

/// GET /app/launch/{app_id}:自动挑选一个空闲节点启动,然后 302 进 web client。
pub async fn handle_launch_app(Path(app_id): Path<String>) -> Response {
    match gAppScheduleManager
        .start_instance(StartInstanceReq {
            app_id,
            device_id: None,
            listen_port: None,
        })
        .await
    {
        Ok(inst) => launch_redirect(inst).await,
        Err(e) => {
            tracing::warn!("launch app failed: {e}");
            err_msg::<String>(e).into_response()
        }
    }
}

/// GET /app/node/launch/{node_id}:在指定节点上启动,然后 302 进 web client。
pub async fn handle_launch_node(Path(node_id): Path<String>) -> Response {
    match gAppScheduleManager.start_node(&node_id).await {
        Ok(inst) => launch_redirect(inst).await,
        Err(e) => {
            tracing::warn!("launch node failed: {e}");
            err_msg::<String>(e).into_response()
        }
    }
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
    Query(params): Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<i32>>, SpvrApiError> {
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
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Json(req): Json<SaveNodeReq>,
) -> Result<Json<RespMessage<AppNode>>, SpvrApiError> {
    match gAppScheduleManager.save_node(req).await {
        Ok(node) => Ok(Json(ok_resp(node))),
        Err(e) => {
            tracing::warn!("save node failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_delete_node(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Path(node_id): Path<String>,
) -> Result<Json<RespMessage<String>>, SpvrApiError> {
    match gAppScheduleManager.delete_node(&node_id).await {
        Ok(()) => Ok(Json(ok_resp("ok".to_string()))),
        Err(e) => {
            tracing::warn!("delete node failed: {e}");
            Ok(err_msg(e))
        }
    }
}

pub async fn handle_list_nodes(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Query(params): Query<HashMap<String, String>>,
) -> Result<Json<RespMessage<Vec<AppNode>>>, SpvrApiError> {
    let app_id = params.get("app_id").cloned();
    Ok(Json(ok_resp(
        gAppScheduleManager.list_nodes(app_id.as_deref()).await,
    )))
}

pub async fn handle_start_node(
    State(_ctx): State<Arc<Mutex<SpvrContext>>>,
    Path(node_id): Path<String>,
) -> Result<Json<RespMessage<AppInstance>>, SpvrApiError> {
    match gAppScheduleManager.start_node(&node_id).await {
        Ok(inst) => Ok(Json(ok_resp(inst))),
        Err(e) => {
            tracing::warn!("start node failed: {e}");
            Ok(err_msg(e))
        }
    }
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
