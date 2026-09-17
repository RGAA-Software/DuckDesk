//! Offline empty-database initialization. Not an HTTP capability.
use crate::{control, ManagedUser, PasswordDigest, StoreError, Username, MIGRATIONS};
use px_pg::{DatabaseConfig, Service};
use uuid::Uuid;
pub async fn initialize_administrator(
    config: &DatabaseConfig,
    deployment: Uuid,
    name: &Username,
    password: &PasswordDigest,
) -> Result<ManagedUser, StoreError> {
    let pool = config
        .connect_bootstrap(Service::Console, deployment, &MIGRATIONS)
        .await
        .map_err(|error| match error {
            px_pg::DatabaseError::Permission => StoreError::Rejected,
            other => StoreError::Database(other),
        })?;
    let result = async {
        px_pg::readiness(&pool, Service::Console, deployment, &MIGRATIONS).await?;
        let owner = sqlx::query_file_scalar!("queries/bootstrap_console_owner.sql")
            .fetch_one(&pool)
            .await?;
        if !owner {
            return Err(StoreError::Rejected);
        }
        let mut tx = pool.begin().await?;
        control::write_gate(&mut tx).await?;
        // Also excludes an INSERT issued outside the normal application gate.
        sqlx::query_file!("queries/bootstrap_console_lock.sql")
            .execute(&mut *tx)
            .await?;
        let count = sqlx::query_file_scalar!("queries/bootstrap_console_count.sql")
            .fetch_one(&mut *tx)
            .await?;
        if count != 0 {
            return Err(StoreError::Rejected);
        }
        let actor = sqlx::query_file_as!(
            ManagedUser,
            "queries/create_managed_user.sql",
            Uuid::new_v4(),
            name.display,
            name.normalized,
            password.encoded(),
            "admin"
        )
        .fetch_one(&mut *tx)
        .await?;
        control::audit(
            &mut tx,
            actor.id,
            actor.id,
            "user_created",
            actor.authorization_revision,
        )
        .await?;
        tx.commit().await?;
        Ok(actor)
    }
    .await;
    pool.close().await;
    result
}
