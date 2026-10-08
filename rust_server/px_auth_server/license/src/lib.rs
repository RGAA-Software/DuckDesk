//! The new license wire contract shared by issuer and consumers.
//! No HTTP/database dependency, legacy parser, product alias or embedded administrator secret.
mod payload;
mod signature;
mod trust_store;

pub use payload::{LicensePayload, LicensedService};
pub use signature::{LicenseSigner, LicenseVerifierSet, VerifyContext};
pub use trust_store::{LicenseTrustStore, TrustedPublicKey};

/// Auth-signed, portable starter entitlement included with every Single Server package.
/// Paid licenses remain bound to the actual Console deployment.
pub const STARTER_DEPLOYMENT_ID: uuid::Uuid =
    uuid::Uuid::from_u128(0x2e267c3181044385a6fc1d2438f0b3d6);
pub const MAX_ISSUED_AT_CLOCK_SKEW_SECONDS: i64 = 30;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum LicenseError {
    #[error("invalid license payload or wire encoding")]
    Invalid,
    #[error("invalid license key")]
    Key,
    #[error("invalid license signature")]
    Signature,
    #[error("license binding or time rejected")]
    Rejected,
}
