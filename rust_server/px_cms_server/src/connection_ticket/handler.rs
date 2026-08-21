use crate::app_schedule::gAppScheduleManager;
use crate::app_schedule::manager::InstanceState;
use crate::cms_api_error::CmsApiError;
use crate::connection_ticket::manager::ConnectionTicketManager;
use crate::connection_ticket::model::TicketResponse;
use crate::event::audit;
use crate::user::session::{AuthenticatedGuest, AuthenticatedUser};
use crate::{gCmsUserDeviceMgr, gDeviceManager};
use axum::extract::{Extension, Path};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde::Deserialize;
use std::net::IpAddr;

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TicketRequest {
    pub client_nonce: String,
    #[serde(default)]
    pub requested_permissions: Vec<String>,
}

fn permissions(requested: Vec<String>, allowed: &[&str]) -> Result<Vec<String>, CmsApiError> {
    if requested
        .iter()
        .any(|item| !allowed.contains(&item.as_str()))
    {
        return Err(CmsApiError::InvalidParams);
    }
    let source: Vec<String> = if requested.is_empty() {
        allowed.iter().map(|value| value.to_string()).collect()
    } else {
        requested
    };
    let mut result = Vec::new();
    for item in source {
        if !result.contains(&item) {
            result.push(item);
        }
    }
    Ok(result)
}

fn host_for_url(host: &str) -> String {
    match host.parse::<IpAddr>() {
        Ok(IpAddr::V6(_)) => format!("[{host}]"),
        _ => host.to_string(),
    }
}

