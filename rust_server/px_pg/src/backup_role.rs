use crate::{schema::identity, DatabaseConfig, DatabaseError, Service};
use sqlx::{Connection, PgConnection};
use uuid::Uuid;

const MINIMUM_PASSWORD_LENGTH: usize = 32;
const MAXIMUM_PASSWORD_LENGTH: usize = 128;

impl Service {
    fn owner_role(self) -> &'static str {
        match self {
            Self::Console => "pixels_console_owner",
            Self::Auth => "pixels_auth_owner",
            Self::Desk => "pixels_desk_owner",
        }
    }

    pub fn backup_role(self) -> &'static str {
        match self {
            Self::Console => "pixels_console_backup",
            Self::Auth => "pixels_auth_backup",
            Self::Desk => "pixels_desk_backup",
        }
    }
}

fn valid_password(password: &str) -> bool {
    (MINIMUM_PASSWORD_LENGTH..=MAXIMUM_PASSWORD_LENGTH).contains(&password.len())
        && password
            .bytes()
            .all(|character| character.is_ascii_alphanumeric() || matches!(character, b'-' | b'_'))
}

/// Creates or rotates the fixed read-only backup role for one service database.
///
/// This administrative operation requires a PostgreSQL superuser connection. The password is
/// passed as a bound value and is never interpolated into client-side SQL or diagnostics.
pub async fn provision_backup_role(
    configuration: &DatabaseConfig,
    service: Service,
    deployment_id: Uuid,
    password: &str,
) -> Result<&'static str, DatabaseError> {
    if !valid_password(password) {
        return Err(DatabaseError::Configuration);
    }

    let mut connection = PgConnection::connect_with(&configuration.options()).await?;
    identity(&mut connection, service, deployment_id).await?;
    let administrator_is_superuser: bool = sqlx::query_scalar(
        "SELECT role_record.rolsuper FROM pg_catalog.pg_roles AS role_record WHERE role_record.rolname=CURRENT_USER",
    )
    .fetch_one(&mut connection)
    .await?;
    if !administrator_is_superuser {
        return Err(DatabaseError::Permission);
    }

    let backup_role = service.backup_role();
    let owner_role = service.owner_role();
    let mut transaction = connection.begin().await?;
    sqlx::query("SELECT set_config('pixels.backup_role_password',$1,true)")
        .bind(password)
        .execute(&mut *transaction)
        .await?;

    let role_statement = format!(
        "DO $pixels$\n\
         DECLARE role_password text := current_setting('pixels.backup_role_password');\n\
         BEGIN\n\
           IF EXISTS (SELECT 1 FROM pg_catalog.pg_roles WHERE rolname='{backup_role}') THEN\n\
             EXECUTE format('ALTER ROLE {backup_role} WITH LOGIN NOINHERIT NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION NOBYPASSRLS CONNECTION LIMIT 2 PASSWORD %L',role_password);\n\
           ELSE\n\
             EXECUTE format('CREATE ROLE {backup_role} WITH LOGIN NOINHERIT NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION NOBYPASSRLS CONNECTION LIMIT 2 PASSWORD %L',role_password);\n\
           END IF;\n\
         END\n\
         $pixels$"
    );
    sqlx::query(&role_statement)
        .execute(&mut *transaction)
        .await?;

    let database_privileges = format!(
        "DO $pixels$\n\
         BEGIN\n\
           EXECUTE format('REVOKE ALL PRIVILEGES ON DATABASE %I FROM {backup_role}',current_database());\n\
           EXECUTE format('GRANT CONNECT ON DATABASE %I TO {backup_role}',current_database());\n\
         END\n\
         $pixels$"
    );
    sqlx::query(&database_privileges)
        .execute(&mut *transaction)
        .await?;

    for privilege_statement in [
        format!("REVOKE ALL PRIVILEGES ON SCHEMA pixels FROM {backup_role}"),
        format!("REVOKE ALL PRIVILEGES ON ALL TABLES IN SCHEMA pixels FROM {backup_role}"),
        format!("REVOKE ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA pixels FROM {backup_role}"),
        format!("GRANT USAGE ON SCHEMA pixels TO {backup_role}"),
        format!("GRANT SELECT ON ALL TABLES IN SCHEMA pixels TO {backup_role}"),
        format!("GRANT SELECT ON ALL SEQUENCES IN SCHEMA pixels TO {backup_role}"),
        format!("ALTER DEFAULT PRIVILEGES FOR ROLE {owner_role} IN SCHEMA pixels REVOKE ALL ON TABLES FROM {backup_role}"),
        format!("ALTER DEFAULT PRIVILEGES FOR ROLE {owner_role} IN SCHEMA pixels REVOKE ALL ON SEQUENCES FROM {backup_role}"),
        format!("ALTER DEFAULT PRIVILEGES FOR ROLE {owner_role} IN SCHEMA pixels GRANT SELECT ON TABLES TO {backup_role}"),
        format!("ALTER DEFAULT PRIVILEGES FOR ROLE {owner_role} IN SCHEMA pixels GRANT SELECT ON SEQUENCES TO {backup_role}"),
    ] {
        sqlx::query(&privilege_statement)
            .execute(&mut *transaction)
            .await?;
    }

    let membership_count: i64 = sqlx::query_scalar(
        "SELECT count(*) FROM pg_catalog.pg_auth_members AS membership JOIN pg_catalog.pg_roles AS member_role ON member_role.oid=membership.member WHERE member_role.rolname=$1",
    )
    .bind(backup_role)
    .fetch_one(&mut *transaction)
    .await?;
    if membership_count != 0 {
        return Err(DatabaseError::Permission);
    }

    transaction.commit().await?;
    connection.close().await?;
    Ok(backup_role)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn password_policy_is_bounded_and_pgpass_safe() {
        assert!(valid_password(&"a".repeat(MINIMUM_PASSWORD_LENGTH)));
        assert!(valid_password(&format!(
            "{}-_",
            "a".repeat(MINIMUM_PASSWORD_LENGTH)
        )));
        assert!(!valid_password(&"a".repeat(MINIMUM_PASSWORD_LENGTH - 1)));
        assert!(!valid_password(&"a".repeat(MAXIMUM_PASSWORD_LENGTH + 1)));
        assert!(!valid_password(&format!(
            "{}:",
            "a".repeat(MINIMUM_PASSWORD_LENGTH)
        )));
        assert!(!valid_password(&format!(
            "{}\\",
            "a".repeat(MINIMUM_PASSWORD_LENGTH)
        )));
    }
}
