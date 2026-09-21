use crate::{
    model::decode_public_key, trust_store::key_id, ChallengePayload, DeploymentCertificate,
    DeploymentIdentityError, DeploymentKind, DeploymentTrustStore, Distribution,
    PlatformDescriptor, SignedDeploymentIdentity,
};
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use ring::signature::{Ed25519KeyPair, KeyPair, UnparsedPublicKey, ED25519};
use std::collections::BTreeMap;
use uuid::Uuid;

const CERTIFICATE_PREFIX: &str = "PXDC2";
const CERTIFICATE_DOMAIN: &[u8] = b"Pixels-Deployment-Certificate-v2\0";
const DESCRIPTOR_PREFIX: &str = "PXDD2";
const DESCRIPTOR_DOMAIN: &[u8] = b"Pixels-Platform-Descriptor-v2\0";
const CHALLENGE_PREFIX: &str = "PXDP1";
const CHALLENGE_DOMAIN: &[u8] = b"Pixels-Deployment-Challenge-v1\0";
const MAX_WIRE_BYTES: usize = 16 * 1024;

pub struct DeploymentIdentitySigner {
    deployment_key: Ed25519KeyPair,
}

pub struct DeploymentVerificationContext {
    pub expected_deployment_id: Option<Uuid>,
    pub expected_kind: DeploymentKind,
    pub expected_distribution: Distribution,
    pub expected_release_namespace: String,
    pub expected_oem_id: Option<String>,
    pub now: i64,
    pub minimum_certificate_version: u64,
    pub minimum_descriptor_revision: u64,
    pub minimum_trust_epoch: u64,
    pub client_build: u64,
    pub protocol_version: u16,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VerifiedDeploymentIdentity {
    pub certificate: DeploymentCertificate,
    pub descriptor: PlatformDescriptor,
}

pub struct DeploymentIdentityVerifier {
    vendor_public_keys: BTreeMap<String, [u8; 32]>,
}

impl DeploymentIdentitySigner {
    pub fn from_pkcs8(bytes: &[u8]) -> Result<Self, DeploymentIdentityError> {
        Ok(Self {
            deployment_key: Ed25519KeyPair::from_pkcs8(bytes)
                .map_err(|_| DeploymentIdentityError::Key)?,
        })
    }

    pub fn public_key(&self) -> &[u8] {
        self.deployment_key.public_key().as_ref()
    }

    pub fn sign_descriptor(
        &self,
        certificate: &DeploymentCertificate,
        descriptor: &PlatformDescriptor,
    ) -> Result<String, DeploymentIdentityError> {
        self.verify_certificate_key(certificate)?;
        if descriptor.deployment_id != certificate.deployment_id
            || descriptor.deployment_kind != certificate.deployment_kind
            || descriptor.distribution != certificate.distribution
            || descriptor.release_namespace != certificate.release_namespace
            || descriptor.oem_id != certificate.oem_id
        {
            return Err(DeploymentIdentityError::Rejected);
        }
        sign_wire(
            DESCRIPTOR_PREFIX,
            DESCRIPTOR_DOMAIN,
            &descriptor.canonical_bytes()?,
            &self.deployment_key,
        )
    }

    pub fn sign_challenge(
        &self,
        certificate: &DeploymentCertificate,
        challenge: &ChallengePayload,
    ) -> Result<String, DeploymentIdentityError> {
        self.verify_certificate_key(certificate)?;
        if challenge.deployment_id != certificate.deployment_id {
            return Err(DeploymentIdentityError::Rejected);
        }
        sign_wire(
            CHALLENGE_PREFIX,
            CHALLENGE_DOMAIN,
            &challenge.canonical_bytes()?,
            &self.deployment_key,
        )
    }

    fn verify_certificate_key(
        &self,
        certificate: &DeploymentCertificate,
    ) -> Result<(), DeploymentIdentityError> {
        certificate.validate()?;
        if certificate.deployment_public_key_hex != hex::encode(self.public_key()) {
            return Err(DeploymentIdentityError::Key);
        }
        Ok(())
    }
}

impl DeploymentIdentityVerifier {
    pub fn new(trust_store: &DeploymentTrustStore) -> Result<Self, DeploymentIdentityError> {
        let vendor_public_keys = trust_store.public_keys()?.into_iter().collect();
        Ok(Self { vendor_public_keys })
    }

