use crate::app_schedule::gAppScheduleManager;
use crate::console_api_error::ConsoleApiError;
use crate::connection_ticket::manager::ConnectionTicketManager;
use crate::connection_ticket::model::{ConnectionTicket, TicketRenewResponse, TicketResponse};
use crate::event::audit;
use crate::{gConsoleSettings, gDeviceManager, gRtcConfigManager};
use crate::identity::access_policy::{
    guest_can_access_app, subject_owns_running_instance, user_can_access_app,
};
use crate::user::session::{AuthenticatedGuest, AuthenticatedUser};
use axum::extract::{ConnectInfo, Extension, Path};
use axum::Json;
use px_base::{ok_resp, RespMessage};
use serde::Deserialize;
use std::net::{IpAddr, SocketAddr};

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TicketRequest {
    pub client_nonce: String,
    #[serde(default)]
    pub requested_permissions: Vec<String>,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TicketRenewRequest {
    pub renewal_token: String,
    pub client_nonce: String,
}

fn permissions(requested: Vec<String>, allowed: &[&str]) -> Result<Vec<String>, ConsoleApiError> {
    if requested
        .iter()
        .any(|item| !allowed.contains(&item.as_str()))
    {
        return Err(ConsoleApiError::InvalidParams);
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

async fn issue_rtc_config(
    ticket: &ConnectionTicket,
) -> Result<crate::rtc::model::RtcSessionIceConfig, ConsoleApiError> {
    gRtcConfigManager
        .issue_session_config(&ticket.session_id)
        .await
        .map_err(|error| {
            tracing::error!(%error, "issue RTC ICE credentials failed");
            ConsoleApiError::InternalError
        })
}

async fn relay_endpoint() -> (String, u16) {
    // Read both fields under one guard. Two `gConsoleSettings.lock()` calls in
    // the same struct literal can keep the first temporary guard alive until
    // the statement ends and self-deadlock while acquiring the second one.
    let settings = gConsoleSettings.lock().await;
    (settings.server_w3c_ip.clone(), settings.relay_port)
}

async fn validate_renewal_resource(ticket: &ConnectionTicket) -> Result<(), ConsoleApiError> {
    match (ticket.subject_type.as_str(), ticket.kind.as_str()) {
        ("user", "device") => {
            let device = gDeviceManager
                .query_device_by_id(ticket.device_id.clone())
                .await
                .map_err(|_| ConsoleApiError::ResourceNotFound)?;
            if !device.active {
                return Err(ConsoleApiError::DeviceOffline);
            }
        }
        ("user" | "guest", "app_instance") => {
            let instance_id = ticket
                .instance_id
                .as_deref()
                .ok_or(ConsoleApiError::ResourceNotFound)?;
            let instance = gAppScheduleManager
                .get_instance(instance_id)
                .await
                .filter(|instance| {
                    subject_owns_running_instance(
                        instance,
                        &ticket.subject_type,
                        &ticket.subject_id,
                    ) && instance.device_id == ticket.device_id
                })
                .ok_or(ConsoleApiError::ResourceNotFound)?;
            let app = gAppScheduleManager
                .get_application(&instance.app_id)
                .await
                .ok_or(ConsoleApiError::ResourceNotFound)?;
            if ticket.subject_type == "guest" {
                if !guest_can_access_app(&app.access_mode) {
                    return Err(ConsoleApiError::ResourceNotFound);
                }
            } else {
                let acl_ids = if app.access_mode == crate::app_schedule::manager::AppAccessMode::Acl
                {
                    crate::identity::manager::IdentityManager::authorized_app_ids(
                        &ticket.subject_id,
                    )
                    .await?
                } else {
                    Default::default()
                };
                if !user_can_access_app(&app.access_mode, &instance.app_id, &acl_ids) {
                    return Err(ConsoleApiError::ResourceNotFound);
                }
            }
        }
        _ => return Err(ConsoleApiError::ResourceNotFound),
    }
    Ok(())
}

/// Cross-origin browser renewal endpoint. Authentication is the rotating,
/// hashed renewal capability itself; no Console cookie or CSRF secret is accepted.
pub async fn renew_connection_ticket(
    ConnectInfo(remote): ConnectInfo<SocketAddr>,
    Json(request): Json<TicketRenewRequest>,
) -> Result<Json<RespMessage<TicketRenewResponse>>, ConsoleApiError> {
    crate::user::rate_limit::check(format!("ticket-renew-ip:{}", remote.ip()), 120, 60 * 1000)?;
    if request.renewal_token.is_empty() || request.renewal_token.len() > 128 {
        return Err(ConsoleApiError::InvalidParams);
    }
    let existing =
        ConnectionTicketManager::lookup_renewal(&request.renewal_token, &request.client_nonce)
            .await?;
    validate_renewal_resource(&existing).await?;
    let (ticket, renewal_token, renewed) =
        ConnectionTicketManager::renew(&request.renewal_token, &request.client_nonce).await?;
    audit::record(
        &renewed.subject_type,
        &renewed.subject_id,
        "ticket_renew",
        "success",
        &renewed.kind,
        renewed.instance_id.as_deref().unwrap_or(&renewed.device_id),
        "",
    )
    .await;
    Ok(Json(ok_resp(TicketRenewResponse {
        ticket,
        renewal_token,
        expires_at: renewed.expires_at,
        rtc_ice_config: issue_rtc_config(&renewed).await?,
        permissions: renewed.permissions,
    })))
}

pub async fn issue_device_ticket(
    Path(device_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<TicketRequest>,
) -> Result<Json<RespMessage<TicketResponse>>, ConsoleApiError> {
    let device = gDeviceManager
        .query_device_by_id(device_id.clone())
        .await
        .map_err(|_| ConsoleApiError::ResourceNotFound)?;
    if !device.active {
        return Err(ConsoleApiError::DeviceOffline);
    }
    let (host, port) = device
        .get_render_endpoints()
        .into_iter()
        .next()
        .ok_or(ConsoleApiError::DeviceOffline)?;
    let granted = permissions(
        request.requested_permissions,
        &["view", "input", "clipboard", "file", "audio"],
    )?;
    let (raw, renewal_token, ticket) = ConnectionTicketManager::issue(
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
        "http://{}:{}/web_client/?deviceId={}#ticket={}&renew={}&nonce={}&perms={}",
        host_for_url(&host),
        port,
        device_id,
        raw,
        renewal_token,
        request.client_nonce,
        granted.join(",")
    );
    let (relay_host, relay_port) = relay_endpoint().await;
    Ok(Json(ok_resp(TicketResponse {
        ticket: raw,
        renewal_token,
        launch_url,
        expires_at: ticket.expires_at,
        permissions: granted,
        rtc_ice_config: issue_rtc_config(&ticket).await?,
        relay_host,
        relay_port,
        signal_device_id: format!("server_{device_id}"),
    })))
}

pub async fn issue_instance_ticket(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedUser>,
    Json(request): Json<TicketRequest>,
) -> Result<Json<RespMessage<TicketResponse>>, ConsoleApiError> {
    let instance = gAppScheduleManager
        .get_instance(&instance_id)
        .await
        .filter(|instance| subject_owns_running_instance(instance, "user", &subject.uid))
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    // Authorization is evaluated again at ticket issue time. Removing an ACL
    // grant must prevent new connections immediately, even when an older
    // instance owned by this user is still running.
    let app = gAppScheduleManager
        .get_application(&instance.app_id)
        .await
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    let acl_ids = if app.access_mode == crate::app_schedule::manager::AppAccessMode::Acl {
        crate::identity::manager::IdentityManager::authorized_app_ids(&subject.uid).await?
    } else {
        Default::default()
    };
    if !user_can_access_app(&app.access_mode, &instance.app_id, &acl_ids) {
        return Err(ConsoleApiError::ResourceNotFound);
    }
    let device = gDeviceManager
        .query_device_by_id(instance.device_id.clone())
        .await
        .map_err(|_| ConsoleApiError::ResourceNotFound)?;
    let host = device
        .get_render_endpoints()
        .into_iter()
        .next()
        .map(|(host, _)| host)
        .ok_or(ConsoleApiError::DeviceOffline)?;
    let granted = permissions(
        request.requested_permissions,
        &["view", "input", "clipboard", "file", "audio"],
    )?;
    let (raw, renewal_token, ticket) = ConnectionTicketManager::issue(
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
        "http://{}:{}/web_client/?deviceId={}#ticket={}&renew={}&nonce={}&instance={}&perms={}",
        host_for_url(&host),
        instance.listen_port,
        instance.device_id,
        raw,
        renewal_token,
        request.client_nonce,
        instance.instance_id,
        granted.join(",")
    );
    let (relay_host, relay_port) = relay_endpoint().await;
    Ok(Json(ok_resp(TicketResponse {
        ticket: raw,
        renewal_token,
        launch_url,
        expires_at: ticket.expires_at,
        permissions: granted,
        rtc_ice_config: issue_rtc_config(&ticket).await?,
        relay_host,
        relay_port,
        signal_device_id: format!(
            "server_{}__instance__{}",
            instance.device_id, instance.instance_id
        ),
    })))
}

pub async fn issue_guest_instance_ticket(
    Path(instance_id): Path<String>,
    Extension(subject): Extension<AuthenticatedGuest>,
    Json(request): Json<TicketRequest>,
) -> Result<Json<RespMessage<TicketResponse>>, ConsoleApiError> {
    let instance = gAppScheduleManager
        .get_instance(&instance_id)
        .await
        .filter(|instance| subject_owns_running_instance(instance, "guest", &subject.guest_id))
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    let app = gAppScheduleManager
        .get_application(&instance.app_id)
        .await
        .filter(|app| guest_can_access_app(&app.access_mode))
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    let device = gDeviceManager
        .query_device_by_id(instance.device_id.clone())
        .await
        .map_err(|_| ConsoleApiError::ResourceNotFound)?;
    let host = device
        .get_render_endpoints()
        .into_iter()
        .next()
        .map(|(host, _)| host)
        .ok_or(ConsoleApiError::DeviceOffline)?;
    // Anonymous sessions receive view/input only. More sensitive capabilities
    // require an authenticated user and an explicit device policy.
    let granted = permissions(request.requested_permissions, &["view", "input"])?;
    let (raw, renewal_token, ticket) = ConnectionTicketManager::issue(
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
        "http://{}:{}/web_client/?deviceId={}#ticket={}&renew={}&nonce={}&instance={}&perms={}",
        host_for_url(&host),
        instance.listen_port,
        instance.device_id,
        raw,
        renewal_token,
        request.client_nonce,
        instance.instance_id,
        granted.join(",")
    );
    let (relay_host, relay_port) = relay_endpoint().await;
    Ok(Json(ok_resp(TicketResponse {
        ticket: raw,
        renewal_token,
        launch_url,
        expires_at: ticket.expires_at,
        permissions: granted,
        rtc_ice_config: issue_rtc_config(&ticket).await?,
        relay_host,
        relay_port,
        signal_device_id: format!(
            "server_{}__instance__{}",
            instance.device_id, instance.instance_id
        ),
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
