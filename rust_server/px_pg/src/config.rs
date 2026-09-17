use crate::{DatabaseError, Service};
use sqlx::postgres::{PgConnectOptions, PgPoolOptions, PgSslMode};
use sqlx::{migrate::Migrator, ConnectOptions, Connection, PgConnection, PgPool};
use std::{fmt, str::FromStr, time::Duration};
use uuid::Uuid;

#[derive(Debug, Clone, Copy)]
pub enum Transport {
    VerifyFull,
    /// Explicit opt-in, only accepts literal loopback addresses or localhost.
    LocalDevelopment,
}

#[derive(Clone)]
pub struct DatabaseConfig {
    options: PgConnectOptions,
    max_connections: u32,
    acquire_timeout: Duration,
}

impl fmt::Debug for DatabaseConfig {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter
            .debug_struct("DatabaseConfig")
            .field("connection", &"<redacted>")
            .field("max_connections", &self.max_connections)
            .field("acquire_timeout", &self.acquire_timeout)
            .finish()
    }
}

impl DatabaseConfig {
    pub fn parse(dsn: &str, transport: Transport) -> Result<Self, DatabaseError> {
        let parsed = url::Url::parse(dsn).map_err(|_| DatabaseError::Configuration)?;
        if !matches!(parsed.scheme(), "postgres" | "postgresql")
            || parsed.username().is_empty()
            || parsed.path().trim_matches('/').is_empty()
            || parsed.fragment().is_some()
        {
            return Err(DatabaseError::Configuration);
        }
        // No URL options that can override host, schema, timeouts or credentials.
        if parsed.query_pairs().any(|(key, _)| key != "sslrootcert") {
            return Err(DatabaseError::Configuration);
        }
        let ssl = match transport {
            Transport::VerifyFull => PgSslMode::VerifyFull,
            Transport::LocalDevelopment => {
                if !matches!(parsed.host_str(), Some("127.0.0.1" | "localhost" | "[::1]")) {
                    return Err(DatabaseError::Configuration);
                }
                PgSslMode::Disable
            }
        };
        let options = PgConnectOptions::from_str(dsn)
            .map_err(|_| DatabaseError::Configuration)?
            .ssl_mode(ssl)
            .application_name("pixels")
            .options([
                ("search_path", "pixels,pg_catalog"),
                ("statement_timeout", "15000"),
                ("lock_timeout", "2000"),
                ("idle_in_transaction_session_timeout", "15000"),
            ])
            .disable_statement_logging();
        Ok(Self {
            options,
            max_connections: 8,
            acquire_timeout: Duration::from_secs(5),
        })
    }

    pub fn with_pool_limits(mut self, max: u32, timeout: Duration) -> Result<Self, DatabaseError> {
        if max == 0 || max > 256 || timeout.is_zero() || timeout > Duration::from_secs(60) {
            return Err(DatabaseError::Configuration);
        }
        self.max_connections = max;
        self.acquire_timeout = timeout;
        Ok(self)
    }

    /// Unguarded pool for offline administration and isolated test infrastructure only.
    /// Business compositions must use connect_runtime, including every reconnect.
    pub async fn connect(&self) -> Result<PgPool, DatabaseError> {
        PgPoolOptions::new()
            .max_connections(self.max_connections)
            .acquire_timeout(self.acquire_timeout)
            .connect_with(self.options.clone())
            .await
            .map_err(Into::into)
    }

    /// Every physical connection pins the schema, including idle pooled connections.
    /// Validation is repeated after obtaining the lock on every new/reconnected backend.
    /// Thus a dead lease backend or database restart cannot let an old binary write a new schema.
    pub async fn connect_runtime(
        &self,
        service: Service,
        deployment: Uuid,
        expected: &'static Migrator,
    ) -> Result<PgPool, DatabaseError> {
        self.connect_guarded(
            service,
            deployment,
            expected,
            crate::runtime::SchemaAccess::Runtime,
        )
        .await
    }

    /// Offline initial-account writes also pin their expected schema. This never runs DDL.
    pub async fn connect_bootstrap(
        &self,
        service: Service,
        deployment: Uuid,
        expected: &'static Migrator,
    ) -> Result<PgPool, DatabaseError> {
        self.connect_guarded(
            service,
            deployment,
            expected,
            crate::runtime::SchemaAccess::Bootstrap,
        )
        .await
    }

    async fn connect_guarded(
        &self,
        service: Service,
        deployment: Uuid,
        expected: &'static Migrator,
        access: crate::runtime::SchemaAccess,
    ) -> Result<PgPool, DatabaseError> {
        // Retain typed startup failures and hold the gate until the guarded pool is ready.
        let guard = tokio::time::timeout(self.acquire_timeout, async {
            let mut connection = PgConnection::connect_with(&self.options).await?;
            crate::runtime::admit_connection(
                &mut connection,
                service,
                deployment,
                expected,
                access,
            )
            .await?;
            Ok::<_, DatabaseError>(connection)
        })
        .await
        .map_err(|_| DatabaseError::Unavailable)??;
        let result = PgPoolOptions::new()
            .max_connections(self.max_connections)
            .acquire_timeout(self.acquire_timeout)
            .after_connect(move |connection, _metadata| {
                Box::pin(async move {
                    crate::runtime::admit_connection(
                        connection, service, deployment, expected, access,
                    )
                    .await
                    // SQLx may log this callback error. Do not include server details/credentials.
                    .map_err(|error| sqlx::Error::Protocol(error.to_string()))
                })
            })
            .connect_with(self.options.clone())
            .await
            .map_err(DatabaseError::from);
        // PgConnection::close consumes the session; cancellation/drop also closes its socket.
        if let Err(error) = guard.close().await {
            if let Ok(pool) = &result {
                pool.close().await;
            }
            return Err(error.into());
        }
        result
    }

    pub(crate) fn options(&self) -> PgConnectOptions {
        self.options.clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn configuration_and_errors_never_format_credentials() {
        let secret = "synthetic-secret";
        let dsn = format!("postgres://operator:{secret}@127.0.0.1/pixels_console");
        let config = DatabaseConfig::parse(&dsn, Transport::LocalDevelopment).unwrap();
        assert!(!format!("{config:?}").contains(secret));
        assert!(!format!("{config:?}").contains("operator"));
        let error = DatabaseConfig::parse("postgres://secret", Transport::VerifyFull).unwrap_err();
        assert_eq!(error, DatabaseError::Configuration);
        assert!(!format!("{error:?} {error}").contains(secret));
    }

    #[test]
    fn insecure_transport_and_option_overrides_are_rejected() {
        for dsn in [
            "postgres://u:p@example.org/pixels_console",
            "postgres://u:p@127.0.0.1/pixels_console?host=example.org",
            "postgres://u:p@127.0.0.1/pixels_console?options=-csearch_path=public",
            "postgres://u:p@127.0.0.1/pixels_console?sslmode=disable",
            "postgres://u:p@127.0.0.1/",
        ] {
            assert!(DatabaseConfig::parse(dsn, Transport::LocalDevelopment).is_err());
        }
        assert!(DatabaseConfig::parse(
            "postgres://u:p@example.org/pixels_console",
            Transport::VerifyFull
        )
        .is_ok());
    }
}
