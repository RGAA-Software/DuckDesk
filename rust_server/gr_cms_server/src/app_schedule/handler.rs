use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::{
    AppInstance, AppNode, AppPlacement, AppRowVo, Application, CreateApplicationReq,
    CreatePlacementReq, InstanceState, SaveAppReq, SaveNodeReq, StartInstanceReq,
};
use crate::gDeviceManager;
use crate::spvr_api_error::SpvrApiError;
use crate::spvr_context::SpvrContext;
use axum::extract::{ConnectInfo, Path, Query, State};
use axum::response::{Html, IntoResponse, Redirect, Response};
use axum::Json;
use gr_base::{ok_resp, RespMessage};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;
use tokio::sync::Mutex;

fn err_msg<T: Default + serde::Serialize>(msg: String) -> Json<RespMessage<T>> {
    Json(RespMessage::new_message(600, msg, T::default()))
}

/// 启动完成后的落地 URL:该实例 render 的 web_client 地址。
/// nonce 非空时带上,web client 信令用它做「同一浏览器」识别(自动接管旧连接)。
async fn build_launch_url(inst: &AppInstance, nonce: Option<&str>) -> Result<String, String> {
    if inst.state == InstanceState::Failed {
        return Err(if inst.error.is_empty() {
            "启动失败".to_string()
        } else {
            inst.error.clone()
        });
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
    let nonce_qs = nonce
        .filter(|n| !n.is_empty())
        .map(|n| format!("&nonce={n}"))
        .unwrap_or_default();
    Ok(format!("http://{ip}:{}{hint}{nonce_qs}", inst.listen_port))
}

/// 启动完成后的落地页:302 到该实例 render 的 web_client。
async fn launch_redirect(inst: AppInstance) -> Response {
    match build_launch_url(&inst, None).await {
        Ok(url) => Redirect::to(&url).into_response(),
        Err(msg) => err_msg::<String>(msg).into_response(),
    }
}

/// launch 引导页:JS 按 app 生成/复用浏览器 nonce(localStorage),调
/// POST /app/launch/start 完成启动后跳转。占位符在 handle_launch_app 里替换。
const LAUNCH_PAGE: &str = r#"<!DOCTYPE html>
<html lang="zh">
<head><meta charset="utf-8"><title>正在启动</title>
<style>body{font-family:sans-serif;display:flex;justify-content:center;align-items:center;height:100vh;margin:0;background:#1d1f24;color:#ddd}#msg{font-size:15px}</style>
</head>
<body><p id="msg">正在启动应用,请稍候…</p>
<script>
const appId = "__APP_ID__";
const appkey = "__APPKEY__";
const nonceKey = "godesk_launch_nonce_" + appId;
let nonce = null;
try {
  nonce = localStorage.getItem(nonceKey);
  if (!nonce) {
    nonce = (crypto.randomUUID ? crypto.randomUUID() : String(Date.now()) + Math.random().toString(16).slice(2));
    localStorage.setItem(nonceKey, nonce);
  }
} catch (e) { nonce = String(Date.now()) + Math.random().toString(16).slice(2); }
fetch("/api/v1/app/control/app/launch/start/" + encodeURIComponent(appId) + "?appkey=" + encodeURIComponent(appkey), {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ nonce: nonce })
}).then(function (r) { return r.json(); }).then(function (d) {
  if (d.code === 200 && d.data && d.data.url) { window.location.replace(d.data.url); }
  else { document.getElementById("msg").innerText = "启动失败: " + (d.message || ("code=" + d.code)); }
}).catch(function (e) {
  document.getElementById("msg").innerText = "启动失败: " + e;
});
</script>
</body></html>"#;

/// GET /app/launch/{app_id} 的 query。
#[derive(serde::Deserialize)]
pub struct LaunchPageQuery {
    /// 由 appkey filter 校验;引导页 JS 调启动接口时原样带上。
    #[serde(default)]
    appkey: Option<String>,
    /// raw=1: 不走引导页,老的直开行为(303 + IP 兜底去重),给 curl/非浏览器调用。
    #[serde(default)]
    raw: Option<String>,
}

/// GET /app/launch/{app_id}:默认返回引导页;raw=1 直接 303。
/// 引导页实现「一个浏览器一个实例」:同浏览器重复打开并入已启动实例,
/// 不同浏览器/机器各自一个实例,NAT 后也不混;raw=1 为 IP 兜底去重。
pub async fn handle_launch_app(
    Path(app_id): Path<String>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    Query(q): Query<LaunchPageQuery>,
) -> Response {
    if q.raw.is_some() {
        return match gAppScheduleManager
            .start_instance(StartInstanceReq {
                app_id,
                device_id: None,
                listen_port: None,
                client_key: Some(addr.ip().to_string()),
                client_key_permanent: false,
            })
            .await
        {
            Ok(inst) => launch_redirect(inst).await,
            Err(e) => {
                tracing::warn!("launch app failed: {e}");
                err_msg::<String>(e).into_response()
            }
        };
    }
    Html(LAUNCH_PAGE
        .replace("__APP_ID__", &app_id)
        .replace("__APPKEY__", q.appkey.as_deref().unwrap_or("")))
    .into_response()
}

#[derive(serde::Deserialize)]
pub struct LaunchStartReq {
    /// 浏览器 nonce(launch 页生成,按 app 存 localStorage);空则退化为 IP 兜底去重。
    #[serde(default)]
    nonce: Option<String>,
}

#[derive(Default, serde::Serialize)]
pub struct LaunchStartResp {
    pub url: String,
}

/// POST /app/launch/start/{app_id}:launch 引导页的启动接口。
/// nonce 作 client_key 永久去重:同一浏览器重复打开并入已启动实例,不新开;
/// 不同浏览器/机器各自一个实例。返回的落地 URL 带 nonce,供 web client
/// 信令识别「同一浏览器」自动接管旧连接。
pub async fn handle_launch_start(
    Path(app_id): Path<String>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    Json(req): Json<LaunchStartReq>,
) -> Response {
    let (key, permanent) = match req.nonce.as_deref().filter(|n| !n.is_empty()) {
        Some(n) => (n.to_string(), true),
        None => (addr.ip().to_string(), false),
    };
    match gAppScheduleManager
        .start_instance(StartInstanceReq {
            app_id,
            device_id: None,
            listen_port: None,
            client_key: Some(key),
            client_key_permanent: permanent,
        })
        .await
    {
        Ok(inst) => match build_launch_url(&inst, req.nonce.as_deref()).await {
            Ok(url) => Json(ok_resp(LaunchStartResp { url })).into_response(),
            Err(msg) => err_msg::<String>(msg).into_response(),
        },
        Err(e) => {
            tracing::warn!("launch start failed: {e}");
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
