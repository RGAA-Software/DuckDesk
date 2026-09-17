use crate::{
    control, node_lifecycle, session_model::SessionRow, FrontendRetirement, NodeConnection,
    ResourceSession, ResourceSessionStore, StoreError, TokenDigest,
};
use chrono::{DateTime, Utc};
use uuid::Uuid;

#[derive(Debug, Clone, serde::Serialize)]
pub struct FrontendGrant {
    pub session: ResourceSession,
    /// Hard fail-closed lease; the transport adapter must disconnect or renew before it expires.
    pub expires_at: DateTime<Utc>,
    /// Node uses request-start monotonic time + this remaining duration, never its
    /// wall clock or response-arrival time. Network delay must not extend the lease.
    pub valid_for_ms: u32,
}
#[derive(Debug, Clone, serde::Serialize, sqlx::FromRow)]
pub struct ExpectedFrontend {
    pub id: Uuid,
    pub revision: i64,
    pub state: String,
}
impl ResourceSessionStore {
    /// Includes never-delivered and unknown frontend identities so reconnect cleanup
    /// cannot silently forget reservations. This is not proof of a live connection.
    pub async fn list_node(
        &self,
        node: &NodeConnection,
    ) -> Result<Vec<ExpectedFrontend>, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        let rows = sqlx::query_file_as!(
            ExpectedFrontend,
            "queries/node_resource_sessions.sql",
            authority.id
        )
        .fetch_all(&mut *tx)
        .await?;
        if rows.len() > 128 {
            return Err(StoreError::Rejected);
        }
        tx.commit().await?;
        Ok(rows)
    }
    /// Online node admission and periodic renewal share exactly the same current-policy check.
    /// A node must not infer a grant merely from a descriptor's host/port.
    pub async fn admit_frontend(
        &self,
        node: &NodeConnection,
        id: Uuid,
        revision: i64,
        token: &TokenDigest,
    ) -> Result<FrontendGrant, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        let mut row = Self::lock(&mut tx, id).await?;
        if row.node_id != authority.id
            || row.node_generation != authority.generation
            || row.control_epoch != authority.control_epoch
        {
            return Err(StoreError::Rejected);
        }
        let lease = sqlx::query_file!(
            "queries/resource_descriptor_valid.sql",
            id,
            revision,
            token.0.as_slice()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        if !(1..=30000).contains(&lease.valid_for_ms) {
            return Err(StoreError::Rejected);
        }
        Self::live_endpoint(&mut tx, &row).await?;
        if row.state == "pending" {
            row = sqlx::query_file_as!(SessionRow, "queries/confirm_resource_session.sql", id)
                .fetch_one(&mut *tx)
                .await?;
            Self::event(&mut tx, &row, "connected").await?;
        }
        let result = FrontendGrant {
            session: row.view()?,
            expires_at: lease.expires_at,
            valid_for_ms: u32::try_from(lease.valid_for_ms).map_err(|_| StoreError::Rejected)?,
        };
        tx.commit().await?;
        Ok(result)
    }
    /// Explicit node-scoped cleanup challenge. Persist the inclusive session revision fence,
    /// drain in-flight admissions and retire all channels for this exact logical frontend
    /// BEFORE acknowledging. Never stop the application or log off its Windows session.
    pub async fn begin_retirement(
        &self,
        node: &NodeConnection,
        id: Uuid,
    ) -> Result<FrontendRetirement, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        let row = Self::lock(&mut tx, id).await?;
        if row.node_id != authority.id || row.closed_at.is_some() {
            return Err(StoreError::Rejected);
        }
        let row = Self::change(&mut tx, &row, "closing").await?;
        let challenge = Uuid::new_v4();
        let deadline = sqlx::query_file_scalar!(
            "queries/start_frontend_retirement.sql",
            id,
            authority.id,
            challenge,
            row.revision,
            authority.generation,
            authority.control_epoch
        )
        .fetch_one(&mut *tx)
        .await?;
        let result = FrontendRetirement {
            session_id: id,
            challenge_id: challenge,
            reject_through_revision: row.revision,
            node_generation: authority.generation,
            control_epoch: authority.control_epoch,
            deadline,
        };
        tx.commit().await?;
        Ok(result)
    }
    pub async fn finish_retirement(
        &self,
        node: &NodeConnection,
        id: Uuid,
        challenge: Uuid,
    ) -> Result<ResourceSession, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let authority = node_lifecycle::authorize(&mut tx, node).await?;
        let row = Self::lock(&mut tx, id).await?;
        if row.node_id != authority.id || !matches!(row.state.as_str(), "closing" | "closed") {
            return Err(StoreError::Rejected);
        }
        let fence_revision = if row.closed_at.is_some() {
            row.revision - 1
        } else {
            row.revision
        };
        sqlx::query_file_scalar!(
            "queries/complete_frontend_retirement.sql",
            id,
            challenge,
            fence_revision,
            authority.generation,
            authority.control_epoch
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        let result = if row.closed_at.is_some() {
            row.view()?
        } else {
            crate::file_transfers::invalidate(&mut tx, Some(authority.id), Some(id)).await?;
            crate::activity::invalidate(&mut tx, Some(authority.id), Some(id)).await?;
            Self::change(&mut tx, &row, "closed").await?.view()?
        };
        tx.commit().await?;
        Ok(result)
    }
}
