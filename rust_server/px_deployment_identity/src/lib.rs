//! Signed deployment identity contract shared by Pixels servers and clients.
//! This trust boundary is independent from commercial licenses and user authentication.

mod model;
mod signature;
mod trust_store;

pub use model::{
    AuthenticationMethod, ChallengePayload, DeploymentCertificate, DeploymentKind,
    PlatformDescriptor, RegistrationPolicy, SignedDeploymentIdentity,
};
pub use signature::{
    sign_certificate, DeploymentIdentitySigner, DeploymentIdentityVerifier,
    DeploymentVerificationContext, VerifiedDeploymentIdentity,
};
pub use trust_store::{DeploymentTrustStore, TrustedVendorKey};

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum DeploymentIdentityError {
    #[error("invalid deployment identity payload or wire encoding")]
    Invalid,
    #[error("invalid deployment identity signing key")]
    Key,
    #[error("invalid deployment identity signature")]
    Signature,
    #[error("deployment identity binding, validity or revision rejected")]
    Rejected,
}
