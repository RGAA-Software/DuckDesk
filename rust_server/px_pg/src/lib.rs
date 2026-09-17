//! PostgreSQL connection, identity and schema checks. Domain queries belong to services.
mod config;
mod error;
mod lease;
mod runtime;
mod schema;

pub use config::{DatabaseConfig, Transport};
pub use error::DatabaseError;
pub use lease::{LeaseStatus, ServiceLease};
pub use runtime::runtime_readiness;
pub use schema::{migrate, readiness, Service};
pub use sqlx::PgPool;
