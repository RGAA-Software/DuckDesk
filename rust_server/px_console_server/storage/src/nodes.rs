use crate::{
    control, NodeConfiguration, NodeConnection, NodeProduct, NodeProfile, NodeReport, RuntimeEpoch,
    StoreError, TokenDigest,
};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone)]
pub struct NodeStore {
    pub(crate) pool: PgPool,
}
impl NodeStore {
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
    /// Composition-root startup only, before opening HTTP/WS or sending commands. A new
    /// process never inherits a persisted Ready snapshot. Not an administrative HTTP action.
    pub async fn begin_runtime(&self) -> Result<RuntimeEpoch, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let epoch = sqlx::query_file_scalar!("queries/begin_control_runtime.sql", Uuid::new_v4())
            .fetch_one(&mut *tx)
            .await?;
        sqlx::query_file!("queries/reset_node_connections.sql")
            .execute(&mut *tx)
            .await?;
        crate::node_lifecycle::invalidate(&mut tx, None).await?;
        tx.commit().await?;
        Ok(RuntimeEpoch(epoch))
    }
    pub async fn create(
        &self,
        admin: &TokenDigest,
        device: Uuid,
        product: NodeProduct,
        credential: &TokenDigest,
        max_instances: u32,
    ) -> Result<NodeProfile, StoreError> {
        NodeConfiguration {
            draining: false,
            disabled: false,
            max_instances,
        }
        .validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let capacity = i32::try_from(max_instances).map_err(|_| StoreError::InvalidInput)?;
        let node = sqlx::query_file_as!(
            NodeProfile,
            "queries/create_node.sql",
            Uuid::new_v4(),
            device,
            product.name(),
            credential.0.as_slice(),
            capacity
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        Self::audit(&mut tx, actor, node.id, node.revision, "created").await?;
        tx.commit().await?;
        Ok(node)
    }
    pub async fn list_managed(
        &self,
        admin: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<NodeProfile>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, admin, false).await?;
        let result = sqlx::query_file_as!(
            NodeProfile,
            "queries/managed_nodes.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn open_connection(
        &self,
        epoch: RuntimeEpoch,
        credential: &TokenDigest,
        connection_key: &TokenDigest,
    ) -> Result<NodeConnection, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let node = sqlx::query_file_as!(
            NodeProfile,
            "queries/open_node_connection.sql",
            credential.0.as_slice(),
            connection_key.0.as_slice(),
            epoch.0
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        crate::node_lifecycle::invalidate(&mut tx, Some(node.id)).await?;
        tx.commit().await?;
        Ok(NodeConnection {
            id: node.id,
            generation: node.generation,
            epoch,
            key: connection_key.clone(),
        })
    }
    pub async fn report(
        &self,
        connection: &NodeConnection,
        report: &NodeReport,
    ) -> Result<NodeProfile, StoreError> {
        let (sequence, host) = report.validate()?;
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let previous = sqlx::query_file_as!(NodeProfile, "queries/lock_node.sql", connection.id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        let node = sqlx::query_file_as!(
            NodeProfile,
            "queries/node_report.sql",
            connection.key.0.as_slice(),
            connection.generation,
            connection.epoch.0,
            sequence,
            i64::from(report.product_version_code),
            host,
            i32::from(report.desktop_port),
            i32::from(report.application_port_start),
            i32::from(report.application_port_end),
            report.game_hook,
            report.webview,
            report.rdp
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        if previous.state == "ready" && node.state == "reconciling" {
            crate::node_lifecycle::invalidate(&mut tx, Some(node.id)).await?;
        }
        tx.commit().await?;
        Ok(node)
    }
    pub async fn close_connection(&self, connection: &NodeConnection) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let changed = sqlx::query_file!(
            "queries/close_node_connection.sql",
            connection.key.0.as_slice(),
            connection.generation,
            connection.epoch.0
        )
        .execute(&mut *tx)
        .await?
        .rows_affected();
        if changed != 1 {
            return Err(StoreError::Rejected);
        }
        crate::node_lifecycle::invalidate(&mut tx, Some(connection.id)).await?;
        tx.commit().await?;
        Ok(())
    }
    pub async fn configure(
        &self,
        admin: &TokenDigest,
        id: Uuid,
        revision: i64,
        config: NodeConfiguration,
    ) -> Result<NodeProfile, StoreError> {
        config.validate()?;
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let previous = Self::lock(&mut tx, id, revision).await?;
        let capacity = i32::try_from(config.max_instances).map_err(|_| StoreError::InvalidInput)?;
        if previous.draining == config.draining
            && previous.disabled == config.disabled
            && previous.max_instances == capacity
        {
            tx.commit().await?;
            return Ok(previous);
        }
        let node = sqlx::query_file_as!(
            NodeProfile,
            "queries/configure_node.sql",
            id,
            config.draining,
            config.disabled,
            capacity
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::audit(&mut tx, actor, id, node.revision, "configured").await?;
        if previous.disabled != config.disabled {
            crate::node_lifecycle::invalidate(&mut tx, Some(id)).await?;
        }
        tx.commit().await?;
        Ok(node)
    }
    pub async fn rotate_key(
        &self,
        admin: &TokenDigest,
        id: Uuid,
        revision: i64,
        credential: &TokenDigest,
    ) -> Result<NodeProfile, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        Self::lock(&mut tx, id, revision).await?;
        let node = sqlx::query_file_as!(
            NodeProfile,
            "queries/rotate_node_key.sql",
            id,
            credential.0.as_slice()
        )
        .fetch_one(&mut *tx)
        .await?;
        Self::audit(&mut tx, actor, id, node.revision, "key_rotated").await?;
        crate::node_lifecycle::invalidate(&mut tx, Some(id)).await?;
        tx.commit().await?;
        Ok(node)
    }
    pub async fn delete(
        &self,
        admin: &TokenDigest,
        id: Uuid,
        revision: i64,
    ) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        Self::lock(&mut tx, id, revision).await?;
        let next = sqlx::query_file_scalar!("queries/delete_node.sql", id)
            .fetch_one(&mut *tx)
            .await?;
        Self::audit(&mut tx, actor, id, next, "deleted").await?;
        crate::node_lifecycle::invalidate(&mut tx, Some(id)).await?;
        tx.commit().await?;
        Ok(())
    }
    async fn lock(
        connection: &mut PgConnection,
        id: Uuid,
        revision: i64,
    ) -> Result<NodeProfile, StoreError> {
        let node = sqlx::query_file_as!(NodeProfile, "queries/lock_node.sql", id)
            .fetch_optional(connection)
            .await?
            .ok_or(StoreError::Rejected)?;
        if revision < 1 || revision != node.revision {
            return Err(StoreError::Rejected);
        }
        Ok(node)
    }
    async fn audit(
        connection: &mut PgConnection,
        actor: Uuid,
        node: Uuid,
        revision: i64,
        action: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/node_audit.sql",
            Uuid::new_v4(),
            node,
            actor,
            revision,
            action
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
