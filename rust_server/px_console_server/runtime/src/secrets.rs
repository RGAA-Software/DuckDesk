use crate::{error::ApiError, GuestAdmission};
use px_console_store::{WorkspaceKey, WorkspaceVault};
use std::{path::PathBuf, sync::Arc, time::Duration};
use uuid::Uuid;
use zeroize::Zeroizing;

pub struct WorkspaceKeyFile {
    pub id: Uuid,
    pub path: PathBuf,
}

/// Private runtime material loaded before the database pool, lease or listener is created.
/// It has no generation fallback and does not implement Debug or serialization.
pub struct RuntimeSecrets {
    vault: Arc<WorkspaceVault>,
    guests: GuestAdmission,
}

impl RuntimeSecrets {
    pub async fn load(
        deployment: Uuid,
        active_workspace_key: Uuid,
        workspace_keys: Vec<WorkspaceKeyFile>,
        guest_source_key: PathBuf,
        guests_enabled: bool,
        guest_lifetime: Duration,
    ) -> Result<Self, ApiError> {
        if deployment.is_nil()
            || active_workspace_key.is_nil()
            || workspace_keys.is_empty()
            || workspace_keys.len() > 32
        {
            return Err(ApiError::Invalid);
        }
        let mut keys = Vec::with_capacity(workspace_keys.len());
        for source in workspace_keys {
            if source.id.is_nil() {
                return Err(ApiError::Invalid);
            }
            keys.push(WorkspaceKey {
                id: source.id,
                bytes: load_key(source.path).await?,
            });
        }
        let vault = Arc::new(WorkspaceVault::new(active_workspace_key, keys)?);
        let guests =
            GuestAdmission::load(deployment, guest_source_key, guests_enabled, guest_lifetime)
                .await?;
        Ok(Self { vault, guests })
    }

    pub fn into_parts(self) -> (Arc<WorkspaceVault>, GuestAdmission) {
        (self.vault, self.guests)
    }
}

async fn load_key(path: PathBuf) -> Result<Zeroizing<[u8; 32]>, ApiError> {
    let bytes = tokio::task::spawn_blocking(move || px_private_files::private::read_private(&path))
        .await
        .map_err(|_| ApiError::Internal)?
        .map_err(|_| ApiError::Unavailable)?;
    if bytes.len() != 32 {
        return Err(ApiError::Invalid);
    }
    let mut key = Zeroizing::new([0; 32]);
    key.copy_from_slice(&bytes);
    Ok(key)
}
