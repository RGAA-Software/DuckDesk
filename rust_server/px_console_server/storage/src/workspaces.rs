use crate::{
    command_model::CommandRow, control, instance_state, node_lifecycle,
    workspace_model::WorkspaceRow, workspace_vault::SecretBinding, NodeConnection, StoreError,
    TokenDigest, WorkspaceCommandLease, WorkspaceCredential, WorkspaceProfile, WorkspaceVault,
};
use sqlx::{PgConnection, PgPool};
use std::sync::Arc;
use uuid::Uuid;

#[derive(Clone)]
pub struct WorkspaceStore {
    pub(crate) pool: PgPool,
    pub(crate) deployment: Uuid,
    pub(crate) vault: Arc<WorkspaceVault>,
}
impl WorkspaceStore {
    #[cfg(feature = "pg-integration")]
    pub async fn connect(
        config: &px_pg::DatabaseConfig,
        deployment: Uuid,
        vault: Arc<WorkspaceVault>,
    ) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
            deployment,
            vault,
        })
    }
    #[cfg(feature = "pg-integration")]
    pub async fn close(&self) {
        self.pool.close().await;
    }
    async fn authorize(
        connection: &mut PgConnection,
        node: &NodeConnection,
        lease: WorkspaceCommandLease,
    ) -> Result<crate::instance_model::InstanceRow, StoreError> {
        let authority = node_lifecycle::authorize(connection, node).await?;
        let command = sqlx::query_file_as!(
            CommandRow,
            "queries/lock_instance_command.sql",
            lease.command_id
        )
        .fetch_optional(&mut *connection)
        .await?
        .ok_or(StoreError::Rejected)?;
        if command.node_id != authority.id
            || command.node_generation != authority.generation
            || command.control_epoch != authority.control_epoch
            || command.kind != "start"
            || command.state != "claimed"
            || command.expired
            || command.lease_id != Some(lease.lease_id)
        {
            return Err(StoreError::Rejected);
        }
        let record = instance_state::lock(connection, command.instance_id).await?;
        if record.revision != command.instance_revision
            || record.state != "starting"
            || record.desired_state != "running"
            || record.ended_at.is_some()
            || !instance_state::owner_authorized(connection, record.id).await?
        {
            return Err(StoreError::Rejected);
        }
        // A structural SQL join also requires RDP. Mode is not inferred from the caller.
        sqlx::query_file!(
            "queries/authorize_workspace_start.sql",
            lease.command_id,
            lease.lease_id,
            authority.id,
            authority.generation,
            authority.control_epoch
        )
        .fetch_optional(&mut *connection)
        .await?
        .ok_or(StoreError::Rejected)?;
        Ok(record)
    }
    fn credential(&self, row: &WorkspaceRow) -> Result<WorkspaceCredential, StoreError> {
        if row.schema_version != 1 {
            return Err(StoreError::RecoveryRequired);
        }
        let password = self
            .vault
            .open(&row.binding(self.deployment), &row.secret())?;
        Ok(WorkspaceCredential {
            workspace_id: row.id,
            account_name: row.account_name.clone(),
            windows_sid: row.windows_sid.clone(),
            credential_revision: row.credential_revision,
            password,
        })
    }
    /// Only a live authenticated RDP Start lease can cross this boundary.
    /// Commit the durable identity before a node is allowed to create any OS account.
    pub async fn credentials_for_start(
        &self,
        node: &NodeConnection,
        lease: WorkspaceCommandLease,
    ) -> Result<WorkspaceCredential, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let instance = Self::authorize(&mut tx, node, lease).await?;
        let existing = sqlx::query_file_as!(
            WorkspaceRow,
            "queries/find_workspace.sql",
            instance.application_id,
            instance.node_id
        )
        .fetch_optional(&mut *tx)
        .await?;
        let row = if let Some(row) = existing {
            if row.deployment_id != instance.deployment_id {
                return Err(StoreError::RecoveryRequired);
            }
            row
        } else {
            let id = Uuid::new_v4();
            let account = format!("pxrdp_{}", &Uuid::new_v4().simple().to_string()[..14]);
            let binding = SecretBinding {
                deployment: self.deployment,
                workspace: id,
                application: instance.application_id,
                placement: instance.deployment_id,
                node: instance.node_id,
                account,
                credential_revision: 1,
            };
            let sealed = self.vault.create(&binding)?;
            sqlx::query_file!(
                "queries/insert_workspace.sql",
                id,
                binding.application,
                binding.placement,
                binding.node,
                binding.account
            )
            .execute(&mut *tx)
            .await?;
            sqlx::query_file!(
                "queries/insert_workspace_secret.sql",
                id,
                sealed.key_id,
                sealed.nonce,
                sealed.ciphertext
            )
            .execute(&mut *tx)
            .await?;
            Self::audit(
                &mut tx,
                id,
                1,
                None,
                Some(instance.node_id),
                Some(lease.command_id),
                "created",
            )
            .await?;
            sqlx::query_file_as!(WorkspaceRow, "queries/get_workspace.sql", id)
                .fetch_one(&mut *tx)
                .await?
        };
        let result = self.credential(&row)?;
        tx.commit().await?;
        Ok(result)
    }
    /// The caller has just issued this exact Panel controller descriptor. This
    /// second gate binds the decrypted password to that still-live RDP frontend;
    /// management and non-Panel callers have no API that crosses this boundary.
    pub async fn credentials_for_frontend(
        &self,
        session_id: Uuid,
    ) -> Result<WorkspaceCredential, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let row = sqlx::query_file_as!(
            WorkspaceRow,
            "queries/authorize_workspace_frontend.sql",
            session_id
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        let result = self.credential(&row)?;
        tx.commit().await?;
        Ok(result)
    }
    /// A SID is evidence reported by the trusted node, not an authorization to adopt
    /// an unrelated Windows account. The node must verify its provisioning ownership.
    pub async fn confirm_account(
        &self,
        node: &NodeConnection,
        lease: WorkspaceCommandLease,
        workspace: Uuid,
        sid: &str,
    ) -> Result<(), StoreError> {
        if !crate::workspace_model::validate_sid(sid) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let instance = Self::authorize(&mut tx, node, lease).await?;
        let row = sqlx::query_file_as!(WorkspaceRow, "queries/get_workspace.sql", workspace)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if row.application_id != instance.application_id
            || row.node_id != instance.node_id
            || row.deployment_id != instance.deployment_id
        {
            return Err(StoreError::Rejected);
        }
        // Do not mark an unreadable credential ready.
        let _credential = self.credential(&row)?;
        match row.windows_sid.as_deref() {
            Some(existing) if existing == sid => {}
            Some(_) => return Err(StoreError::RecoveryRequired),
            None => {
                let updated = sqlx::query_file_as!(
                    WorkspaceProfile,
                    "queries/confirm_workspace.sql",
                    workspace,
                    sid
                )
                .fetch_one(&mut *tx)
                .await?;
                Self::audit(
                    &mut tx,
                    workspace,
                    updated.revision,
                    None,
                    Some(instance.node_id),
                    Some(lease.command_id),
                    "confirmed",
                )
                .await?;
            }
        }
        tx.commit().await?;
        Ok(())
    }
    pub async fn list_managed(
        &self,
        admin: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<WorkspaceProfile>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, admin, false).await?;
        let rows = sqlx::query_file_as!(
            WorkspaceProfile,
            "queries/list_workspaces.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(rows)
    }
    /// Re-encrypt the same password under the active key. Never reset an OS password
    /// or rename/recreate the persistent workspace during cryptographic rotation.
    pub async fn rewrap_managed(
        &self,
        admin: &TokenDigest,
        workspace: Uuid,
        revision: i64,
    ) -> Result<WorkspaceProfile, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let row = sqlx::query_file_as!(WorkspaceRow, "queries/get_workspace.sql", workspace)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if row.revision != revision {
            return Err(StoreError::Rejected);
        }
        let credential = self.credential(&row)?;
        let secret = self
            .vault
            .seal(&row.binding(self.deployment), &credential.password)?;
        let updated = sqlx::query_file_as!(
            WorkspaceProfile,
            "queries/rewrap_workspace.sql",
            workspace,
            secret.key_id,
            secret.nonce,
            secret.ciphertext
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::audit(
            &mut tx,
            workspace,
            updated.revision,
            Some(actor),
            None,
            None,
            "rewrapped",
        )
        .await?;
        tx.commit().await?;
        Ok(updated)
    }
    async fn audit(
        connection: &mut PgConnection,
        workspace: Uuid,
        revision: i64,
        actor: Option<Uuid>,
        node: Option<Uuid>,
        command: Option<Uuid>,
        action: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/workspace_audit.sql",
            Uuid::new_v4(),
            workspace,
            revision,
            actor,
            node,
            command,
            action
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