    pub fn verify_identity(
        &self,
        identity: &SignedDeploymentIdentity,
        context: &DeploymentVerificationContext,
    ) -> Result<VerifiedDeploymentIdentity, DeploymentIdentityError> {
        validate_context(context)?;
        let certificate = self.verify_certificate(
            &identity.certificate_wire,
            context.expected_deployment_id,
            context.expected_kind,
            context.expected_distribution,
            &context.expected_release_namespace,
            context.expected_oem_id.as_deref(),
            context.now,
            context.minimum_certificate_version,
        )?;
        let deployment_public_key = decode_public_key(&certificate.deployment_public_key_hex)?;
        let descriptor: PlatformDescriptor = verify_wire_with_key(
            &identity.descriptor_wire,
            DESCRIPTOR_PREFIX,
            DESCRIPTOR_DOMAIN,
            deployment_public_key,
        )?;
        descriptor.canonical_bytes()?;
        if descriptor.deployment_id != certificate.deployment_id
            || descriptor.deployment_kind != certificate.deployment_kind
            || descriptor.distribution != certificate.distribution
            || descriptor.release_namespace != certificate.release_namespace
            || descriptor.oem_id != certificate.oem_id
            || descriptor.descriptor_revision < context.minimum_descriptor_revision
            || descriptor.trust_epoch < context.minimum_trust_epoch
            || descriptor.issued_at > context.now
            || descriptor.expires_at <= context.now
            || descriptor.minimum_client_build > context.client_build
            || context.protocol_version < descriptor.minimum_protocol_version
            || context.protocol_version > descriptor.maximum_protocol_version
        {
            return Err(DeploymentIdentityError::Rejected);
        }
        Ok(VerifiedDeploymentIdentity {
            certificate,
            descriptor,
        })
    }

    pub fn verify_certificate(
        &self,
        wire: &str,
        expected_deployment_id: Option<Uuid>,
        expected_kind: DeploymentKind,
        expected_distribution: Distribution,
        expected_release_namespace: &str,
        expected_oem_id: Option<&str>,
        now: i64,
        minimum_certificate_version: u64,
    ) -> Result<DeploymentCertificate, DeploymentIdentityError> {
        if now < 0 || minimum_certificate_version == 0 {
            return Err(DeploymentIdentityError::Rejected);
        }
        let certificate: DeploymentCertificate =
            verify_wire(wire, CERTIFICATE_PREFIX, CERTIFICATE_DOMAIN, |untrusted| {
                let certificate: DeploymentCertificate = serde_json::from_slice(untrusted)
                    .map_err(|_| DeploymentIdentityError::Invalid)?;
                let public_key = self
                    .vendor_public_keys
                    .get(&certificate.issuer_key_id)
                    .ok_or(DeploymentIdentityError::Key)?;
                Ok((certificate, *public_key))
            })?;
        certificate.validate()?;
        if expected_deployment_id.is_some_and(|expected| expected != certificate.deployment_id)
            || certificate.deployment_kind != expected_kind
            || certificate.distribution != expected_distribution
            || certificate.release_namespace != expected_release_namespace
            || certificate.oem_id.as_deref() != expected_oem_id
            || certificate.certificate_version < minimum_certificate_version
            || certificate.not_before > now
            || certificate.expires_at <= now
        {
            return Err(DeploymentIdentityError::Rejected);
        }
        Ok(certificate)
    }

