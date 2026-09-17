use crate::{ApplicationStore, DeliveryFailure, StoreError};
use uuid::Uuid;

/// Internal dispatcher only. Immutable fields are also the administration audit.
#[derive(Debug, Clone, sqlx::FromRow)]
pub struct ApplicationEvent {
    pub id: Uuid,
    pub application_id: Uuid,
    pub revision: i64,
    pub access_revision: i64,
    pub kind: String,
    pub lease_id: Uuid,
    pub attempts: i32,
}
impl ApplicationStore {
    pub async fn claim_events(&self, limit: u32) -> Result<Vec<ApplicationEvent>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        Ok(sqlx::query_file_as!(
            ApplicationEvent,
            "queries/claim_application_events.sql",
            i64::from(limit),
            Uuid::new_v4()
        )
        .fetch_all(&self.pool)
        .await?)
    }
    pub async fn complete_event(&self, id: Uuid, lease: Uuid) -> Result<(), StoreError> {
        let count = sqlx::query_file!("queries/complete_application_event.sql", id, lease)
            .execute(&self.pool)
            .await?
            .rows_affected();
        if count != 1 {
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
        let count = sqlx::query_file!(
            "queries/retry_application_event.sql",
            id,
            lease,
            f64::from(delay_seconds),
            reason
        )
        .execute(&self.pool)
        .await?
        .rows_affected();
        if count != 1 {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
}
