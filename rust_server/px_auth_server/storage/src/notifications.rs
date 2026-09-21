use crate::{AuthError, LicenseStore};
use uuid::Uuid;

/// Internal dispatcher payload. Lease tokens must never be exposed through the public Auth API.
#[derive(Debug, Clone, sqlx::FromRow)]
pub struct LicenseNotification {
    pub id: Uuid,
    pub license_id: Uuid,
    pub revision: i64,
    pub action: String,
    pub deployment_id: Uuid,
    pub product: String,
    pub distribution: String,
    pub release_namespace: String,
    pub oem_id: Option<String>,
    pub machine_sha256: String,
    pub wire: Option<String>,
    pub lease_id: Uuid,
    pub attempts: i32,
}

#[derive(Clone, Copy)]
pub enum NotificationFailure {
    Unavailable,
    Rejected,
}

impl LicenseStore {
    /// Records authenticated contact from the exact signed-license consumer. This is
    /// intentionally separate from dispatcher leases: no lease token crosses the API.
    /// A late dispatcher ACK is fenced after this update clears its lease.
    pub async fn acknowledge_consumer_contact(&self, license_id: Uuid) -> Result<u64, AuthError> {
        if license_id.is_nil() {
            return Err(AuthError::Invalid);
        }
        Ok(sqlx::query(
            "UPDATE pixels.license_notification_outbox notification \
             SET delivered_at=clock_timestamp(),lease_id=NULL,lease_until=NULL,last_error=NULL \
             WHERE notification.license_id=$1 AND notification.delivered_at IS NULL \
             AND notification.revision <= COALESCE((SELECT license.revision FROM pixels.licenses license WHERE license.id=$1),0)",
        )
        .bind(license_id)
        .execute(&self.pool)
        .await?
        .rows_affected())
    }

    /// Claims only the oldest undelivered revision for each license.
    pub async fn claim_notifications(
        &self,
        limit: u32,
    ) -> Result<Vec<LicenseNotification>, AuthError> {
        if !(1..=100).contains(&limit) {
            return Err(AuthError::Invalid);
        }
        Ok(sqlx::query_file_as!(
            LicenseNotification,
            "queries/claim_license_notifications.sql",
            i64::from(limit),
            Uuid::new_v4()
        )
        .fetch_all(&self.pool)
        .await?)
    }

    pub async fn complete_notification(&self, id: Uuid, lease_id: Uuid) -> Result<(), AuthError> {
        let affected = sqlx::query_file!("queries/complete_license_notification.sql", id, lease_id)
            .execute(&self.pool)
            .await?
            .rows_affected();
        if affected != 1 {
            return Err(AuthError::Rejected);
        }
        Ok(())
    }

    pub async fn retry_notification(
        &self,
        id: Uuid,
        lease_id: Uuid,
        delay_seconds: u32,
        failure: NotificationFailure,
    ) -> Result<(), AuthError> {
        if !(1..=300).contains(&delay_seconds) {
            return Err(AuthError::Invalid);
        }
        let failure = match failure {
            NotificationFailure::Unavailable => "unavailable",
            NotificationFailure::Rejected => "rejected",
        };
        let affected = sqlx::query_file!(
            "queries/retry_license_notification.sql",
            id,
            lease_id,
            f64::from(delay_seconds),
            failure
        )
        .execute(&self.pool)
        .await?
        .rows_affected();
        if affected != 1 {
            return Err(AuthError::Rejected);
        }
        Ok(())
    }
}
