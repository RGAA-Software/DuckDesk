use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::ApplicationType;
use crate::console_api_error::ConsoleApiError;
use crate::identity::access_policy::{
    guest_can_access_app, subject_owns_running_instance, user_can_access_app,
};
use crate::rdp_connection::{client_configuration, RdpClientConfiguration};
use crate::user::session::{AuthenticatedGuest, AuthenticatedUser};
use crate::{gConsoleSettings, gConsoleUserDeviceMgr, gDeviceManager};
use axum::extract::{Extension, Path};
use axum::http::header::CACHE_CONTROL;
use axum::response::{IntoResponse, Response};
use axum::Json;
use px_base::ok_resp;
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NativeConnectionRequest {
    #[serde(default)]
    pub view_only: bool,
    #[serde(default)]
    pub client_capability: String,
}

#[derive(Debug, Default, Serialize)]
pub struct NativeConnectionDescriptor {
    pub host: String,
    pub port: i32,
    pub device_id: String,
    pub instance_id: String,
    pub app_type: String,
    pub signal_device_id: String,
    pub relay_host: String,
    pub relay_port: u16,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rdp: Option<RdpClientConfiguration>,
}

#[derive(Debug, Default, Serialize)]
pub struct NativeDeviceConnectionDescriptor {
    pub host: String,
    pub port: i32,
    pub device_id: String,
    pub signal_device_id: String,
    pub relay_host: String,
    pub relay_port: u16,
}

pub async fn user_native_device_connection(
    Path(device_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
) -> Result<Response, ConsoleApiError> {
    // Keep the same product policy as the authenticated device list: an active
    // Console account may address every registered node. The Render still
    // verifies the device password before the native Client is launched.
    gConsoleUserDeviceMgr
        .query_user_device_summaries(subject.uid)
        .await?
        .into_iter()
        .find(|device| device.device_id == device_id)
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    let device = gDeviceManager
        .query_device_by_id(device_id.clone())
        .await
        .map_err(|_| ConsoleApiError::ResourceNotFound)?;
    let (host, port) = device
        .get_current_render_endpoints()
        .await
        .into_iter()
        .next()
        .ok_or(ConsoleApiError::DeviceOffline)?;
    let settings = gConsoleSettings.lock().await;
    let response = NativeDeviceConnectionDescriptor {
        host,
        port,
        device_id: device_id.clone(),
        signal_device_id: format!("server_{device_id}"),
        relay_host: settings.server_w3c_ip.clone(),
        relay_port: settings.relay_port,
    };
    Ok((
        [(CACHE_CONTROL, "no-store, private")],
        Json(ok_resp(response)),
    )
        .into_response())
}

async fn build_descriptor(
    instance_id: &str,
    subject_type: &str,
    subject_id: &str,
    request: NativeConnectionRequest,
) -> Result<Response, ConsoleApiError> {
    let instance = gAppScheduleManager
        .get_instance(instance_id)
        .await
        .filter(|instance| subject_owns_running_instance(instance, subject_type, subject_id))
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    let app = gAppScheduleManager
        .get_application(&instance.app_id)
        .await
        .ok_or(ConsoleApiError::ResourceNotFound)?;
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
    if request.view_only && (!app.allow_observer || app.app_type == ApplicationType::Rdp) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let device = gDeviceManager
        .query_device_by_id(instance.device_id.clone())
        .await
        .map_err(|_| ConsoleApiError::ResourceNotFound)?;
    let host = device
        .get_current_render_endpoints()
        .await
        .into_iter()
        .next()
        .map(|(host, _)| host)
        .ok_or(ConsoleApiError::DeviceOffline)?;
    let rdp = client_configuration(
        &app,
        &instance,
        request.view_only,
        &request.client_capability,
    )
    .await?;
    let (relay_host, relay_port) = if rdp.is_some() {
        (String::new(), 0)
    } else {
        let settings = gConsoleSettings.lock().await;
        (settings.server_w3c_ip.clone(), settings.relay_port)
    };
    let response = NativeConnectionDescriptor {
        host,
        port: instance.listen_port,
        device_id: instance.device_id.clone(),
        instance_id: instance.instance_id.clone(),
        app_type: app.app_type.as_str().to_string(),
        signal_device_id: format!(
            "server_{}__instance__{}",
            instance.device_id, instance.instance_id
        ),
        relay_host,
        relay_port,
        rdp,
    };
    Ok((
        [(CACHE_CONTROL, "no-store, private")],
        Json(ok_resp(response)),
    )
        .into_response())
}

pub async fn user_native_connection(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<NativeConnectionRequest>,
) -> Result<Response, ConsoleApiError> {
    build_descriptor(&instance_id, "user", &subject.uid, request).await
}

pub async fn guest_native_connection(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedGuest>,
    Json(request): Json<NativeConnectionRequest>,
) -> Result<Response, ConsoleApiError> {
    build_descriptor(&instance_id, "guest", &subject.guest_id, request).await
}

#[cfg(test)]
mod tests {
    use super::{NativeConnectionDescriptor, NativeDeviceConnectionDescriptor};

    #[test]
    fn native_descriptor_has_no_ephemeral_authorization_fields() {
        let descriptor = NativeConnectionDescriptor {
            host: "render.example.test".to_string(),
            port: 4613,
            device_id: "device-90".to_string(),
            instance_id: "instance-1".to_string(),
            app_type: "game".to_string(),
            signal_device_id: "server_device-90__instance__instance-1".to_string(),
            relay_host: "relay.example.test".to_string(),
            relay_port: 4605,
            rdp: None,
        };

        let json = serde_json::to_value(descriptor).expect("native descriptor must serialize");
        let object = json
            .as_object()
            .expect("native descriptor must be a JSON object");
        assert!(!object.contains_key("ticket"));
        assert!(!object.contains_key("renewal_token"));
        assert!(!object.contains_key("reservation"));
        assert!(!object.contains_key("expires_at"));
    }

    #[test]
    fn native_device_descriptor_has_no_password_or_ticket() {
        let descriptor = NativeDeviceConnectionDescriptor {
            host: "render.example.test".to_string(),
            port: 4601,
            device_id: "device-90".to_string(),
            signal_device_id: "server_device-90".to_string(),
            relay_host: "relay.example.test".to_string(),
            relay_port: 4605,
        };
        let json =
            serde_json::to_value(descriptor).expect("native device descriptor must serialize");
        let object = json
            .as_object()
            .expect("native device descriptor must be an object");
        for forbidden in [
            "password",
            "password_hash",
            "ticket",
            "renewal_token",
            "reservation",
            "expires_at",
        ] {
            assert!(!object.contains_key(forbidden));
        }
    }
}
