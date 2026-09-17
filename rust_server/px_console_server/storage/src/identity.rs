use crate::{
    model::CredentialRow, AuthenticatedSession, ClientType, Credential, PasswordDigest, StoreError,
    TokenDigest, UserProfile, Username,
};
#[cfg(feature = "pg-integration")]
use px_pg::DatabaseConfig;
use sqlx::PgPool;
use std::time::Duration;
use uuid::Uuid;

#[derive(Clone)]
pub struct IdentityStore {
    pub(crate) pool: PgPool,
}

impl IdentityStore {
    /// Domain composition root: validates schema and deployment, never creates tables.
    #[cfg(feature = "pg-integration")]
    pub async fn connect(config: &DatabaseConfig, deployment: Uuid) -> Result<Self, StoreError> {
        Ok(Self {
            pool: crate::connect(config, deployment).await?,
        })
    }

    #[cfg(feature = "pg-integration")]
    pub async fn close(&self) {
        self.pool.close().await;
    }

    pub async fn register(
        &self,
        username: &Username,
        password: &PasswordDigest,
    ) -> Result<UserProfile, StoreError> {
        let mut tx = self.pool.begin().await?;
        crate::control::write_gate(&mut tx).await?;
        let user = sqlx::query_file_as!(
            UserProfile,
            "queries/register.sql",
            Uuid::new_v4(),
            username.display,
            username.normalized,
            password.encoded()
        )
        .fetch_one(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(user)
    }

    /// Internal credential lookup; HTTP responses must never serialize this object.
    pub async fn credential(&self, username: &Username) -> Result<Credential, StoreError> {
        sqlx::query_file_as!(CredentialRow, "queries/credential.sql", username.normalized)
            .fetch_optional(&self.pool)
            .await?
            .ok_or(StoreError::Rejected)?
            .credential()
    }

    /// Internal password verification input bound to this exact login, not a supplied username.
    pub async fn session_credential(
        &self,
        token: &TokenDigest,
        client: ClientType,
    ) -> Result<Credential, StoreError> {
        sqlx::query_file_as!(
            CredentialRow,
            "queries/session_credential.sql",
            token.0.as_slice(),
            client.name()
        )
        .fetch_optional(&self.pool)
        .await?
        .ok_or(StoreError::Rejected)?
        .credential()
    }
    pub async fn profile(
        &self,
        token: &TokenDigest,
        client: ClientType,
    ) -> Result<crate::ManagedUser, StoreError> {
        sqlx::query_file_as!(
            crate::ManagedUser,
            "queries/self_profile.sql",
            token.0.as_slice(),
            client.name()
        )
        .fetch_optional(&self.pool)
        .await?
        .ok_or(StoreError::Rejected)
    }

    /// Only call after verifying the credential. Its revision prevents password-change/login races.
    pub async fn issue_session(
        &self,
        user: Uuid,
        verified_revision: i64,
        token: &TokenDigest,
        client: ClientType,
        lifetime: Duration,
    ) -> Result<AuthenticatedSession, StoreError> {
        if lifetime.is_zero()
            || lifetime.subsec_nanos() != 0
            || lifetime.as_secs() > 365 * 86400
            || verified_revision <= 0
        {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        crate::control::read_gate(&mut tx).await?;
        let current = sqlx::query_file_scalar!("queries/lock_user.sql", user)
            .fetch_optional(&mut *tx)
            .await?;
        if current != Some(verified_revision) {
            return Err(StoreError::Rejected);
        }
        let session = sqlx::query_file_as!(
            AuthenticatedSession,
            "queries/issue_session.sql",
            Uuid::new_v4(),
            user,
            token.0.as_slice(),
            client.name(),
            verified_revision,
            lifetime.as_secs() as f64
        )
        .fetch_one(&mut *tx)
        .await?;
        tx.commit().await?;
        Ok(session)
    }

    /// Checks expiration and current revision on every request, regardless of physical cleanup.
    pub async fn authenticate(
        &self,
        token: &TokenDigest,
        client: ClientType,
    ) -> Result<AuthenticatedSession, StoreError> {
        sqlx::query_file_as!(
            AuthenticatedSession,
            "queries/authenticate.sql",
            token.0.as_slice(),
            client.name()
        )
        .fetch_optional(&self.pool)
        .await?
        .ok_or(StoreError::Rejected)
    }

    /// Caller obtains actor from authenticated middleware, never from request body owner fields.
    /// Unknown IDs and another user's IDs have the same rejection and no persistent side effect.
    pub async fn revoke_session(&self, actor: Uuid, session: Uuid) -> Result<(), StoreError> {
        let mut tx = self.pool.begin().await?;
        crate::control::write_gate(&mut tx).await?;
        let found = sqlx::query_file_scalar!("queries/revoke_session.sql", session, actor)
            .fetch_one(&mut *tx)
            .await?;
        if found != 1 {
            return Err(StoreError::Rejected);
        }
        tx.commit().await?;
        Ok(())
    }

    pub async fn change_password(
        &self,
        token: &TokenDigest,
        client: ClientType,
        expected_revision: i64,
        password: &PasswordDigest,
    ) -> Result<i64, StoreError> {
        if expected_revision < 1 {
            return Err(StoreError::InvalidInput);
        }
        let mut tx = self.pool.begin().await?;
        crate::control::write_gate(&mut tx).await?;
        let actor = sqlx::query_file_as!(
            AuthenticatedSession,
            "queries/authenticate.sql",
            token.0.as_slice(),
            client.name()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        if actor.authorization_revision != expected_revision {
            return Err(StoreError::Rejected);
        }
        let revision = sqlx::query_file_scalar!(
            "queries/change_password.sql",
            actor.user_id,
            expected_revision,
            password.encoded()
        )
        .fetch_optional(&mut *tx)
        .await?
        .ok_or(StoreError::Rejected)?;
        crate::control::audit(
            &mut tx,
            actor.user_id,
            actor.user_id,
            "password_changed",
            revision,
        )
        .await?;
        tx.commit().await?;
        Ok(revision)
    }
}
