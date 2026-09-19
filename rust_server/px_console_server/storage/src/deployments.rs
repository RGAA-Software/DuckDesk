use crate::{
    control, DeploymentConfiguration, DeploymentObservation, DeploymentProfile, NodeConnection,
    NodeDeploymentAssignment, NodeDeploymentPreparation, StoreError, TokenDigest,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct DeploymentStore {
    pub(crate) pool: PgPool,
}

#[derive(sqlx::FromRow)]
struct NodeDeploymentRow {
    id: Uuid,
    application_id: Uuid,
    kind: String,
    install_root: Option<String>,
    executable_relative: Option<String>,
    gpu_key: Option<String>,
    disabled: bool,
    deployment_revision: i64,
    application_revision: i64,
}

impl NodeDeploymentRow {
    fn assignment(self) -> Result<NodeDeploymentAssignment, StoreError> {
        let invalid = StoreError::Database(px_pg::DatabaseError::Operation);
        let preparation = match self.kind.as_str() {
            "game_hook" => NodeDeploymentPreparation::GameHook {
                install_root: self.install_root.ok_or(invalid)?,
                executable_relative: self.executable_relative.ok_or(invalid)?,
                gpu_key: self.gpu_key,
            },
            "webview" if self.install_root.is_none() && self.executable_relative.is_none() => {
                NodeDeploymentPreparation::Webview {
                    gpu_key: self.gpu_key,
                }
            }
            "rdp" if self.install_root.is_none() && self.executable_relative.is_none() => {
                NodeDeploymentPreparation::Rdp {
                    gpu_key: self.gpu_key,
                }
            }
            _ => return Err(invalid),
        };
        Ok(NodeDeploymentAssignment {
            id: self.id,
            application_id: self.application_id,
            deployment_revision: self.deployment_revision,
            application_revision: self.application_revision,
            disabled: self.disabled,
            preparation,
        })
    }
}
impl DeploymentStore {
    #[cfg(feature = "pg-integration")]
    pub async fn connect(
        config: &px_pg::DatabaseConfig,
        deployment: Uuid,
    ) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
        })
    }
    #[cfg(feature = "pg-integration")]
    pub async fn close(&self) {
        self.pool.close().await;
    }
    pub async fn create(
        &self,
        admin: &TokenDigest,
        application: Uuid,
        node: Uuid,
        config: &DeploymentConfiguration,
    ) -> Result<DeploymentProfile, StoreError> {
        config.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let capacity = i32::try_from(config.capacity).map_err(|_| StoreError::InvalidInput)?;
        let gpu_values = config
            .gpu_profile
            .as_ref()
            .map(crate::GpuResourceProfile::database_values);
        let result = sqlx::query_file_as!(
            DeploymentProfile,
            "queries/create_deployment.sql",
            Uuid::new_v4(),
            application,
            node,
            config.target.kind(),
            config.target.root(),
            config.gpu_key.as_deref(),
            gpu_values.map(|values| values.0),
            gpu_values.map(|values| values.1),
            gpu_values.map(|values| values.2),
            gpu_values.map(|values| values.3),
            gpu_values.map(|values| values.4),
            gpu_values.map(|values| values.5),
            capacity,
            config.disabled
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        Self::audit(&mut tx, actor, result.id, result.revision, "created").await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_managed(
        &self,
        admin: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<DeploymentProfile>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, admin, false).await?;
        let result = sqlx::query_file_as!(
            DeploymentProfile,
            "queries/list_deployments.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_node(
        &self,
        connection: &NodeConnection,
        after: Option<Uuid>,
        limit: u16,
    ) -> Result<Vec<NodeDeploymentAssignment>, StoreError> {
        if !(1..=50).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let node = crate::node_lifecycle::authorize(&mut tx, connection).await?;
        let rows = sqlx::query_file_as!(
            NodeDeploymentRow,
            "queries/list_node_deployments.sql",
            node.id,
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        let assignments = rows
            .into_iter()
            .map(NodeDeploymentRow::assignment)
            .collect::<Result<Vec<_>, _>>()?;
        tx.commit().await?;
        Ok(assignments)
    }
    pub async fn configure(
        &self,
        admin: &TokenDigest,
        id: Uuid,
        revision: i64,
        config: &DeploymentConfiguration,
    ) -> Result<DeploymentProfile, StoreError> {
        config.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let previous = sqlx::query_file_as!(DeploymentProfile, "queries/lock_deployment.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if revision < 1 || previous.revision != revision || previous.kind != config.target.kind() {
            return Err(StoreError::Rejected);
        }
        let app_revision = sqlx::query_file_scalar!(
            "queries/deployment_application_revision.sql",
            previous.application_id
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        let capacity = i32::try_from(config.capacity).map_err(|_| StoreError::InvalidInput)?;
        let gpu_values = config
            .gpu_profile
            .as_ref()
            .map(crate::GpuResourceProfile::database_values);
        if previous.install_root.as_deref() == config.target.root()
            && previous.gpu_key == config.gpu_key
            && previous.gpu_memory_bytes == gpu_values.map(|values| values.0)
            && previous.gpu_compute_per_mille == gpu_values.map(|values| values.1)
            && previous.gpu_encoder_per_mille == gpu_values.map(|values| values.2)
            && previous.gpu_memory_reserve_bytes == gpu_values.map(|values| values.3)
            && previous.gpu_compute_limit_per_mille == gpu_values.map(|values| values.4)
            && previous.gpu_encoder_limit_per_mille == gpu_values.map(|values| values.5)
            && previous.capacity == capacity
            && previous.disabled == config.disabled
            && previous.application_revision == app_revision
        {
            tx.commit().await?;
            return Ok(previous);
        }
        let result = sqlx::query_file_as!(
            DeploymentProfile,
            "queries/configure_deployment.sql",
            id,
            config.target.root(),
            config.gpu_key.as_deref(),
            gpu_values.map(|values| values.0),
            gpu_values.map(|values| values.1),
            gpu_values.map(|values| values.2),
            gpu_values.map(|values| values.3),
            gpu_values.map(|values| values.4),
            gpu_values.map(|values| values.5),
            capacity,
            config.disabled,
            config.target.kind()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        Self::audit(&mut tx, actor, id, result.revision, "configured").await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn report(
        &self,
        connection: &NodeConnection,
        id: Uuid,
        observation: &DeploymentObservation,
    ) -> Result<DeploymentProfile, StoreError> {
        let sequence = observation.validate()?;
        let (state, reason) = observation.status.fields();
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let result = sqlx::query_file_as!(
            DeploymentProfile,
            "queries/report_deployment.sql",
            id,
            connection.key.0.as_slice(),
            connection.generation,
            connection.epoch.0,
            observation.endpoint_revision,
            observation.deployment_revision,
            observation.application_revision,
            sequence,
            state,
            reason
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        tx.commit().await?;
        Ok(result)
    }
    async fn audit(
        connection: &mut PgConnection,
        actor: Uuid,
        id: Uuid,
        revision: i64,
        action: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/deployment_audit.sql",
            Uuid::new_v4(),
            id,
            actor,
            revision,
            action
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
