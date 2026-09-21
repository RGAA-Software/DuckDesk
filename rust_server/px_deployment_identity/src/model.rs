use crate::DeploymentIdentityError;
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use px_release_catalog::Distribution;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeploymentKind {
    Official,
    Private,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum AuthenticationMethod {
    Guest,
    Password,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RegistrationPolicy {
    Closed,
    Open,
}

/// Declaration order is the canonical compact JSON field order.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeploymentCertificate {
    pub schema_version: u16,
    pub deployment_id: Uuid,
    pub deployment_kind: DeploymentKind,
    pub distribution: Distribution,
    pub release_namespace: String,
    pub oem_id: Option<String>,
    pub deployment_public_key_hex: String,
    pub certificate_version: u64,
    pub not_before: i64,
    pub expires_at: i64,
    pub issuer_key_id: String,
}

/// Public platform capabilities. Endpoint paths remain relative to the already verified origin.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PlatformDescriptor {
    pub schema_version: u16,
    pub deployment_id: Uuid,
    pub deployment_kind: DeploymentKind,
    pub distribution: Distribution,
    pub release_namespace: String,
    pub oem_id: Option<String>,
    pub descriptor_revision: u64,
    pub trust_epoch: u64,
    pub issued_at: i64,
    pub expires_at: i64,
    pub minimum_client_build: u64,
    pub api_versions: Vec<String>,
    pub minimum_protocol_version: u16,
    pub maximum_protocol_version: u16,
    pub authentication_methods: Vec<AuthenticationMethod>,
    pub registration_policy: RegistrationPolicy,
    pub console_api_path: String,
    pub node_control_path: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct SignedDeploymentIdentity {
    pub certificate_wire: String,
    pub descriptor_wire: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ChallengePayload {
    pub schema_version: u16,
    pub deployment_id: Uuid,
    pub descriptor_revision: u64,
    pub nonce: String,
    pub issued_at: i64,
    pub expires_at: i64,
}

impl DeploymentCertificate {
    pub(crate) fn canonical_bytes(&self) -> Result<Vec<u8>, DeploymentIdentityError> {
        self.validate()?;
        serde_json::to_vec(self).map_err(|_| DeploymentIdentityError::Invalid)
    }

    pub(crate) fn validate(&self) -> Result<(), DeploymentIdentityError> {
        if self.schema_version != 2
            || self.deployment_id.is_nil()
            || !valid_release_domain(
                self.deployment_kind,
                self.distribution,
                &self.release_namespace,
                self.oem_id.as_deref(),
            )
            || decode_public_key(&self.deployment_public_key_hex).is_err()
            || self.certificate_version == 0
            || self.not_before < 0
            || self.expires_at <= self.not_before
            || self.expires_at > 253_402_300_799
            || !valid_key_id(&self.issuer_key_id)
        {
            return Err(DeploymentIdentityError::Invalid);
        }
        Ok(())
    }
}

impl PlatformDescriptor {
    pub(crate) fn canonical_bytes(&self) -> Result<Vec<u8>, DeploymentIdentityError> {
        self.validate()?;
        serde_json::to_vec(self).map_err(|_| DeploymentIdentityError::Invalid)
    }

    fn validate(&self) -> Result<(), DeploymentIdentityError> {
        if self.schema_version != 2
            || self.deployment_id.is_nil()
            || !valid_release_domain(
                self.deployment_kind,
                self.distribution,
                &self.release_namespace,
                self.oem_id.as_deref(),
            )
            || self.descriptor_revision == 0
            || self.trust_epoch == 0
            || self.issued_at < 0
            || self.expires_at <= self.issued_at
            || self.expires_at - self.issued_at > 86_400
            || self.api_versions.is_empty()
            || self.api_versions.len() > 16
            || self
                .api_versions
                .iter()
                .any(|version| !valid_token(version))
            || self.api_versions.windows(2).any(|pair| pair[0] >= pair[1])
            || self.minimum_protocol_version == 0
            || self.maximum_protocol_version < self.minimum_protocol_version
            || self.authentication_methods.is_empty()
            || self.authentication_methods.len() > 8
            || self
                .authentication_methods
                .windows(2)
                .any(|pair| pair[0] >= pair[1])
            || self.console_api_path != "/api/console"
            || self.node_control_path != "/api/console/node-control"
        {
            return Err(DeploymentIdentityError::Invalid);
        }
        Ok(())
    }
}

impl ChallengePayload {
    pub(crate) fn canonical_bytes(&self) -> Result<Vec<u8>, DeploymentIdentityError> {
        self.validate()?;
        serde_json::to_vec(self).map_err(|_| DeploymentIdentityError::Invalid)
    }

    fn validate(&self) -> Result<(), DeploymentIdentityError> {
        let decoded_nonce = URL_SAFE_NO_PAD
            .decode(&self.nonce)
            .map_err(|_| DeploymentIdentityError::Invalid)?;
        if self.schema_version != 1
            || self.deployment_id.is_nil()
            || self.descriptor_revision == 0
            || decoded_nonce.len() != 32
            || URL_SAFE_NO_PAD.encode(decoded_nonce) != self.nonce
            || self.issued_at < 0
            || self.expires_at <= self.issued_at
            || self.expires_at - self.issued_at > 60
        {
            return Err(DeploymentIdentityError::Invalid);
        }
        Ok(())
    }
}

pub(crate) fn decode_public_key(value: &str) -> Result<[u8; 32], DeploymentIdentityError> {
    let bytes = hex::decode(value).map_err(|_| DeploymentIdentityError::Key)?;
    let key: [u8; 32] = bytes.try_into().map_err(|_| DeploymentIdentityError::Key)?;
    if key == [0; 32] || hex::encode(key) != value {
        return Err(DeploymentIdentityError::Key);
    }
    Ok(key)
}

pub(crate) fn valid_key_id(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn valid_token(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 32
        && value.bytes().all(|byte| {
            byte.is_ascii_lowercase() || byte.is_ascii_digit() || matches!(byte, b'.' | b'-' | b'_')
        })
}

fn valid_release_domain(
    deployment_kind: DeploymentKind,
    distribution: Distribution,
    release_namespace: &str,
    oem_id: Option<&str>,
) -> bool {
    let expected_kind = match distribution {
        Distribution::Official => DeploymentKind::Official,
        Distribution::Customer | Distribution::Oem => DeploymentKind::Private,
    };
    deployment_kind == expected_kind
        && distribution
            .validate_release_domain(release_namespace, oem_id)
            .is_ok()
}
