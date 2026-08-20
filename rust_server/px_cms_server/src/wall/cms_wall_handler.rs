use crate::cms_api_error::CmsApiError;
use crate::cms_context::CmsContext;
use crate::{gCmsServiceConnMgr, gDeviceManager};
use axum::extract::State;
use axum::Json;
use protocol::cms_service::{
    CmsServiceCreateWallSession, CmsServiceCreateWallSessionResult,
};
use px_base::{ok_resp, RespMessage};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{oneshot, Mutex};

lazy_static::lazy_static! {
    static ref WALL_SESSION_WAITERS: Mutex<HashMap<String, oneshot::Sender<CmsServiceCreateWallSessionResult>>> =
        Mutex::new(HashMap::new());
}

#[derive(Debug, Deserialize)]
pub struct CreateWallSessionRequest {
    pub device_id: String,
    pub offer_sdp: String,
}

#[derive(Debug, Default, Serialize)]
pub struct WallSessionAnswer {
    pub session_id: String,
    pub answer_sdp: String,
    pub render_ip: String,
    pub render_port: i32,
}

/// Called by the CMS service websocket receive path. Results are paired with
/// the HTTP request without ever exposing the device password to the browser.
pub async fn on_wall_session_result(result: CmsServiceCreateWallSessionResult) {
    let waiter = WALL_SESSION_WAITERS.lock().await.remove(&result.request_id);
    if let Some(waiter) = waiter {
        let _ = waiter.send(result);
    } else {
        tracing::warn!("late/unknown wall result: {}", result.request_id);
    }
}

/// CMS-owned signaling path:
/// browser -> authenticated CMS HTTP -> authenticated CMS/px_service WS ->
/// localhost render HTTP. Render accepts wall_observer only from loopback.
pub async fn create_wall_session(
    State(_context): State<Arc<Mutex<CmsContext>>>,
    Json(req): Json<CreateWallSessionRequest>,
) -> Result<Json<RespMessage<WallSessionAnswer>>, CmsApiError> {
    let device_id = req.device_id.trim();
    if device_id.is_empty() || req.offer_sdp.len() < 32 || req.offer_sdp.len() > 256 * 1024 {
        return Err(CmsApiError::InvalidParams);
    }

    let device = gDeviceManager
        .query_device_by_id(device_id.to_string())
        .await?;
    let password_md5 = if !device.safety_pwd_md5.is_empty() {
        device.safety_pwd_md5.clone()
    } else {
        device.random_pwd_md5.clone()
    };
    if password_md5.is_empty() {
        return Err(CmsApiError::SafetyPwdMissing);
    }

    let Some((render_ip, render_port)) = device.get_render_endpoints().into_iter().next() else {
        return Err(CmsApiError::ConnectionNotFound);
    };
    let service_conn = gCmsServiceConnMgr
        .get_conn(device_id.to_string())
        .await
        .map_err(|_| CmsApiError::DeviceOffline)?;

    let request_id = format!("wall_req_{}", uuid::Uuid::new_v4().simple());
    let session_id = format!("wall_{}", uuid::Uuid::new_v4().simple());
    let (tx, rx) = oneshot::channel();
    WALL_SESSION_WAITERS.lock().await.insert(request_id.clone(), tx);

    let sent = service_conn
        .lock()
        .await
        .send_create_wall_session(CmsServiceCreateWallSession {
            request_id: request_id.clone(),
            session_id: session_id.clone(),
            device_id: device_id.to_string(),
            render_port,
            safety_pwd_md5: password_md5,
            offer_sdp: req.offer_sdp,
        })
        .await;
    if !sent {
        WALL_SESSION_WAITERS.lock().await.remove(&request_id);
        return Err(CmsApiError::ConnectionNotFound);
    }

    let result = match tokio::time::timeout(Duration::from_secs(16), rx).await {
        Ok(Ok(result)) => result,
        _ => {
            WALL_SESSION_WAITERS.lock().await.remove(&request_id);
            return Err(CmsApiError::RequestTimeout);
        }
    };
    if !result.ok || result.answer_sdp.is_empty() {
        tracing::warn!(
            "wall signaling rejected for {}: {}",
            device_id,
            result.error
        );
        return Err(CmsApiError::ConnectionNotFound);
    }

    Ok(Json(ok_resp(WallSessionAnswer {
        session_id,
        answer_sdp: result.answer_sdp,
        render_ip,
        render_port,
    })))
}
