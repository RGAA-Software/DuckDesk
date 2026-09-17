use crate::{DatabaseError, Service};
use sqlx::{migrate::Migrator, PgConnection, PgPool};
use uuid::Uuid;

#[derive(Clone, Copy)]
pub(crate) enum SchemaAccess {
    Runtime,
    Bootstrap,
}

pub(crate) async fn require_owner(
    connection: &mut PgConnection,
    service: Service,
) -> Result<(), DatabaseError> {
    let allowed: bool = sqlx::query_scalar(
        "SELECT current_user::text=$1 AND session_user::text=$1
        AND NOT r.rolsuper AND NOT r.rolcreatedb AND NOT r.rolcreaterole
        AND NOT r.rolreplication AND NOT r.rolbypassrls
        FROM pg_roles r WHERE r.rolname=current_user",
    )
    .bind(format!("pixels_{}_owner", service.name()))
    .fetch_one(connection)
    .await?;
    if !allowed {
        return Err(DatabaseError::Permission);
    }
    Ok(())
}

pub(crate) async fn admit_connection(
    connection: &mut PgConnection,
    service: Service,
    deployment: Uuid,
    expected: &Migrator,
    access: SchemaAccess,
) -> Result<(), DatabaseError> {
    match access {
        SchemaAccess::Runtime => require_role(connection, service).await?,
        SchemaAccess::Bootstrap => require_owner(connection, service).await?,
    }
    let shared: bool = sqlx::query_scalar("SELECT pg_try_advisory_lock_shared($1)")
        .bind(crate::schema::SCHEMA_LOCK)
        .fetch_one(&mut *connection)
        .await?;
    if !shared {
        return Err(DatabaseError::Conflict);
    }
    crate::schema::readiness_on(connection, service, deployment, expected).await
}

/// Business processes must not accidentally run with the offline schema owner's role.
/// Readiness also detects subsequently broadened privileges; the CLI's schema check
/// remains separate so owner-only bootstrap and migrations can use it.
pub async fn runtime_readiness(
    pool: &PgPool,
    service: Service,
    deployment: Uuid,
    expected: &Migrator,
) -> Result<(), DatabaseError> {
    crate::readiness(pool, service, deployment, expected).await?;
    let mut connection = pool.acquire().await?;
    require_role(&mut connection, service).await
}

pub(crate) async fn require_role(
    connection: &mut PgConnection,
    service: Service,
) -> Result<(), DatabaseError> {
    let runtime = format!("pixels_{}_runtime", service.name());
    let owner = format!("pixels_{}_owner", service.name());
    let allowed:bool=sqlx::query_scalar(
        "SELECT current_user::text=$1 AND session_user::text=$1 AND NOT r.rolsuper AND NOT r.rolcreatedb
        AND NOT r.rolcreaterole AND NOT r.rolreplication AND NOT r.rolbypassrls
        AND NOT has_schema_privilege(current_user,'pixels','CREATE')
        AND NOT has_database_privilege(current_user,current_database(),'CREATE')
        AND NOT has_database_privilege(current_user,current_database(),'TEMPORARY')
        AND NOT pg_has_role(current_user,$2,'MEMBER')
        FROM pg_roles r WHERE r.rolname=current_user"
    ).bind(runtime).bind(owner).fetch_one(connection).await?;
    if !allowed {
        return Err(DatabaseError::Permission);
    }
    Ok(())
}