    pub fn verify_challenge(
        &self,
        verified_identity: &VerifiedDeploymentIdentity,
        wire: &str,
        expected_nonce: &str,
        now: i64,
    ) -> Result<ChallengePayload, DeploymentIdentityError> {
        let deployment_public_key =
            decode_public_key(&verified_identity.certificate.deployment_public_key_hex)?;
        let challenge: ChallengePayload = verify_wire_with_key(
            wire,
            CHALLENGE_PREFIX,
            CHALLENGE_DOMAIN,
            deployment_public_key,
        )?;
        challenge.canonical_bytes()?;
        if challenge.deployment_id != verified_identity.certificate.deployment_id
            || challenge.descriptor_revision != verified_identity.descriptor.descriptor_revision
            || challenge.nonce != expected_nonce
            || challenge.issued_at > now
            || challenge.expires_at <= now
        {
            return Err(DeploymentIdentityError::Rejected);
        }
        Ok(challenge)
    }
}

pub fn sign_certificate(
    vendor_pkcs8: &[u8],
    certificate: &DeploymentCertificate,
) -> Result<String, DeploymentIdentityError> {
    let vendor_key =
        Ed25519KeyPair::from_pkcs8(vendor_pkcs8).map_err(|_| DeploymentIdentityError::Key)?;
    if certificate.issuer_key_id != key_id(vendor_key.public_key().as_ref()) {
        return Err(DeploymentIdentityError::Key);
    }
    sign_wire(
        CERTIFICATE_PREFIX,
        CERTIFICATE_DOMAIN,
        &certificate.canonical_bytes()?,
        &vendor_key,
    )
}

fn validate_context(
    context: &DeploymentVerificationContext,
) -> Result<(), DeploymentIdentityError> {
    if context.now < 0
        || context.minimum_certificate_version == 0
        || context.minimum_descriptor_revision == 0
        || context.minimum_trust_epoch == 0
        || context.client_build == 0
        || context.protocol_version == 0
        || context
            .expected_distribution
            .validate_release_domain(
                &context.expected_release_namespace,
                context.expected_oem_id.as_deref(),
            )
            .is_err()
    {
        return Err(DeploymentIdentityError::Rejected);
    }
    Ok(())
}

fn sign_wire(
    prefix: &str,
    domain: &[u8],
    payload: &[u8],
    key: &Ed25519KeyPair,
) -> Result<String, DeploymentIdentityError> {
    let signature = key.sign(&message(domain, payload));
    Ok(format!(
        "{prefix}.{}.{}",
        URL_SAFE_NO_PAD.encode(payload),
        URL_SAFE_NO_PAD.encode(signature.as_ref())
    ))
}

fn verify_wire<T>(
    wire: &str,
    prefix: &str,
    domain: &[u8],
    parse_and_key: impl FnOnce(&[u8]) -> Result<(T, [u8; 32]), DeploymentIdentityError>,
) -> Result<T, DeploymentIdentityError>
where
    T: serde::Serialize,
{
    let (payload, signature) = decode_wire(wire, prefix)?;
    let (value, public_key) = parse_and_key(&payload)?;
    verify_signature(domain, &payload, &signature, public_key)?;
    if serde_json::to_vec(&value).map_err(|_| DeploymentIdentityError::Invalid)? != payload {
        return Err(DeploymentIdentityError::Invalid);
    }
    Ok(value)
}

fn verify_wire_with_key<T>(
    wire: &str,
    prefix: &str,
    domain: &[u8],
    public_key: [u8; 32],
) -> Result<T, DeploymentIdentityError>
where
    T: serde::de::DeserializeOwned + serde::Serialize,
{
    let (payload, signature) = decode_wire(wire, prefix)?;
    verify_signature(domain, &payload, &signature, public_key)?;
    let value: T =
        serde_json::from_slice(&payload).map_err(|_| DeploymentIdentityError::Invalid)?;
    if serde_json::to_vec(&value).map_err(|_| DeploymentIdentityError::Invalid)? != payload {
        return Err(DeploymentIdentityError::Invalid);
    }
    Ok(value)
}

fn decode_wire(wire: &str, prefix: &str) -> Result<(Vec<u8>, Vec<u8>), DeploymentIdentityError> {
    if wire.is_empty() || wire.len() > MAX_WIRE_BYTES {
        return Err(DeploymentIdentityError::Invalid);
    }
    let mut parts = wire.split('.');
    if parts.next() != Some(prefix) {
        return Err(DeploymentIdentityError::Invalid);
    }
    let payload = URL_SAFE_NO_PAD
        .decode(parts.next().ok_or(DeploymentIdentityError::Invalid)?)
        .map_err(|_| DeploymentIdentityError::Invalid)?;
    let signature = URL_SAFE_NO_PAD
        .decode(parts.next().ok_or(DeploymentIdentityError::Invalid)?)
        .map_err(|_| DeploymentIdentityError::Invalid)?;
    if parts.next().is_some() || signature.len() != 64 {
        return Err(DeploymentIdentityError::Invalid);
    }
    Ok((payload, signature))
}

fn verify_signature(
    domain: &[u8],
    payload: &[u8],
    signature: &[u8],
    public_key: [u8; 32],
) -> Result<(), DeploymentIdentityError> {
    UnparsedPublicKey::new(&ED25519, public_key)
        .verify(&message(domain, payload), signature)
        .map_err(|_| DeploymentIdentityError::Signature)
}

fn message(domain: &[u8], payload: &[u8]) -> Vec<u8> {
    let mut message = Vec::with_capacity(domain.len() + payload.len());
    message.extend_from_slice(domain);
    message.extend_from_slice(payload);
    message
}
