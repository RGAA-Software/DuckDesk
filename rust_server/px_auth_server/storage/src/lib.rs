//! Auth-owned transactional license issuance. No HTTP, Mongo or process globals.
mod issuance;
mod model;
mod notifications;
mod operators;

pub use model::{Activation, Customer, IssueRequest, IssuedLicense, LicenseTerms};
pub use notifications::{LicenseNotification, NotificationFailure};
pub use operators::{
    bootstrap_author, AuthorSummary, OperatorCredential, OperatorProfile, OperatorSession,
    OperatorStore,
};
use px_license::LicenseSigner;
use px_pg::{DatabaseConfig, DatabaseError};
use sqlx::{PgConnection, PgPool};
use std::sync::Arc;
use uuid::Uuid;

pub static MIGRATIONS: sqlx::migrate::Migrator = sqlx::migrate!("../migrations");
#[derive(Debug, thiserror::Error)]
pub enum AuthError {
    #[error("invalid authorization input")]
    Invalid,
    #[error("authorization denied or revoked")]
    Rejected,
    #[error("authorization revision or request conflict")]
    Conflict,
    #[error(transparent)]
    Database(#[from] DatabaseError),
}
impl From<sqlx::Error> for AuthError {
    fn from(error: sqlx::Error) -> Self {
        Self::Database(error.into())
    }
}
#[derive(Clone)]
pub struct LicenseStore {
    pool: PgPool,
    signer: Arc<LicenseSigner>,
}
impl LicenseStore {
    pub async fn ready(&self, deployment: Uuid) -> Result<(), AuthError> {
        px_pg::runtime_readiness(&self.pool, px_pg::Service::Auth, deployment, &MIGRATIONS).await?;
        Ok(())
    }
    pub async fn database_time(&self) -> Result<i64, AuthError> {
        Ok(sqlx::query_file_scalar!("queries/database_time.sql")
            .fetch_one(&self.pool)
            .await?
            .timestamp())
    }
    pub async fn recovery_generation(&self) -> Result<Uuid, AuthError> {
        Ok(sqlx::query_file_scalar!("queries/recovery_generation.sql")
            .fetch_one(&self.pool)
            .await?)
    }
    pub async fn list_licenses(
        &self,
        token: &[u8; 32],
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<LicenseSummary>, AuthError> {
        if !(1..=100).contains(&limit) {
            return Err(AuthError::Invalid);
        }
        let mut tx = self.pool.begin().await?;
        Self::authorize(&mut tx, token, false).await?;
        let rows = sqlx::query_file_as!(
            LicenseSummary,
            "queries/list_licenses.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(rows)
    }
    pub async fn connect(
        config: &DatabaseConfig,
        deployment: Uuid,
        signer: Arc<LicenseSigner>,
    ) -> Result<Self, AuthError> {
        let pool = config
            .connect_runtime(px_pg::Service::Auth, deployment, &MIGRATIONS)
            .await?;
        Ok(Self { pool, signer })
    }
    pub async fn close(&self) {
        self.pool.close().await;
    }
    // Hold the author/session locks to commit: logout/role revision cannot race a write past authorization.
    async fn authorize(
        connection: &mut PgConnection,
        digest: &[u8; 32],
        write: bool,
    ) -> Result<Uuid, AuthError> {
        let author = sqlx::query_file!("queries/authorize_operator.sql", digest.as_slice())
            .fetch_optional(connection)
            .await?;
        match author {
            Some(author) if author.role == "admin" || (!write && author.role == "visitor") => {
                Ok(author.id)
            }
            _ => Err(AuthError::Rejected),
        }
    }
    pub async fn create_customer(
        &self,
        token: &[u8; 32],
        name: &str,
        remark: &str,
    ) -> Result<Customer, AuthError> {
        if name.trim() != name
            || !(1..=128).contains(&name.chars().count())
            || name.chars().any(char::is_control)
            || remark.chars().count() > 1024
            || remark.contains('\0')
        {
            return Err(AuthError::Invalid);
        }
        let mut tx = self.pool.begin().await?;
        Self::authorize(&mut tx, token, true).await?;
        let customer = sqlx::query_file_as!(
            Customer,
            "queries/create_customer.sql",
            Uuid::new_v4(),
            name,
            name.to_lowercase(),
            remark
        )
        .fetch_one(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(customer)
    }
    pub async fn list_customers(
        &self,
        token: &[u8; 32],
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<Customer>, AuthError> {
        if !(1..=100).contains(&limit) {
            return Err(AuthError::Invalid);
        }
        let mut tx = self.pool.begin().await?;
        Self::authorize(&mut tx, token, false).await?;
        let items = sqlx::query_file_as!(
            Customer,
            "queries/list_customers.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(items)
    }
    pub async fn revoke(
        &self,
        token: &[u8; 32],
        license_id: Uuid,
        revision: i64,
    ) -> Result<i64, AuthError> {
        if revision < 1 {
            return Err(AuthError::Invalid);
        }
        let mut tx = self.pool.begin().await?;
        let actor = Self::authorize(&mut tx, token, true).await?;
        let next: Option<i64> =
            sqlx::query_file_scalar!("queries/revoke_license.sql", license_id, revision)
                .fetch_optional(&mut *tx)
                .await?;
        let next = next.ok_or(AuthError::Conflict)?;
        sqlx::query_file!(
            "queries/audit_revocation.sql",
            Uuid::new_v4(),
            actor,
            license_id,
            next
        )
        .execute(&mut *tx)
        .await?;
        sqlx::query_file!(
            "queries/insert_license_notification.sql",
            Uuid::new_v4(),
            license_id,
            next,
            "revoked",
            None::<Uuid>
        )
        .execute(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(next)
    }
    /// Auth online verification adds revocation/current revision to cryptographic verification at the caller.
    pub async fn current(
        &self,
        license_id: Uuid,
        revision: i64,
    ) -> Result<IssuedLicense, AuthError> {
        sqlx::query_file_as!(
            IssuedLicense,
            "queries/current_license.sql",
            license_id,
            revision
        )
        .fetch_optional(&self.pool)
        .await?
        .ok_or(AuthError::Rejected)
    }
}

#[derive(serde::Serialize, sqlx::FromRow)]
pub struct LicenseSummary {
    pub license_id: Uuid,
    pub customer_id: Uuid,
    pub revision: i64,
    pub revoked_at: Option<chrono::DateTime<chrono::Utc>>,
    pub wire: String,
}
