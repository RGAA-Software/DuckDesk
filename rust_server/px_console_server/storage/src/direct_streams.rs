use crate::{control, node_lifecycle, NodeConnection, RuntimeEntitlement, StoreError};
use chrono::{DateTime, Utc};
use sqlx::PgPool;
use uuid::Uuid;

const QUOTA_LOCK: i64 = 5788347791197331458;

#[derive(Clone)]
pub struct DirectStreamStore {
    pub(crate) pool: PgPool,
}

impl DirectStreamStore {
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

    /// The authenticated node requests this only after Render has checked the device password.
    /// Renewals use the same stream ID and fail closed when the node or license changes.
    pub async fn admit(
        &self,
        node: &NodeConnection,
        stream_id: Uuid,
        entitlement: RuntimeEntitlement,
    ) -> Result<DateTime<Utc>, StoreError> {
        if stream_id.is_nil() {
            return Err(StoreError::InvalidInput);
        }
        if !entitlement.desktop {
            return Err(StoreError::LicenseRestriction);
        }
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let authority = node_lifecycle::authorize(&mut transaction, node).await?;
        sqlx::query("SELECT pg_advisory_xact_lock($1)")
            .bind(QUOTA_LOCK)
            .execute(&mut *transaction)
            .await?;

        sqlx::query("DELETE FROM pixels.direct_streams WHERE expires_at<=clock_timestamp()")
            .execute(&mut *transaction)
            .await?;
        let active_sessions: i64 = sqlx::query_scalar(
            "SELECT (SELECT count(*) FROM pixels.resource_sessions WHERE closed_at IS NULL) + \
             (SELECT count(*) FROM pixels.direct_streams WHERE expires_at>clock_timestamp())",
        )
        .fetch_one(&mut *transaction)
        .await?;
        let active_desktops: i64 = if entitlement.starter_mode_limit {
            sqlx::query_scalar(
                "SELECT (SELECT count(*) FROM pixels.resource_sessions \
                 WHERE target_kind='desktop' AND closed_at IS NULL) + \
                 (SELECT count(*) FROM pixels.direct_streams WHERE expires_at>clock_timestamp())",
            )
            .fetch_one(&mut *transaction)
            .await?
        } else {
            0
        };

        let renewed = sqlx::query_scalar::<_, DateTime<Utc>>(
            "UPDATE pixels.direct_streams SET expires_at=clock_timestamp()+interval '30 seconds' \
             WHERE id=$1 AND node_id=$2 AND node_generation=$3 AND control_epoch=$4 \
             AND expires_at>clock_timestamp() RETURNING expires_at",
        )
        .bind(stream_id)
        .bind(authority.id)
        .bind(authority.generation)
        .bind(authority.control_epoch)
        .fetch_optional(&mut *transaction)
        .await?;
        if let Some(expires_at) = renewed {
            if active_sessions > i64::from(entitlement.max_streams)
                || (entitlement.starter_mode_limit && active_desktops > 1)
            {
                return Err(StoreError::LicenseRestriction);
            }
            transaction.commit().await?;
            return Ok(expires_at);
        }
        if active_sessions >= i64::from(entitlement.max_streams) {
            return Err(StoreError::LicenseRestriction);
        }
        if entitlement.starter_mode_limit && active_desktops >= 1 {
            return Err(StoreError::LicenseRestriction);
        }
        let expires_at = sqlx::query_scalar::<_, DateTime<Utc>>(
            "INSERT INTO pixels.direct_streams(id,node_id,device_id,node_generation,control_epoch,expires_at) \
             SELECT $1,node.id,node.device_id,$3,$4,clock_timestamp()+interval '30 seconds' \
             FROM pixels.nodes AS node WHERE node.id=$2 AND node.state='ready' \
             AND NOT node.disabled AND NOT node.draining AND node.connection_hash IS NOT NULL \
             RETURNING expires_at",
        )
        .bind(stream_id)
        .bind(authority.id)
        .bind(authority.generation)
        .bind(authority.control_epoch)
        .fetch_optional(&mut *transaction)
        .await?
        .ok_or(StoreError::Rejected)?;
        transaction.commit().await?;
        Ok(expires_at)
    }

    pub async fn release(&self, node: &NodeConnection, stream_id: Uuid) -> Result<(), StoreError> {
        if stream_id.is_nil() {
            return Err(StoreError::InvalidInput);
        }
        let mut transaction = self.pool.begin().await?;
        control::write_gate(&mut transaction).await?;
        let authority = node_lifecycle::authorize(&mut transaction, node).await?;
        sqlx::query(
            "DELETE FROM pixels.direct_streams WHERE id=$1 AND node_id=$2 \
             AND node_generation=$3 AND control_epoch=$4",
        )
        .bind(stream_id)
        .bind(authority.id)
        .bind(authority.generation)
        .bind(authority.control_epoch)
        .execute(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(())
    }
}
