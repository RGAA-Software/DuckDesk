use crate::{control, ClientType, StoreError, TokenDigest};
use chrono::{DateTime, Utc};
use sqlx::{PgConnection, PgPool};
use std::time::Duration;
use uuid::Uuid;

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct GuestSession {
    pub id: Uuid,
    pub client_type: String,
    pub created_at: DateTime<Utc>,
    pub expires_at: DateTime<Utc>,
    pub revoked_at: Option<DateTime<Utc>>,
    pub revision: i64,
}
/// A deployment-keyed HMAC of a trusted transport source, never a raw IP, user identity,
/// bearer token, or value accepted from a request DTO. Not serialized or displayed.
#[derive(Clone)]
pub struct OriginFingerprint(pub(crate) [u8; 32]);
impl OriginFingerprint {
    pub fn from_hmac_sha256(bytes: [u8; 32]) -> Self {
        Self(bytes)
    }
}
impl std::fmt::Debug for OriginFingerprint {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("OriginFingerprint(<redacted>)")
    }
}
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct ManagedGuest {
    pub id: Uuid,
    pub client_type: String,
    pub created_at: DateTime<Utc>,
    pub expires_at: DateTime<Utc>,
    pub revoked_at: Option<DateTime<Utc>>,
    pub revision: i64,
    pub blocked: bool,
}
#[derive(Clone, Copy)]
pub enum GuestBlockReason {
    Operator,
    Abuse,
}
#[derive(Clone)]
pub struct GuestStore {
    pub(crate) pool: PgPool,
}
impl GuestStore {
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
    /// Internal issuance boundary after rate limiting. Always a new identity; there is no
    /// user/device/IP fallback, refresh of expired identities, or administrator guest type.
    pub async fn issue(
        &self,
        source: &OriginFingerprint,
        token: &TokenDigest,
        client: ClientType,
        lifetime: Duration,
    ) -> Result<GuestSession, StoreError> {
        if client == ClientType::AdminWeb
            || lifetime.is_zero()
            || lifetime.subsec_nanos() != 0
            || lifetime.as_secs() > 86400
        {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let result = sqlx::query_file_as!(
            GuestSession,
            "queries/issue_guest.sql",
            Uuid::new_v4(),
            token.0.as_slice(),
            client.name(),
            lifetime.as_secs() as f64,
            source.0.as_slice()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn list_managed(
        &self,
        admin: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ManagedGuest>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        control::authorize(&mut tx, admin, false).await?;
        let result = sqlx::query_file_as!(
            ManagedGuest,
            "queries/managed_guests.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(result)
    }
    /// Explicit wider-scope administrative action; blocks all guest sessions of the selected
    /// source, but never user sessions. Short-lived source bans have immutable history.
    pub async fn block_source(
        &self,
        admin: &TokenDigest,
        id: Uuid,
        revision: i64,
        lifetime: Duration,
        reason: GuestBlockReason,
    ) -> Result<GuestSession, StoreError> {
        if revision < 1
            || lifetime.is_zero()
            || lifetime.subsec_nanos() != 0
            || lifetime.as_secs() > 86400
        {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let previous = sqlx::query_file!("queries/lock_guest.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if previous.revision != revision {
            return Err(StoreError::Rejected);
        }
        let reason = match reason {
            GuestBlockReason::Operator => "operator",
            GuestBlockReason::Abuse => "abuse",
        };
        sqlx::query_file!(
            "queries/insert_guest_source_block.sql",
            Uuid::new_v4(),
            id,
            &previous.source_hash,
            actor,
            reason,
            lifetime.as_secs() as f64
        )
        .execute(&mut *tx)
        .await?;
        sqlx::query_file!(
            "queries/revoke_guest_source.sql",
            &previous.source_hash,
            id,
            actor
        )
        .execute(&mut *tx)
        .await?;
        let result = sqlx::query_file_as!(GuestSession, "queries/guest_profile.sql", id)
            .fetch_one(&mut *tx)
            .await?;
        tx.commit().await?;
        Ok(result)
    }
    pub async fn authenticate(
        &self,
        token: &TokenDigest,
        client: ClientType,
    ) -> Result<GuestSession, StoreError> {
        let mut tx = self.pool.begin().await?;
        control::read_gate(&mut tx).await?;
        let result = Self::authorize(&mut tx, token, client).await?;
        tx.commit().await?;
        Ok(result)
    }
    pub(crate) async fn authorize(
        connection: &mut PgConnection,
        token: &TokenDigest,
        client: ClientType,
    ) -> Result<GuestSession, StoreError> {
        sqlx::query_file_as!(
            GuestSession,
            "queries/authenticate_guest.sql",
            token.0.as_slice(),
            client.name()
        )
        .fetch_optional(connection)
        .await?
        .ok_or(StoreError::Rejected)
    }
    /// Token owner can revoke only that exact guest session. Repeat logout remains safe.
    pub async fn logout(&self, token: &TokenDigest, client: ClientType) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let previous = sqlx::query_file_as!(
            GuestSession,
            "queries/lock_guest_token.sql",
            token.0.as_slice(),
            client.name()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        if previous.revoked_at.is_none() {
            let result =
                sqlx::query_file_as!(GuestSession, "queries/revoke_guest.sql", previous.id)
                    .fetch_one(&mut *tx)
                    .await?;
            Self::event(&mut tx, &result, None, "logout").await?;
        }
        tx.commit().await?;
        Ok(())
    }
    pub async fn block(
        &self,
        admin: &TokenDigest,
        id: Uuid,
        revision: i64,
        reason: GuestBlockReason,
    ) -> Result<GuestSession, StoreError> {
        if revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        control::write_gate(&mut tx).await?;
        let actor = control::authorize(&mut tx, admin, true).await?;
        let previous = sqlx::query_file!("queries/lock_guest.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if previous.revision != revision {
            return Err(StoreError::Rejected);
        }
        if previous.blocked {
            tx.commit().await?;
            return Ok(GuestSession {
                id: previous.id,
                client_type: previous.client_type,
                created_at: previous.created_at,
                expires_at: previous.expires_at,
                revoked_at: previous.revoked_at,
                revision: previous.revision,
            });
        }
        let reason = match reason {
            GuestBlockReason::Operator => "operator",
            GuestBlockReason::Abuse => "abuse",
        };
        sqlx::query_file!("queries/block_guest.sql", id, actor, reason)
            .execute(&mut *tx)
            .await?;
        let result = sqlx::query_file_as!(GuestSession, "queries/revoke_guest.sql", id)
            .fetch_one(&mut *tx)
            .await?;
        Self::event(&mut tx, &result, Some(actor), "blocked").await?;
        tx.commit().await?;
        Ok(result)
    }
    async fn event(
        connection: &mut PgConnection,
        guest: &GuestSession,
        actor: Option<Uuid>,
        reason: &str,
    ) -> Result<(), StoreError> {
        sqlx::query_file!(
            "queries/guest_event.sql",
            Uuid::new_v4(),
            guest.id,
            actor,
            guest.revision,
            reason
        )
        .execute(connection)
        .await?;
        Ok(())
    }
}
