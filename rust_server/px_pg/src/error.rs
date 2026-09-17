/// Deliberately excludes server messages, SQL, DSNs and constraint names.
#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum DatabaseError {
    #[error("invalid PostgreSQL configuration (details redacted)")]
    Configuration,
    #[error("PostgreSQL unavailable or operation timed out")]
    Unavailable,
    #[error("PostgreSQL authentication or permission denied")]
    Permission,
    #[error("PostgreSQL data constraint conflict")]
    Conflict,
    #[error("PostgreSQL writes are paused for a coordinated backup")]
    WriteBarrier,
    #[error("PostgreSQL deployment or service identity mismatch")]
    Identity,
    #[error("PostgreSQL schema missing, dirty, changed or unsupported")]
    Schema,
    #[error("PostgreSQL operation failed; do not assume an unconfirmed write rolled back")]
    Operation,
}

impl From<sqlx::Error> for DatabaseError {
    fn from(error: sqlx::Error) -> Self {
        match error {
            sqlx::Error::PoolTimedOut
            | sqlx::Error::PoolClosed
            | sqlx::Error::Io(_)
            | sqlx::Error::Tls(_) => Self::Unavailable,
            sqlx::Error::Database(db) => match db.code().as_deref() {
                Some("42501" | "28P01" | "28000") => Self::Permission,
                Some("23505" | "23503" | "23502" | "23514") => Self::Conflict,
                Some("25006") => Self::WriteBarrier,
                Some("57014" | "55P03" | "57P01" | "57P02" | "57P03") => Self::Unavailable,
                Some("42P01" | "3F000") => Self::Schema,
                _ => Self::Operation,
            },
            _ => Self::Operation,
        }
    }
}

impl From<sqlx::migrate::MigrateError> for DatabaseError {
    fn from(error: sqlx::migrate::MigrateError) -> Self {
        match error {
            sqlx::migrate::MigrateError::Execute(error)
            | sqlx::migrate::MigrateError::ExecuteMigration(error, _) => error.into(),
            _ => Self::Schema,
        }
    }
}
