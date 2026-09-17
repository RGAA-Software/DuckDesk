use crate::app_schedule::manager::{AppInstance, Application, ApplicationType};
use crate::console_api_error::ConsoleApiError;
use crate::{gConsoleDatabase, gConsoleServiceConnMgr, gConsoleSettings};
use serde::Serialize;

/// Serialized only in an authenticated HTTPS response and never written to logs or URLs.
#[derive(Clone)]
pub struct RdpPassword(pub zeroize::Zeroizing<String>);

impl std::fmt::Debug for RdpPassword {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("[REDACTED]")
    }
}

impl Serialize for RdpPassword {
    fn serialize<S: serde::Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(self.0.as_str())
    }
}

#[derive(Debug, Clone, Serialize)]
pub struct RdpClientConfiguration {
    pub schema: u32,
    pub workspace_id: String,
    pub instance_id: String,
    pub node_id: String,
    pub device_id: String,
    pub account_name: String,
    pub credential_version: u32,
    pub password: RdpPassword,
    pub domain: String,
    pub proxy_certificate_sha256: String,
}

pub async fn client_configuration(
    app: &Application,
    instance: &AppInstance,
    view_only: bool,
    client_capability: &str,
) -> Result<Option<RdpClientConfiguration>, ConsoleApiError> {
    if app.app_type != ApplicationType::Rdp {
        return Ok(None);
    }
    if view_only || client_capability != "windows-rdp-v1" {
        return Err(ConsoleApiError::InvalidParams);
    }

    let connection = gConsoleServiceConnMgr
        .get_conn(instance.device_id.clone())
        .await?;
    let (domain, certificate) = {
        let connection = connection.lock().await;
        if !connection.rdp_available {
            return Err(ConsoleApiError::DeviceOffline);
        }
        (
            connection.rdp_domain.clone(),
            connection.rdp_proxy_certificate_sha256.clone(),
        )
    };
    let collection = gConsoleDatabase
        .lock()
        .await
        .c_rdp_workspace
        .clone()
        .ok_or(ConsoleApiError::DatabaseError)?;
    let key_path = gConsoleSettings.lock().await.rdp_master_key_path.clone();
    let vault = crate::app_schedule::rdp_workspace::RdpWorkspaceVault::load(std::path::Path::new(
        &key_path,
    ))
    .map_err(|_| ConsoleApiError::InternalError)?;
    let credential = vault
        .open_existing(
            &collection,
            &instance.app_id,
            &instance.node_id,
            &instance.device_id,
        )
        .await
        .map_err(|_| ConsoleApiError::InternalError)?;
    Ok(Some(RdpClientConfiguration {
        schema: 1,
        workspace_id: credential.record.workspace_id,
        instance_id: instance.instance_id.clone(),
        node_id: instance.node_id.clone(),
        device_id: instance.device_id.clone(),
        account_name: credential.record.account_name,
        credential_version: credential.record.credential_version,
        password: RdpPassword(credential.password),
        domain,
        proxy_certificate_sha256: certificate,
    }))
}

#[cfg(test)]
mod tests {
    use super::RdpPassword;

    #[test]
    fn secret_debug_is_redacted_but_protected_serialization_is_explicit() {
        let secret = RdpPassword(zeroize::Zeroizing::new("test-secret-password".to_string()));
        assert_eq!(format!("{secret:?}"), "[REDACTED]");
        assert!(!format!("{secret:?}").contains("test-secret"));
        assert_eq!(
            serde_json::to_string(&secret).unwrap(),
            "\"test-secret-password\""
        );
    }
}
