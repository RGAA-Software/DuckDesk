use crate::{PasswordDigest, StoreError, TokenDigest, Username};
use chrono::{DateTime, Utc};
use sqlx::{PgConnection, PgPool};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Role {
    User,
    Admin,
    Viewer,
}
impl Role {
    pub fn name(self) -> &'static str {
        match self {
            Self::User => "user",
            Self::Admin => "admin",
            Self::Viewer => "viewer",
        }
    }
}
#[derive(Debug, Clone, serde::Serialize, sqlx::FromRow)]
pub struct ManagedUser {
    pub id: Uuid,
    pub username: String,
    pub role: String,
    pub disabled: bool,
    pub deleted_at: Option<DateTime<Utc>>,
    pub authorization_revision: i64,
    pub revision: i64,
    pub has_avatar: bool,
    pub created_at: DateTime<Utc>,
}
#[derive(Clone)]
pub struct ControlStore {
    pub(crate) pool: PgPool,
}
pub(crate) async fn read_gate(connection: &mut PgConnection) -> Result<(), StoreError> {
    sqlx::query_file!("queries/authorization_read_gate.sql")
        .fetch_one(connection)
        .await?;
    Ok(())
}
pub(crate) async fn write_gate(connection: &mut PgConnection) -> Result<(), StoreError> {
    sqlx::query_file!("queries/authorization_write_gate.sql")
        .fetch_one(connection)
        .await?;
    Ok(())
}
// Call only after a read/write gate, and retain both gate and row locks through commit.
pub(crate) async fn authorize(
    connection: &mut PgConnection,
    token: &TokenDigest,
    write: bool,
) -> Result<Uuid, StoreError> {
    let actor = sqlx::query_file!("queries/authorize_administrator.sql", token.0.as_slice())
        .fetch_optional(connection)
        .await?
        .ok_or(StoreError::Rejected)?;
    if actor.role != "admin" && (write || actor.role != "viewer") {
        return Err(StoreError::Rejected);
    }
    Ok(actor.id)
}
pub(crate) async fn audit(
    connection: &mut PgConnection,
    actor: Uuid,
    subject: Uuid,
    action: &str,
    revision: i64,
) -> Result<(), StoreError> {
    sqlx::query_file!(
        "queries/authorization_audit.sql",
        Uuid::new_v4(),
        actor,
        subject,
        action,
        revision
    )
    .execute(connection)
    .await?;
    Ok(())
}
impl ControlStore {
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
    /// Revalidates a read-only administrator session, including its current role,
    /// disabled/deleted state, expiration and authorization revision.
    pub async fn authorize_read(&self, token: &TokenDigest) -> Result<(), StoreError> {
        let mut transaction = self.pool.begin().await?;
        read_gate(&mut transaction).await?;
        authorize(&mut transaction, token, false).await?;
        transaction.commit().await?;
        Ok(())
    }
    pub async fn list_users(
        &self,
        token: &TokenDigest,
        after: Option<Uuid>,
        limit: u32,
    ) -> Result<Vec<ManagedUser>, StoreError> {
        if !(1..=100).contains(&limit) {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        read_gate(&mut tx).await?;
        authorize(&mut tx, token, false).await?;
        let users = sqlx::query_file_as!(
            ManagedUser,
            "queries/managed_users.sql",
            after,
            i64::from(limit)
        )
        .fetch_all(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(users)
    }
    pub async fn create_user(
        &self,
        token: &TokenDigest,
        name: &Username,
        password: &PasswordDigest,
        role: Role,
    ) -> Result<ManagedUser, StoreError> {
        let mut tx = self.pool.begin().await?;
        write_gate(&mut tx).await?;
        let actor = authorize(&mut tx, token, true).await?;
        let user = sqlx::query_file_as!(
            ManagedUser,
            "queries/create_managed_user.sql",
            Uuid::new_v4(),
            name.display,
            name.normalized,
            password.encoded(),
            role.name()
        )
        .fetch_one(&mut *tx)
        .await?;
        audit(
            &mut tx,
            actor,
            user.id,
            "user_created",
            user.authorization_revision,
        )
        .await?;
        tx.commit().await?;
        Ok(user)
    }
    pub async fn update_user(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
        role: Role,
        disabled: bool,
    ) -> Result<ManagedUser, StoreError> {
        if revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        write_gate(&mut tx).await?;
        let actor = authorize(&mut tx, token, true).await?;
        let user = sqlx::query_file_as!(ManagedUser, "queries/lock_managed_user.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if user.revision != revision || user.deleted_at.is_some() {
            return Err(StoreError::Rejected);
        }
        if user.role == role.name() && user.disabled == disabled {
            tx.commit().await?;
            return Ok(user);
        }
        if user.role == "admin" && !user.disabled && (role != Role::Admin || disabled) {
            Self::retain_administrator(&mut tx).await?;
        }
        let user = sqlx::query_file_as!(
            ManagedUser,
            "queries/update_managed_user.sql",
            id,
            role.name(),
            disabled
        )
        .fetch_one(&mut *tx)
        .await?;
        sqlx::query_file!(
            "queries/authorization_event.sql",
            Uuid::new_v4(),
            id,
            user.authorization_revision,
            "user_changed"
        )
        .execute(&mut *tx)
        .await?;
        audit(
            &mut tx,
            actor,
            id,
            "user_changed",
            user.authorization_revision,
        )
        .await?;
        tx.commit().await?;
        Ok(user)
    }
    pub async fn delete_user(
        &self,
        token: &TokenDigest,
        id: Uuid,
        revision: i64,
    ) -> Result<(), StoreError> {
        if revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        write_gate(&mut tx).await?;
        let actor = authorize(&mut tx, token, true).await?;
        let user = sqlx::query_file_as!(ManagedUser, "queries/lock_managed_user.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if user.revision != revision || user.deleted_at.is_some() {
            return Err(StoreError::Rejected);
        }
        if user.role == "admin" && !user.disabled {
            Self::retain_administrator(&mut tx).await?;
        }
        let next = sqlx::query_file_scalar!("queries/delete_managed_user.sql", id)
            .fetch_one(&mut *tx)
            .await?;
        sqlx::query_file!(
            "queries/authorization_event.sql",
            Uuid::new_v4(),
            id,
            next,
            "user_changed"
        )
        .execute(&mut *tx)
        .await?;
        audit(&mut tx, actor, id, "user_deleted", next).await?;
        tx.commit().await?;
        Ok(())
    }
    pub async fn reset_password(
        &self,
        token: &TokenDigest,
        id: Uuid,
        expected_revision: i64,
        password: &PasswordDigest,
    ) -> Result<ManagedUser, StoreError> {
        if expected_revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        write_gate(&mut tx).await?;
        let actor = authorize(&mut tx, token, true).await?;
        let user = sqlx::query_file_as!(ManagedUser, "queries/lock_managed_user.sql", id)
            .fetch_optional(&mut *tx)
            .await?
            .ok_or(StoreError::Rejected)?;
        if user.revision != expected_revision || user.deleted_at.is_some() || user.disabled {
            return Err(StoreError::Rejected);
        }
        let revision = sqlx::query_file_scalar!(
            "queries/change_password.sql",
            id,
            user.authorization_revision,
            password.encoded()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        audit(&mut tx, actor, id, "password_reset", revision).await?;
        let user = sqlx::query_file_as!(ManagedUser, "queries/lock_managed_user.sql", id)
            .fetch_one(&mut *tx)
            .await?;
        tx.commit().await?;
        Ok(user)
    }
    async fn retain_administrator(connection: &mut PgConnection) -> Result<(), StoreError> {
        if sqlx::query_file_scalar!("queries/active_administrators.sql")
            .fetch_one(connection)
            .await?
            <= 1
        {
            return Err(StoreError::Rejected);
        }
        Ok(())
    }
}