pub async fn issue_device_ticket(
    Path(device_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<TicketRequest>,
) -> Result<Json<RespMessage<TicketResponse>>, CmsApiError> {
    if !gCmsUserDeviceMgr
        .user_has_device(&subject.uid, &device_id)
        .await?
    {
        return Err(CmsApiError::ResourceNotFound);
    }
    let device = gDeviceManager
        .query_device_by_id(device_id.clone())
        .await
        .map_err(|_| CmsApiError::ResourceNotFound)?;
    if !device.active {
        return Err(CmsApiError::DeviceOffline);
    }
    let (host, port) = device
        .get_render_endpoints()
        .into_iter()
        .next()
        .ok_or(CmsApiError::DeviceOffline)?;
    let granted = permissions(
        request.requested_permissions,
        &["view", "input", "clipboard", "file", "audio"],
    )?;
    let (raw, ticket) = ConnectionTicketManager::issue(
        "device",
        "user",
        &subject.uid,
        &subject.sid,
        &device_id,
        None,
        None,
        granted.clone(),
        request.client_nonce.clone(),
    )
    .await?;
    audit::record(
        "user",
        &subject.uid,
        "ticket_issue",
        "success",
        "device",
        &device_id,
        "",
    )
    .await;
    let launch_url = format!(
        "http://{}:{}/web_client/?deviceId={}#ticket={}&nonce={}",
        host_for_url(&host),
        port,
        device_id,
        raw,
        request.client_nonce
    );
    Ok(Json(ok_resp(TicketResponse {
        ticket: raw,
        launch_url,
        expires_at: ticket.expires_at,
        permissions: granted,
    })))
}

pub async fn issue_instance_ticket(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<TicketRequest>,
) -> Result<Json<RespMessage<TicketResponse>>, CmsApiError> {
    let instance = gAppScheduleManager
        .get_instance(&instance_id)
        .await
        .filter(|instance| {
            instance.owner_type == "user"
                && instance.owner_id == subject.uid
                && instance.state == InstanceState::Running
        })
        .ok_or(CmsApiError::ResourceNotFound)?;
    // Authorization is evaluated again at ticket issue time. Removing an ACL
    // grant must prevent new connections immediately, even when an older
    // instance owned by this user is still running.
    let app = gAppScheduleManager
        .get_application(&instance.app_id)
        .await
        .ok_or(CmsApiError::ResourceNotFound)?;
    if app.access_mode == crate::app_schedule::manager::AppAccessMode::Acl
        && !crate::identity::manager::IdentityManager::authorized_app_ids(&subject.uid)
            .await?
            .contains(&instance.app_id)
    {
        return Err(CmsApiError::ResourceNotFound);
    }
    let device = gDeviceManager
        .query_device_by_id(instance.device_id.clone())
        .await
        .map_err(|_| CmsApiError::ResourceNotFound)?;
    let host = device
        .get_render_endpoints()
        .into_iter()
        .next()
        .map(|(host, _)| host)
        .ok_or(CmsApiError::DeviceOffline)?;
    let granted = permissions(
        request.requested_permissions,
        &["view", "input", "clipboard", "file", "audio"],
    )?;
    let (raw, ticket) = ConnectionTicketManager::issue(
        "app_instance",
        "user",
        &subject.uid,
        &subject.sid,
        &instance.device_id,
        Some(app.app_id),
        Some(instance.instance_id.clone()),
        granted.clone(),
        request.client_nonce.clone(),
    )
    .await?;
    audit::record(
        "user",
        &subject.uid,
        "ticket_issue",
        "success",
        "app_instance",
        &instance.instance_id,
        "",
    )
    .await;
    let launch_url = format!(
        "http://{}:{}/web_client/?deviceId={}#ticket={}&nonce={}&instance={}",
        host_for_url(&host),
        instance.listen_port,
        instance.device_id,
        raw,
        request.client_nonce,
        instance.instance_id
    );
    Ok(Json(ok_resp(TicketResponse {
        ticket: raw,
        launch_url,
        expires_at: ticket.expires_at,
        permissions: granted,
    })))
}

pub async fn issue_guest_instance_ticket(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedGuest>,
    Json(request): Json<TicketRequest>,
) -> Result<Json<RespMessage<TicketResponse>>, CmsApiError> {
    let instance = gAppScheduleManager
        .get_instance(&instance_id)
        .await
        .filter(|instance| {
            instance.owner_type == "guest"
                && instance.owner_id == subject.guest_id
                && instance.state == InstanceState::Running
        })
        .ok_or(CmsApiError::ResourceNotFound)?;
    let app = gAppScheduleManager
        .get_application(&instance.app_id)
        .await
        .filter(|app| app.access_mode == crate::app_schedule::manager::AppAccessMode::Public)
        .ok_or(CmsApiError::ResourceNotFound)?;
    let device = gDeviceManager
        .query_device_by_id(instance.device_id.clone())
        .await
        .map_err(|_| CmsApiError::ResourceNotFound)?;
    let host = device
        .get_render_endpoints()
        .into_iter()
        .next()
        .map(|(host, _)| host)
        .ok_or(CmsApiError::DeviceOffline)?;
    // Anonymous sessions receive view/input only. More sensitive capabilities
    // require an authenticated user and an explicit device policy.
    let granted = permissions(request.requested_permissions, &["view", "input"])?;
    let (raw, ticket) = ConnectionTicketManager::issue(
        "app_instance",
        "guest",
        &subject.guest_id,
        &subject.sid,
        &instance.device_id,
        Some(app.app_id),
        Some(instance.instance_id.clone()),
        granted.clone(),
        request.client_nonce.clone(),
    )
    .await?;
    audit::record(
        "guest",
        &subject.guest_id,
        "ticket_issue",
        "success",
        "app_instance",
        &instance.instance_id,
        "",
    )
    .await;
    let launch_url = format!(
        "http://{}:{}/web_client/?deviceId={}#ticket={}&nonce={}&instance={}",
        host_for_url(&host),
        instance.listen_port,
        instance.device_id,
        raw,
        request.client_nonce,
        instance.instance_id
    );
    Ok(Json(ok_resp(TicketResponse {
        ticket: raw,
        launch_url,
        expires_at: ticket.expires_at,
        permissions: granted,
    })))
}

#[cfg(test)]
mod tests {
    use super::permissions;

    #[test]
    fn authenticated_permissions_are_deduplicated_and_validated() {
        let allowed = ["view", "input", "clipboard", "file", "audio"];
        let granted = permissions(
            vec!["view".into(), "clipboard".into(), "clipboard".into()],
            &allowed,
        )
        .unwrap();
        assert_eq!(granted, vec!["view", "clipboard"]);
        assert!(permissions(vec!["admin".into()], &allowed).is_err());
    }

    #[test]
    fn guest_permissions_cannot_escalate_to_sensitive_capabilities() {
        let allowed = ["view", "input"];
        assert!(permissions(vec!["view".into(), "file".into()], &allowed).is_err());
        assert_eq!(
            permissions(Vec::new(), &allowed).unwrap(),
            vec!["view", "input"]
        );
    }
}
