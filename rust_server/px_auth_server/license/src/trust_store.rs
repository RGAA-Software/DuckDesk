use crate::{LicenseError, LicenseSigner, LicenseVerifierSet};
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;
use uuid::Uuid;

const TRUST_STORE_SCHEMA_VERSION: u16 = 1;
const MAX_TRUST_STORE_BYTES: usize = 64 * 1024;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TrustedPublicKey {
    pub key_id: String,
    pub public_key_hex: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct LicenseTrustStore {
    pub schema_version: u16,
    pub authority_deployment_id: Uuid,
    pub recovery_generation: Uuid,
    pub active_key_id: String,
    pub trusted_keys: Vec<TrustedPublicKey>,
}

impl LicenseTrustStore {
    pub fn new(
        authority_deployment_id: Uuid,
        recovery_generation: Uuid,
        active_public_key: [u8; 32],
        additional_public_keys: impl IntoIterator<Item = [u8; 32]>,
    ) -> Result<Self, LicenseError> {
        let active_key_id = key_id(&active_public_key);
        let mut public_keys = additional_public_keys.into_iter().collect::<Vec<_>>();
        public_keys.push(active_public_key);
        public_keys.sort_by_key(|public_key| key_id(public_key));
        let trusted_keys = public_keys
            .into_iter()
            .map(|public_key| TrustedPublicKey {
                key_id: key_id(&public_key),
                public_key_hex: hex::encode(public_key),
            })
            .collect();
        let trust_store = Self {
            schema_version: TRUST_STORE_SCHEMA_VERSION,
            authority_deployment_id,
            recovery_generation,
            active_key_id,
            trusted_keys,
        };
        trust_store.validate()?;
        Ok(trust_store)
    }

    pub fn from_canonical_bytes(bytes: &[u8]) -> Result<Self, LicenseError> {
        if bytes.is_empty() || bytes.len() > MAX_TRUST_STORE_BYTES {
            return Err(LicenseError::Key);
        }
        let trust_store = serde_json::from_slice::<Self>(bytes).map_err(|_| LicenseError::Key)?;
        trust_store.validate()?;
        if trust_store.canonical_bytes()? != bytes {
            return Err(LicenseError::Key);
        }
        Ok(trust_store)
    }

    pub fn canonical_bytes(&self) -> Result<Vec<u8>, LicenseError> {
        self.validate()?;
        serde_json::to_vec(self).map_err(|_| LicenseError::Key)
    }

    pub fn verifier_set(&self) -> Result<LicenseVerifierSet, LicenseError> {
        LicenseVerifierSet::new(self.decode_public_keys()?)
    }

    pub fn verify_active_signer(&self, signer: &LicenseSigner) -> Result<(), LicenseError> {
        if signer.key_id() != self.active_key_id {
            return Err(LicenseError::Key);
        }
        let active_public_key = hex::encode(signer.public_key());
        if !self.trusted_keys.iter().any(|trusted_key| {
            trusted_key.key_id == self.active_key_id
                && trusted_key.public_key_hex == active_public_key
        }) {
            return Err(LicenseError::Key);
        }
        Ok(())
    }

    fn validate(&self) -> Result<(), LicenseError> {
        if self.schema_version != TRUST_STORE_SCHEMA_VERSION
            || self.authority_deployment_id.is_nil()
            || self.recovery_generation.is_nil()
            || !valid_key_id(&self.active_key_id)
            || self.trusted_keys.is_empty()
            || self.trusted_keys.len() > 16
        {
            return Err(LicenseError::Key);
        }
        let mut previous_key_id = None;
        let mut key_ids = BTreeSet::new();
        for trusted_key in &self.trusted_keys {
            if !valid_key_id(&trusted_key.key_id)
                || previous_key_id.is_some_and(|previous| previous >= trusted_key.key_id.as_str())
            {
                return Err(LicenseError::Key);
            }
            let public_key = decode_public_key(&trusted_key.public_key_hex)?;
            if key_id(&public_key) != trusted_key.key_id || !key_ids.insert(&trusted_key.key_id) {
                return Err(LicenseError::Key);
            }
            previous_key_id = Some(trusted_key.key_id.as_str());
        }
        if !key_ids.contains(&self.active_key_id) {
            return Err(LicenseError::Key);
        }
        Ok(())
    }

    fn decode_public_keys(&self) -> Result<Vec<[u8; 32]>, LicenseError> {
        self.trusted_keys
            .iter()
            .map(|trusted_key| decode_public_key(&trusted_key.public_key_hex))
            .collect()
    }
}

fn decode_public_key(public_key_hex: &str) -> Result<[u8; 32], LicenseError> {
    let public_key_bytes = hex::decode(public_key_hex).map_err(|_| LicenseError::Key)?;
    public_key_bytes.try_into().map_err(|_| LicenseError::Key)
}

fn valid_key_id(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn key_id(public_key: &[u8]) -> String {
    use sha2::{Digest, Sha256};
    hex::encode(Sha256::digest(public_key))
}
