use crate::{
    control, DeploymentConfiguration, DeploymentObservation, DeploymentProfile, NodeConnection,
    StoreError, TokenDigest,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct DeploymentStore {
    pub(crate) pool: PgPool,
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
        let result = sqlx::query_file_as!(
            DeploymentProfile,
            "queries/create_deployment.sql",
            Uuid::new_v4(),
            application,
            node,
            config.target.kind(),
            config.target.root(),
            config.gpu_key.as_deref(),
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
        if previous.install_root.as_deref() == config.target.root()
            && previous.gpu_key == config.gpu_key
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
