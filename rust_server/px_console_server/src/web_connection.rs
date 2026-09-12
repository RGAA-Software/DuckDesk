use crate::app_schedule::gAppScheduleManager;
use crate::console_api_error::ConsoleApiError;
use crate::identity::access_policy::{guest_can_access_app, subject_owns_running_instance, user_can_access_app};
use crate::rtc::model::RtcSessionIceConfig;
use crate::user::session::{AuthenticatedGuest, AuthenticatedUser};
use crate::{gConsoleSettings, gDeviceManager, gRtcConfigManager};
use axum::extract::{Extension, Path};
use axum::http::header::CACHE_CONTROL;
use axum::response::{IntoResponse, Response};
use axum::Json;
use px_base::ok_resp;
use serde::{Deserialize, Serialize};
use std::net::IpAddr;
use uuid::Uuid;

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WebConnectionRequest {
    pub client_nonce: String,
    #[serde(default = "default_join_mode")]
    pub join_mode: String,
}

#[derive(Debug, Default, Serialize)]
pub struct WebConnectionDescriptor {
    pub launch_url: String,
    pub device_id: String,
    pub instance_id: String,
    pub stream_id: String,
    pub password_hash: String,
    pub permissions: Vec<String>,
    pub rtc_ice_config: RtcSessionIceConfig,
    pub relay_host: String,
    pub relay_port: u16,
    pub signal_device_id: String,
}

fn default_join_mode() -> String {
    "control".to_string()
}

fn validate_request(request: &WebConnectionRequest) -> Result<(), ConsoleApiError> {
    if request.client_nonce.is_empty() || request.client_nonce.len() > 128 || !matches!(request.join_mode.as_str(), "control" | "observe") {
        return Err(ConsoleApiError::InvalidParams);
    }
    Ok(())
}

fn permissions(join_mode: &str, include_file: bool) -> Vec<String> {
    if join_mode == "observe" {
        return vec!["view".to_string(), "audio".to_string()];
    }
    let mut result = vec!["view".to_string(), "input".to_string(), "clipboard".to_string(), "audio".to_string()];
    if include_file {
        result.push("file".to_string());
    }
    result
}

fn host_for_url(host: &str) -> String {
    match host.parse::<IpAddr>() {
        Ok(IpAddr::V6(_)) => format!("[{host}]"),
        _ => host.to_string(),
    }
}

async fn rtc_config(subject: &str) -> Result<RtcSessionIceConfig, ConsoleApiError> {
    gRtcConfigManager.issue_session_config(subject).await.map_err(|error| {
        tracing::error!(%error, "issue RTC configuration failed");
        ConsoleApiError::InternalError
    })
}

async fn relay_endpoint() -> (String, u16) {
    let settings = gConsoleSettings.lock().await;
    (settings.server_w3c_ip.clone(), settings.relay_port)
}

fn device_password_hash(device: &crate::device::console_device::ConsoleDevice) -> Result<String, ConsoleApiError> {
    let password_hash = if device.safety_pwd_md5.is_empty() { &device.random_pwd_md5 } else { &device.safety_pwd_md5 };
    if password_hash.is_empty() {
        return Err(ConsoleApiError::InvalidParams);
    }
    Ok(password_hash.clone())
}

