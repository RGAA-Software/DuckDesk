use crate::LicenseError;
pub use px_release_catalog::Distribution;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Product {
    PixelsConsole,
    Gopico,
    Clientbox,
    Goagent,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Mode {
    Trial,
    Licensed,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Feature {
    CloudApplications,
    Desktop,
    Rdp,
}

/// Declaration order is the canonical compact JSON field order.
/// All fields are mandatory; no defaults or skipped fields.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LicensePayload {
    pub schema: u16,
    pub license_id: Uuid,
    pub deployment_id: Uuid,
    pub product: Product,
    pub distribution: Distribution,
    pub release_namespace: String,
    pub oem_id: Option<String>,
    pub machine_sha256: String,
    pub revision: i64,
    pub mode: Mode,
    pub issued_at: i64,
    pub not_before: i64,
    pub expires_at: i64,
    pub max_devices: u32,
    pub max_sessions: u32,
    pub features: Vec<Feature>,
    pub key_id: String,
}

pub(crate) fn hash_text(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte_value| byte_value.is_ascii_digit() || (b'a'..=b'f').contains(&byte_value))
}
impl LicensePayload {
    pub fn validate(&self) -> Result<(), LicenseError> {
        if self.schema != 2
            || self.license_id.is_nil()
            || self.deployment_id.is_nil()
            || !hash_text(&self.machine_sha256)
            || !hash_text(&self.key_id)
            || self.revision < 1
            || self.issued_at < 0
            || self.not_before < self.issued_at
            || self.expires_at <= self.not_before
            || self.expires_at > 253402300799
            || self.max_devices == 0
            || self.max_sessions == 0
            || self.features.is_empty()
            || self.features.len() > 3
            || self.features.windows(2).any(|pair| pair[0] >= pair[1])
        {
            return Err(LicenseError::Invalid);
        }
        self.distribution
            .validate_release_domain(&self.release_namespace, self.oem_id.as_deref())
            .map_err(|_| LicenseError::Invalid)?;
        Ok(())
    }
    pub fn canonical_bytes(&self) -> Result<Vec<u8>, LicenseError> {
        self.validate()?;
        serde_json::to_vec(self).map_err(|_| LicenseError::Invalid)
    }
}
