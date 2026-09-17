//! The new license wire contract shared by issuer and consumers.
//! No HTTP/database dependency, legacy parser, product alias or embedded administrator secret.
mod payload;
mod signature;

pub use payload::{Distribution, Feature, LicensePayload, Mode, Product};
pub use signature::{LicenseSigner, LicenseVerifier, VerifyContext};

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
