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
    /// Public endpoints for newly established iroh transports; existing sessions retain their paths.
    pub async fn available_iroh_relays(
        &self,
    ) -> Result<Vec<px_node_protocol::IrohRelayConfig>, StoreError> {
        let mut transaction = self.pool.begin().await?;
        control::read_gate(&mut transaction).await?;
        let endpoints: Vec<(String, i32, i32)> = sqlx::query_as(
            "SELECT public_host, public_port, iroh_qad_port FROM pixels.relay_nodes \
             WHERE iroh_qad_port IS NOT NULL AND state='ready' AND connection_hash IS NOT NULL \
             AND control_epoch=(SELECT epoch FROM pixels.control_runtime) \
             AND last_seen > clock_timestamp() - interval '30 seconds' \
             AND NOT disabled AND NOT desired_draining AND reported_draining=false AND deleted_at IS NULL \
             AND current_connections + 2 <= max_connections \
             ORDER BY current_connections::numeric / max_connections, id LIMIT 32"
        ).fetch_all(&mut *transaction).await?;
        transaction.commit().await?;
        endpoints
            .into_iter()
            .map(|(host, port, qad_port)| {
                let authority = if host.parse::<std::net::Ipv6Addr>().is_ok() {
                    format!("[{host}]")
                } else {
                    host
                };
                Ok(px_node_protocol::IrohRelayConfig {
                    url: format!("https://{authority}:{port}/"),
                    qad_port: Some(u16::try_from(qad_port).map_err(|_| StoreError::InvalidInput)?),
                })
            })
            .collect()
    }

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
            desired_draining: relay_node.desired_draining,
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
            validated.forwarded_bytes,
            report.iroh_qad_port.map(i32::from)
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
