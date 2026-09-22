//! The new license wire contract shared by issuer and consumers.
//! No HTTP/database dependency, legacy parser, product alias or embedded administrator secret.
mod payload;
mod signature;
mod trust_store;

pub use payload::{Distribution, LicensePayload, LicensedService, Mode, Product};
pub use signature::{LicenseSigner, LicenseVerifierSet, VerifyContext};
pub use trust_store::{LicenseTrustStore, TrustedPublicKey};

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum LicenseError {
    #[error("invalid license payload or wire encoding")]
    Invalid,
    #[error("invalid license key")]
    Key,
    #[error("invalid license signature")]
    Signature,
    #[error("license binding, time or revision rejected")]
    Rejected,
}
