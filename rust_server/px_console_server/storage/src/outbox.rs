use crate::{ControlStore, StoreError};
use uuid::Uuid;

/// Backend-only dispatcher contract. Never expose lease tokens to an HTTP caller.
#[derive(Debug, Clone, sqlx::FromRow)]
pub struct AuthorizationEvent {
    pub id: Uuid,
    pub user_id: Uuid,
    pub session_id: Option<Uuid>,
    pub authorization_revision: i64,
    pub reason: String,
    pub lease_id: Uuid,
    pub attempts: i32,
}
#[derive(Clone, Copy)]
pub enum DeliveryFailure {
    Unavailable,
    Rejected,
}
impl ControlStore {
    /// Fresh per-claim token prevents a late ack from acknowledging a reclaimed delivery.
    pub async fn claim_events(&self, limit: u32) -> Result<Vec<AuthorizationEvent>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        Ok(sqlx::query_file_as!(
            AuthorizationEvent,
            "queries/claim_authorization_events.sql",
            i64::from(limit),
            Uuid::new_v4()
        )
        .fetch_all(&self.pool)
        .await?)
    }
    pub async fn complete_event(&self, id: Uuid, lease: Uuid) -> Result<(), StoreError> {
        let rows = sqlx::query_file!("queries/complete_authorization_event.sql", id, lease)
            .execute(&self.pool)
            .await?
            .rows_affected();
        if rows != 1 {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
    pub async fn retry_event(
        &self,
        id: Uuid,
        lease: Uuid,
        delay_seconds: u32,
        failure: DeliveryFailure,
    ) -> Result<(), StoreError> {
        if !(1..=300).contains(&delay_seconds) {
            return Err(StoreError::InvalidInput);
        }
        let reason = match failure {
            DeliveryFailure::Unavailable => "unavailable",
            DeliveryFailure::Rejected => "rejected",
        };
        let rows = sqlx::query_file!(
            "queries/retry_authorization_event.sql",
            id,
            lease,
            f64::from(delay_seconds),
            reason
        )
        .execute(&self.pool)
        .await?
        .rows_affected();
        if rows != 1 {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
}
