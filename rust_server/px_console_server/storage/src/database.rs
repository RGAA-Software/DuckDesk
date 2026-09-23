//! One validated pool per Console process; repositories are narrow capability handles,
//! not independently connected services. Only this owner shuts down the shared pool.
use crate::{
    ApplicationStore, ControlStore, DeploymentStore, DeviceStore, GroupStore, GuestStore,
    IdentityStore, InstanceStore, NodeStore, RelayNodeStore, StoreError, TelemetryAlertStore,
    WorkspaceStore, WorkspaceVault,
};
use px_pg::DatabaseConfig;
use sqlx::PgPool;
use std::sync::Arc;
use uuid::Uuid;

pub struct ConsoleDatabase {
    pool: PgPool,
    deployment: Uuid,
    workspace_vault: Arc<WorkspaceVault>,
}
#[derive(Debug, Clone, Copy, serde::Serialize)]
pub struct PoolStatus {
    pub connections: u32,
    pub idle: usize,
    pub closed: bool,
}
impl ConsoleDatabase {
    pub async fn connect(
        config: &DatabaseConfig,
        deployment: Uuid,
        workspace_vault: Arc<WorkspaceVault>,
    ) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
            deployment,
            workspace_vault,
        })
    }
    pub async fn ready(&self) -> Result<(), StoreError> {
        px_pg::runtime_readiness(
            &self.pool,
            px_pg::Service::Console,
            self.deployment,
            &crate::MIGRATIONS,
        )
        .await?;
        Ok(())
    }
    pub async fn initialized(&self) -> Result<bool, StoreError> {
        Ok(sqlx::query_file_scalar!("queries/console_initialized.sql")
            .fetch_one(&self.pool)
            .await?)
    }
    pub fn pool_status(&self) -> PoolStatus {
        PoolStatus {
            connections: self.pool.size(),
            idle: self.pool.num_idle(),
            closed: self.pool.is_closed(),
        }
    }
    pub async fn close(&self) {
        self.pool.close().await;
    }
    pub fn identity(&self) -> IdentityStore {
        IdentityStore {
            pool: self.pool.clone(),
        }
    }
    pub fn resource_sessions(&self) -> crate::ResourceSessionStore {
        crate::ResourceSessionStore {
            pool: self.pool.clone(),
        }
    }
    pub fn file_transfers(&self) -> crate::FileTransferStore {
        crate::FileTransferStore {
            pool: self.pool.clone(),
        }
    }
    pub fn recordings(&self) -> crate::RecordingStore {
        crate::RecordingStore {
            pool: self.pool.clone(),
        }
    }
    pub fn activity(&self) -> crate::ActivityStore {
        crate::ActivityStore {
            pool: self.pool.clone(),
        }
    }
    pub fn updates(&self) -> crate::UpdateStore {
        crate::UpdateStore {
            pool: self.pool.clone(),
        }
    }
    pub fn recording_cache(&self) -> crate::RecordingCacheStore {
        crate::RecordingCacheStore {
            pool: self.pool.clone(),
            deployment: self.deployment,
        }
    }
    pub fn saved_connections(&self) -> crate::SavedConnectionStore {
        crate::SavedConnectionStore {
            pool: self.pool.clone(),
        }
    }
    pub fn control(&self) -> ControlStore {
        ControlStore {
            pool: self.pool.clone(),
        }
    }
    pub fn groups(&self) -> GroupStore {
        GroupStore {
            pool: self.pool.clone(),
        }
    }
    pub fn devices(&self) -> DeviceStore {
        DeviceStore {
            pool: self.pool.clone(),
        }
    }
    pub fn applications(&self) -> ApplicationStore {
        ApplicationStore {
            pool: self.pool.clone(),
        }
    }
    pub fn guests(&self) -> GuestStore {
        GuestStore {
            pool: self.pool.clone(),
        }
    }
    pub fn nodes(&self) -> NodeStore {
        NodeStore {
            pool: self.pool.clone(),
        }
    }
    pub fn relay_nodes(&self) -> RelayNodeStore {
        RelayNodeStore {
            pool: self.pool.clone(),
        }
    }
    pub fn telemetry_alerts(&self) -> TelemetryAlertStore {
        TelemetryAlertStore {
            pool: self.pool.clone(),
        }
    }
    pub fn deployments(&self) -> DeploymentStore {
        DeploymentStore {
            pool: self.pool.clone(),
        }
    }
    pub fn instances(&self) -> InstanceStore {
        InstanceStore {
            pool: self.pool.clone(),
        }
    }
    pub fn workspaces(&self) -> WorkspaceStore {
        WorkspaceStore {
            pool: self.pool.clone(),
            deployment: self.deployment,
            vault: self.workspace_vault.clone(),
        }
    }
}