pub async fn user_web_device_connection(
    Path(device_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<WebConnectionRequest>,
) -> Result<Response, ConsoleApiError> {
    validate_request(&request)?;
    let device = gDeviceManager.query_device_by_id(device_id.clone()).await.map_err(|_| ConsoleApiError::ResourceNotFound)?;
    if !device.active || (request.join_mode == "observe" && !device.allow_observer) {
        return Err(ConsoleApiError::DeviceOffline);
    }
    let (host, port) = device.get_current_render_endpoints().await.into_iter().next().ok_or(ConsoleApiError::DeviceOffline)?;
    let stream_id = format!("web-{}", Uuid::new_v4().simple());
    let (relay_host, relay_port) = relay_endpoint().await;
    let descriptor = WebConnectionDescriptor {
        launch_url: format!("http://{}:{}/web/", host_for_url(&host), port),
        device_id: device_id.clone(),
        instance_id: String::new(),
        stream_id,
        password_hash: device_password_hash(&device)?,
        permissions: permissions(&request.join_mode, true),
        rtc_ice_config: rtc_config(&subject.sid).await?,
        relay_host,
        relay_port,
        signal_device_id: format!("server_{device_id}"),
    };
    Ok(([(CACHE_CONTROL, "no-store, private")], Json(ok_resp(descriptor))).into_response())
}

async fn instance_descriptor(
    instance_id: &str,
    subject_type: &str,
    subject_id: &str,
    session_id: &str,
    request: WebConnectionRequest,
) -> Result<Response, ConsoleApiError> {
    validate_request(&request)?;
    let instance = gAppScheduleManager
        .get_instance(instance_id)
        .await
        .filter(|instance| subject_owns_running_instance(instance, subject_type, subject_id))
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    let app = gAppScheduleManager.get_application(&instance.app_id).await.ok_or(ConsoleApiError::ResourceNotFound)?;
    if subject_type == "guest" {
        if !guest_can_access_app(&app.access_mode) {
            return Err(ConsoleApiError::ResourceNotFound);
        }
    } else {
        let acl_ids = if app.access_mode == crate::app_schedule::manager::AppAccessMode::Acl {
            crate::identity::manager::IdentityManager::authorized_app_ids(subject_id).await?
        } else {
            Default::default()
        };
        if !user_can_access_app(&app.access_mode, &instance.app_id, &acl_ids) {
            return Err(ConsoleApiError::ResourceNotFound);
        }
    }
    if request.join_mode == "observe" && !app.allow_observer {
        return Err(ConsoleApiError::ResourceNotFound);
    }
    let device = gDeviceManager.query_device_by_id(instance.device_id.clone()).await.map_err(|_| ConsoleApiError::ResourceNotFound)?;
    let host = device
        .get_current_render_endpoints()
        .await
        .into_iter()
        .next()
        .map(|(host, _)| host)
        .ok_or(ConsoleApiError::DeviceOffline)?;
    let stream_id = format!("web-{}", Uuid::new_v4().simple());
    let (relay_host, relay_port) = relay_endpoint().await;
    let descriptor = WebConnectionDescriptor {
        launch_url: format!("http://{}:{}/web/", host_for_url(&host), instance.listen_port),
        device_id: instance.device_id.clone(),
        instance_id: instance.instance_id.clone(),
        stream_id,
        password_hash: device_password_hash(&device)?,
        permissions: permissions(&request.join_mode, false),
        rtc_ice_config: rtc_config(session_id).await?,
        relay_host,
        relay_port,
        signal_device_id: format!("server_{}__instance__{}", instance.device_id, instance.instance_id),
    };
    Ok(([(CACHE_CONTROL, "no-store, private")], Json(ok_resp(descriptor))).into_response())
}

pub async fn user_web_instance_connection(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<WebConnectionRequest>,
) -> Result<Response, ConsoleApiError> {
    instance_descriptor(&instance_id, "user", &subject.uid, &subject.sid, request).await
}

pub async fn guest_web_instance_connection(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedGuest>,
    Json(request): Json<WebConnectionRequest>,
) -> Result<Response, ConsoleApiError> {
    instance_descriptor(&instance_id, "guest", &subject.guest_id, &subject.sid, request).await
}

#[cfg(test)]
mod tests {
    use super::WebConnectionDescriptor;

    #[test]
    fn descriptor_contains_password_authentication_and_no_ephemeral_authorization() {
        let value = serde_json::to_value(WebConnectionDescriptor::default()).unwrap();
        let object = value.as_object().unwrap();
        assert!(object.contains_key("password_hash"));
        for forbidden in ["ticket", "renewal_token", "expires_at", "logical_session_id"] {
            assert!(!object.contains_key(forbidden));
        }
    }
}
