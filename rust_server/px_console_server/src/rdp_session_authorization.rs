use crate::app_schedule::{gAppScheduleManager, manager::ApplicationType};
use crate::console_api_error::ConsoleApiError;
use crate::{gConsoleDatabase, gUserManager, gUserSessionManager};
use mongodb::bson::doc;

#[derive(Debug, Default)]
pub struct RdpSessionAuthorization {
    pub device_id: String,
    pub instance_id: String,
    pub subject_type: String,
    pub subject_id: String,
    pub logical_session_id: String,
}

pub async fn validate(device_id: &str, instance_id: &str, logical_session_id: &str) -> Result<RdpSessionAuthorization, ConsoleApiError> {
    if !valid_binding(device_id, instance_id, logical_session_id) {
        return Err(ConsoleApiError::InvalidParams);
    }
    let instance = gAppScheduleManager
        .get_instance(instance_id)
        .await
        .filter(|instance| instance.device_id == device_id && !instance.owner_session_id.is_empty())
        .ok_or(ConsoleApiError::ResourceNotFound)?;
    let app = gAppScheduleManager.get_application(&instance.app_id).await.ok_or(ConsoleApiError::ResourceNotFound)?;
    if app.app_type != ApplicationType::Rdp {
        return Err(ConsoleApiError::ResourceNotFound);
    }
    let now = px_base::get_current_timestamp();
    let sessions = gConsoleDatabase.lock().await.user_session();
    let session = sessions
        .lock()
        .await
        .find_one(doc! {
            "sid": &instance.owner_session_id,
            "subject_type": &instance.owner_type,
            "subject_id": &instance.owner_id,
            "revoked_at": null,
            "expires_at": { "$gt": now },
            "absolute_expires_at": { "$gt": now },
        })
        .await
        .map_err(|_| ConsoleApiError::DatabaseError)?
        .ok_or(ConsoleApiError::AuthenticationRequired)?;
    if instance.owner_type == "user" {
        let user = gUserManager
            .query_user_by_id(instance.owner_id.clone())
            .await
            .map_err(|_| ConsoleApiError::AuthenticationRequired)?;
        if user.deleted || user.disabled || user.auth_version != session.auth_version {
            return Err(ConsoleApiError::AuthenticationRequired);
        }
    } else if instance.owner_type == "guest" {
        if gUserSessionManager.is_guest_blocked(Some(&instance.owner_id), &session.ip_hash).await? {
            return Err(ConsoleApiError::AuthenticationRequired);
        }
    } else {
        return Err(ConsoleApiError::AuthenticationRequired);
    }
    Ok(RdpSessionAuthorization {
        device_id: device_id.to_string(),
        instance_id: instance_id.to_string(),
        subject_type: instance.owner_type,
        subject_id: instance.owner_id,
        logical_session_id: logical_session_id.to_string(),
    })
}

fn valid_binding(device: &str, instance: &str, logical: &str) -> bool {
    [device, instance, logical].iter().all(|value| {
        !value.is_empty()
            && value.len() <= 128
            && value.bytes().all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
    })
}

#[cfg(test)]
mod tests {
    use super::valid_binding;

    #[test]
    fn runtime_check_requires_bounded_exact_binding() {
        assert!(valid_binding("001190520", "inst-123", "logical-123"));
        assert!(!valid_binding("", "inst", "logical"));
        assert!(!valid_binding("node", "inst", ""));
        assert!(!valid_binding("node", "inst", "logical/other"));
        assert!(!valid_binding("node", "inst", &"x".repeat(129)));
    }
}
