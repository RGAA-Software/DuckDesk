use crate::{
    control, RelayNodeConfiguration, RelayNodeConnection, RelayNodeProfile, RelayNodeReport,
    RelayNodeSpec, RuntimeEpoch, StoreError, TokenDigest,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct RelayNodeStore {
    pub(crate) pool: PgPool,
}

impl RelayNodeStore {
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
        spec: &RelayNodeSpec,
        credential: &TokenDigest,
    ) -> Result<RelayNodeProfile, StoreError> {
        let public_host = spec.validate()?;
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let actor = control::authorize(&mut transaction, admin, true).await?;
        let relay_node = sqlx::query_file_as!(
            RelayNodeProfile,
            "queries/create_relay_node.sql",
            Uuid::new_v4(),
            spec.name,
            public_host,
            i32::from(spec.public_port),
            credential.0.as_slice()
        )
        .fetch_one(&mut *transaction)
        .await?;
        Self::audit(
            &mut transaction,
            actor,
            relay_node.id,
            relay_node.revision,
            "created",
        )
        .await?;
        transaction.commit().await?;
        Ok(relay_node)
    }

    pub async fn list_managed(
        &self,
        admin: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<RelayNodeProfile>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        control::authorize(&mut transaction, admin, false).await?;
        let relay_nodes = sqlx::query_file_as!(
            RelayNodeProfile,
            "queries/managed_relay_nodes.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(relay_nodes)
    }

    pub async fn configure(
        &self,
        admin: &TokenDigest,
        relay_node_id: Uuid,
        revision: i64,
        configuration: RelayNodeConfiguration,
    ) -> Result<RelayNodeProfile, StoreError> {
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let actor = control::authorize(&mut transaction, admin, true).await?;
        let previous = Self::lock(&mut transaction, relay_node_id, revision).await?;
        if previous.desired_draining == configuration.draining
            && previous.disabled == configuration.disabled
        {
            transaction.commit().await?;
            return Ok(previous);
        }
        let relay_node = sqlx::query_file_as!(
            RelayNodeProfile,
            "queries/configure_relay_node.sql",
            relay_node_id,
            configuration.draining,
            configuration.disabled
        )
        .fetch_one(&mut *transaction)
        .await?;
        Self::audit(
            &mut transaction,
            actor,
            relay_node.id,
            relay_node.revision,
            "configured",
        )
        .await?;
        transaction.commit().await?;
        Ok(relay_node)
    }

    pub async fn open_connection(
        &self,
        epoch: RuntimeEpoch,
        credential: &TokenDigest,
        connection_key: &TokenDigest,
    ) -> Result<RelayNodeConnection, StoreError> {
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        let relay_node = sqlx::query_file_as!(
            RelayNodeProfile,
            "queries/open_relay_connection.sql",
            credential.0.as_slice(),
            connection_key.0.as_slice(),
            epoch.0
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        transaction.commit().await?;
        Ok(RelayNodeConnection {
            id: relay_node.id,
            generation: relay_node.generation,
            epoch,
            key: connection_key.clone(),
        })
    }

    pub async fn report(
        &self,
        connection: &RelayNodeConnection,
        report: &RelayNodeReport,
    ) -> Result<RelayNodeProfile, StoreError> {
        let validated = report.validate()?;
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        let relay_node = sqlx::query_file_as!(
            RelayNodeProfile,
            "queries/report_relay_node.sql",
            connection.key.0.as_slice(),
            connection.generation,
            connection.epoch.0,
            validated.sequence,
            report.draining,
            validated.product_version_code,
            validated.max_connections,
            validated.current_connections,
            validated.max_rooms,
            validated.current_rooms,
            validated.uploaded_bytes,
            validated.forwarded_bytes
        )
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        transaction.commit().await?;
        Ok(relay_node)
    }

    pub async fn close_connection(
        &self,
        connection: &RelayNodeConnection,
    ) -> Result<(), StoreError> {
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        let changed = sqlx::query_file!(
            "queries/close_relay_connection.sql",
            connection.key.0.as_slice(),
            connection.generation,
            connection.epoch.0
        )
        .execute(&mut *transaction)
        .await?
        .rows_affected();
        if changed != 1 {
            return Err(StoreError::Rejected);
        }
        transaction.commit().await?;
        Ok(())
    }

    async fn lock(
        connection: &mut PgConnection,
        relay_node_id: Uuid,
        revision: i64,
    ) -> Result<RelayNodeProfile, StoreError> {
        let relay_node = sqlx::query_file_as!(
            RelayNodeProfile,
            "queries/lock_relay_node.sql",
            relay_node_id
        )
        .fetch_optional(connection)
        .await?
        .ok_or(StoreError::Rejected)?;
        if revision < 1 || revision != relay_node.revision {
            return Err(StoreError::Rejected);
        }
        Ok(relay_node)
    }

    async fn audit(
        connection: &mut PgConnection,
        actor: Uuid,
        relay_node_id: Uuid,
        revision: i64,
        action: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/relay_node_audit.sql",
            Uuid::new_v4(),
            relay_node_id,
            actor,
            revision,
            action
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
