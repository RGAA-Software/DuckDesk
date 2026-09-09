use super::model::TicketGrant;
use crate::app_schedule::{gAppScheduleManager, manager::ApplicationType};
use crate::console_api_error::ConsoleApiError;
use crate::{gConsoleDatabase, gUserManager};
use mongodb::bson::doc;

/// Only reachable over the authenticated Service WebSocket. No new ticket,
/// Windows credential, renewal capability or remote control seat is issued.
pub async fn validate(
    device: &str,
    instance: &str,
    logical_session: &str,
) -> Result<TicketGrant, ConsoleApiError> {
    if !valid_binding(device, instance, logical_session) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let tickets = gConsoleDatabase.lock().await.connection_ticket();
    let ticket = tickets
        .lock()
        .await
        .find_one(doc! {
            "device_id": device, "instance_id": instance,
            "logical_session_id": logical_session, "kind": "app_instance", "permissions": "rdp",
        })
        .await
        .map_err(|_| ConsoleApiError::DatabaseError)?
        .ok_or(ConsoleApiError::TicketExpiredOrUsed)?;
    let now = px_base::get_current_timestamp();
    let sessions = gConsoleDatabase.lock().await.user_session();
    let session = sessions
        .lock()
        .await
        .find_one(doc! {
            "sid": &ticket.session_id, "subject_type": &ticket.subject_type,
            "subject_id": &ticket.subject_id, "revoked_at": null,
            "expires_at": { "$gt": now }, "absolute_expires_at": { "$gt": now },
        })
        .await
        .map_err(|_| ConsoleApiError::DatabaseError)?
        .ok_or(ConsoleApiError::AuthenticationRequired)?;
    if ticket.subject_type == "user" {
        let user = gUserManager
            .query_user_by_id(ticket.subject_id.clone())
            .await
            .map_err(|_| ConsoleApiError::AuthenticationRequired)?;
        if user.deleted || user.disabled || user.auth_version != session.auth_version {
            return Err(ConsoleApiError::AuthenticationRequired);
        }
    } else if ticket.subject_type == "guest" {
        if crate::gUserSessionManager
            .is_guest_blocked(Some(&ticket.subject_id), &session.ip_hash)
            .await?
        {
            return Err(ConsoleApiError::AuthenticationRequired);
        }
    } else {
        return Err(ConsoleApiError::AuthenticationRequired);
    }
    super::handler::validate_renewal_resource(&ticket).await?;
    let app = gAppScheduleManager
        .get_application(ticket.app_id.as_deref().unwrap_or_default())
        .await
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    if app.app_type != ApplicationType::Rdp {
        return Err(ConsoleApiError::ResourceNotFound);
    }
    Ok(TicketGrant {
        kind: "rdp_runtime".to_string(),
        device_id: device.to_string(),
        instance_id: Some(instance.to_string()),
        logical_session_id: logical_session.to_string(),
        // A runtime validation is not an admission or a renewal operation.
        ..Default::default()
    })
}

fn valid_binding(device: &str, instance: &str, logical: &str) -> bool {
    [device, instance, logical].iter().all(|value| {
        !value.is_empty()
            && value.len() <= 128
            && value
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn runtime_check_requires_bounded_exact_binding() {
        assert!(valid_binding("001190520", "inst-123", "logical-123"));
        assert!(!valid_binding("", "inst", "logical"));
        assert!(!valid_binding("node", "inst", ""));
        assert!(!valid_binding("node", "inst", "logical/other"));
        assert!(!valid_binding("node", "inst", &"x".repeat(129)));
    }
}
