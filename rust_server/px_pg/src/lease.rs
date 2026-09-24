//! A dedicated PostgreSQL session lock. Never return its connection to a pool.
//! The application renews at least every second and gates work on status.check().
//! Failure/expiry is terminal: only a new process activation may obtain a new lease.
use crate::{DatabaseConfig, DatabaseError, Service};
use sqlx::{Connection, PgConnection};
use std::{
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};
use uuid::Uuid;
const LOCK: i64 = 22091602;
const LIFETIME: Duration = Duration::from_secs(5);
const PROBE_TIMEOUT: Duration = Duration::from_secs(1);
struct Deadline {
    until: Instant,
    failed: bool,
}
#[derive(Clone)]
pub struct LeaseStatus {
    deadline: Arc<Mutex<Deadline>>,
}
impl LeaseStatus {
    pub fn check(&self) -> Result<(), DatabaseError> {
        let mut state = self
            .deadline
            .lock()
            .map_err(|_| DatabaseError::Unavailable)?;
        if state.failed || Instant::now() >= state.until {
            state.failed = true;
            return Err(DatabaseError::Unavailable);
        }
        Ok(())
    }
    fn fail(&self) {
        if let Ok(mut state) = self.deadline.lock() {
            state.failed = true;
        }
    }
    fn extend(&self, until: Instant) -> Result<(), DatabaseError> {
        let mut state = self
            .deadline
            .lock()
            .map_err(|_| DatabaseError::Unavailable)?;
        if state.failed || Instant::now() >= state.until || Instant::now() >= until {
            state.failed = true;
            return Err(DatabaseError::Unavailable);
        }
        state.until = until;
        Ok(())
    }
}
pub struct ServiceLease {
    connection: PgConnection,
    service: Service,
    deployment: Uuid,
    status: LeaseStatus,
    #[cfg(feature = "pg-integration")]
    backend_pid: i32,
}
impl ServiceLease {
    pub async fn acquire(
        config: &DatabaseConfig,
        service: Service,
        deployment: Uuid,
    ) -> Result<Self, DatabaseError> {
        // Readiness/schema validation is separately mandatory before business startup.
        let mut connection = tokio::time::timeout(
            Duration::from_secs(5),
            PgConnection::connect_with(&config.options()),
        )
        .await
        .map_err(|_| DatabaseError::Unavailable)??;
        crate::schema::identity(&mut connection, service, deployment).await?;
        crate::runtime::require_role(&mut connection, service).await?;
        let (acquired, _backend_pid): (bool, i32) = tokio::time::timeout(
            PROBE_TIMEOUT,
            sqlx::query_as("SELECT pg_try_advisory_lock($1),pg_backend_pid()")
                .bind(LOCK)
                .fetch_one(&mut connection),
        )
        .await
        .map_err(|_| DatabaseError::Unavailable)??;
        if !acquired {
            return Err(DatabaseError::Conflict);
        }
        // The previous backend may have died while its process still holds a cached
        // local status. Do not publish this activation until every old status expired.
        // A clean first startup also waits: there is no trusted in-DB shortcut.
        tokio::time::sleep(LIFETIME).await;
        let started = Instant::now();
        tokio::time::timeout(
            PROBE_TIMEOUT,
            Self::probe(&mut connection, service, deployment),
        )
        .await
        .map_err(|_| DatabaseError::Unavailable)??;
        let status = LeaseStatus {
            deadline: Arc::new(Mutex::new(Deadline {
                until: started + LIFETIME,
                failed: false,
            })),
        };
        status.check()?;
        Ok(Self {
            connection,
            service,
            deployment,
            status,
            #[cfg(feature = "pg-integration")]
            backend_pid: _backend_pid,
        })
    }
    pub fn status(&self) -> LeaseStatus {
        self.status.clone()
    }
    async fn probe(
        connection: &mut PgConnection,
        service: Service,
        deployment: Uuid,
    ) -> Result<(), DatabaseError> {
        crate::schema::identity(connection, service, deployment).await?;
        crate::runtime::require_role(connection, service).await?;
        let held: bool = sqlx::query_scalar(
            "SELECT EXISTS(SELECT 1 FROM pg_locks WHERE locktype='advisory' AND granted
            AND pid=pg_backend_pid() AND classid=0 AND objid::bigint=$1 AND objsubid=1
            AND database=(SELECT oid FROM pg_database WHERE datname=current_database()))",
        )
        .bind(LOCK)
        .fetch_one(connection)
        .await
        .map_err(|error| {
            let category = match &error {
                sqlx::Error::Io(_) => "io",
                sqlx::Error::Tls(_) => "tls",
                sqlx::Error::Database(_) => "database",
                _ => "other",
            };
            eprintln!("PostgreSQL service lease probe query failed: {category}");
            DatabaseError::from(error)
        })?;
        if held {
            Ok(())
        } else {
            eprintln!("PostgreSQL service lease probe failed: advisory lock no longer held");
            Err(DatabaseError::Unavailable)
        }
    }
    pub async fn renew(&mut self) -> Result<(), DatabaseError> {
        if let Err(error) = self.status.check() {
            eprintln!("PostgreSQL service lease renewal rejected: local deadline expired or lease already failed");
            return Err(error);
        }
        let started = Instant::now();
        let result = tokio::time::timeout(
            PROBE_TIMEOUT,
            Self::probe(&mut self.connection, self.service, self.deployment),
        )
        .await;
        match result {
            Ok(Ok(())) => {
                let extension = self.status.extend(started + LIFETIME);
                if extension.is_err() {
                    eprintln!(
                        "PostgreSQL service lease renewal rejected: deadline expired during probe"
                    );
                }
                extension
            }
            Ok(Err(error)) => {
                eprintln!("PostgreSQL service lease renewal probe failed: {error}");
                self.status.fail();
                Err(error)
            }
            Err(_) => {
                eprintln!("PostgreSQL service lease renewal probe timed out");
                self.status.fail();
                Err(DatabaseError::Unavailable)
            }
        }
    }
    #[cfg(feature = "pg-integration")]
    pub fn test_backend_pid(&self) -> i32 {
        self.backend_pid
    }
}
impl Drop for ServiceLease {
    fn drop(&mut self) {
        self.status.fail();
    }
}
