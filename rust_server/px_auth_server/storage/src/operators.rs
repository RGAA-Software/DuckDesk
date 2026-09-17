use crate::{AuthError, LicenseStore};
use chrono::{DateTime, Utc};
use sqlx::PgPool;
use uuid::Uuid;
use zeroize::Zeroizing;

#[derive(Clone)]
pub struct OperatorStore {
    pub(crate) pool: PgPool,
}
// Never serialize/debug the credential row.
pub struct OperatorCredential {
    pub id: Uuid,
    pub password_hash: Zeroizing<String>,
    pub authorization_revision: i64,
}
#[derive(Debug, Clone, serde::Serialize, sqlx::FromRow)]
pub struct OperatorProfile {
    pub id: Uuid,
    pub username: String,
    pub role: String,
}
pub struct OperatorSession {
    pub id: Uuid,
    pub expires_at: DateTime<Utc>,
}
impl LicenseStore {
    pub fn operators(&self) -> OperatorStore {
        OperatorStore {
            pool: self.pool.clone(),
        }
    }
}
impl OperatorStore {
    pub async fn credential(
        &self,
        normalized: &str,
    ) -> Result<Option<OperatorCredential>, AuthError> {
        let row = sqlx::query_file!("queries/operator_credential.sql", normalized)
            .fetch_optional(&self.pool)
            .await?;
        Ok(row.map(|row| OperatorCredential {
            id: row.id,
            password_hash: Zeroizing::new(row.password_hash),
            authorization_revision: row.authorization_revision,
        }))
    }
    /// Caller must have verified the password for this exact author/revision.
    pub async fn issue_session(
        &self,
        id: Uuid,
        verified_revision: i64,
        token: &[u8; 32],
    ) -> Result<OperatorSession, AuthError> {
        let mut tx = self.pool.begin().await?;
        let revision: Option<i64> =
            sqlx::query_file_scalar!("queries/lock_operator_revision.sql", id)
                .fetch_optional(&mut *tx)
                .await?;
        if revision != Some(verified_revision) || verified_revision < 1 {
            return Err(AuthError::Rejected);
        }
        let session = Uuid::new_v4();
        let expires = sqlx::query_file_scalar!(
            "queries/create_operator_session.sql",
            session,
            id,
            token.as_slice(),
            verified_revision
        )
        .fetch_one(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(OperatorSession {
            id: session,
            expires_at: expires,
        })
    }
    pub async fn authenticate(&self, token: &[u8; 32]) -> Result<OperatorProfile, AuthError> {
        sqlx::query_file_as!(
            OperatorProfile,
            "queries/authenticate_operator.sql",
            token.as_slice()
        )
        .fetch_optional(&self.pool)
        .await?
        .ok_or(AuthError::Rejected)
    }
    pub async fn revoke_session(&self, token: &[u8; 32]) -> Result<(), AuthError> {
        // Deliberately idempotent, but an arbitrary token never reveals a session or grants access.
        sqlx::query_file!("queries/revoke_operator_session.sql", token.as_slice())
            .execute(&self.pool)
            .await?;
        Ok(())
    }
    /// Explicit administration, not a startup upsert that silently resets a password on every restart.
    pub async fn set_password(
        &self,
        actor_token: &[u8; 32],
        author: Uuid,
        expected_revision: i64,
        password_hash: &str,
    ) -> Result<(), AuthError> {
        if expected_revision < 1 {
            return Err(AuthError::Invalid);
        }
        validate_password_hash(password_hash)?;
        let actor = self.authenticate(actor_token).await?;
        let mut tx = self.pool.begin().await?;
        // Both administrator and target are locked in UUID order, including self-reset.
        // Revalidate session/role after acquiring locks; pre-read identity is not authorization.
        sqlx::query_file!("queries/lock_password_subjects.sql", &[actor.id, author])
            .fetch_all(&mut *tx)
            .await?;
        LicenseStore::authorize(&mut tx, actor_token, true).await?;
        let changed = sqlx::query_file!(
            "queries/set_operator_password.sql",
            password_hash,
            author,
            expected_revision
        )
        .execute(&mut *tx)
        .await?
        .rows_affected();
        if changed != 1 {
            return Err(AuthError::Conflict);
        }
        tx.commit().await?;
        Ok(())
    }
}
#[derive(serde::Serialize, sqlx::FromRow)]
pub struct AuthorSummary {
    pub id: Uuid,
    pub username: String,
    pub role: String,
    pub authorization_revision: i64,
}
fn validate_password_hash(password_hash: &str) -> Result<(), AuthError> {
    if !px_credentials::valid_hash(password_hash) {
        return Err(AuthError::Invalid);
    }

    Ok(())
}
fn validate_author(username: &str, hash: &str, role: &str) -> Result<(), AuthError> {
    if username != username.trim()
        || username != username.to_lowercase()
        || !(2..=64).contains(&username.chars().count())
        || username
            .chars()
            .any(|character| character.is_control() || matches!(character, '/' | '\\'))
        || !matches!(role, "admin" | "visitor")
    {
        return Err(AuthError::Invalid);
    }
    validate_password_hash(hash)
}
impl OperatorStore {
    pub async fn list(
        &self,
        token: &[u8; 32],
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<AuthorSummary>, AuthError> {
        if !(1..=100).contains(&limit) {
            return Err(AuthError::Invalid);
        }
        let mut tx = self.pool.begin().await?;
        LicenseStore::authorize(&mut tx, token, true).await?;
        let rows = sqlx::query_file_as!(
            AuthorSummary,
            "queries/list_authors.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(rows)
    }
    pub async fn create(
        &self,
        token: &[u8; 32],
        username: &str,
        hash: &str,
        role: &str,
    ) -> Result<AuthorSummary, AuthError> {
        validate_author(username, hash, role)?;
        let mut tx = self.pool.begin().await?;
        LicenseStore::authorize(&mut tx, token, true).await?;
        let row = sqlx::query_file_as!(
            AuthorSummary,
            "queries/create_author.sql",
            Uuid::new_v4(),
            username,
            hash,
            role
        )
        .fetch_one(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(row)
    }
}
/// Separate owner-only empty-database initialization. Never called from server startup.
pub async fn bootstrap_author(
    config: &px_pg::DatabaseConfig,
    deployment: Uuid,
    username: &str,
    hash: &str,
) -> Result<AuthorSummary, AuthError> {
    validate_author(username, hash, "admin")?;
    let pool = config
        .connect_bootstrap(px_pg::Service::Auth, deployment, &crate::MIGRATIONS)
        .await
        .map_err(|error| match error {
            px_pg::DatabaseError::Permission => AuthError::Rejected,
            other => AuthError::Database(other),
        })?;
    let result = async {
        px_pg::readiness(&pool, px_pg::Service::Auth, deployment, &crate::MIGRATIONS).await?;
        let mut tx = pool.begin().await?;
        let role = sqlx::query_file_scalar!("queries/database_role.sql")
            .fetch_one(&mut *tx)
            .await?;
        if role != "pixels_auth_owner" {
            return Err(AuthError::Rejected);
        }
        sqlx::query_file!("queries/lock_bootstrap.sql")
            .fetch_one(&mut *tx)
            .await?;
        if sqlx::query_file_scalar!("queries/count_authors.sql")
            .fetch_one(&mut *tx)
            .await?
            != 0
        {
            return Err(AuthError::Conflict);
        }
        let row = sqlx::query_file_as!(
            AuthorSummary,
            "queries/create_author.sql",
            Uuid::new_v4(),
            username,
            hash,
            "admin"
        )
        .fetch_one(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(row)
    }
    .await;
    pool.close().await;
    result
}
