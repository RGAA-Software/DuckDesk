use crate::{model::valid_key_id, DeploymentIdentityError};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeSet;

const MAX_TRUST_STORE_BYTES: usize = 64 * 1024;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TrustedVendorKey {
    pub key_id: String,
    pub public_key_hex: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeploymentTrustStore {
    pub schema_version: u16,
    pub trust_epoch: u64,
    pub trusted_keys: Vec<TrustedVendorKey>,
}

impl DeploymentTrustStore {
    pub fn new(
        trust_epoch: u64,
        public_keys: impl IntoIterator<Item = [u8; 32]>,
    ) -> Result<Self, DeploymentIdentityError> {
        let mut trusted_keys = public_keys
            .into_iter()
            .map(|public_key| TrustedVendorKey {
                key_id: key_id(&public_key),
                public_key_hex: hex::encode(public_key),
            })
            .collect::<Vec<_>>();
        trusted_keys.sort_by(|left, right| left.key_id.cmp(&right.key_id));
        let store = Self {
            schema_version: 1,
            trust_epoch,
            trusted_keys,
        };
        store.validate()?;
        Ok(store)
    }

    pub fn from_canonical_bytes(bytes: &[u8]) -> Result<Self, DeploymentIdentityError> {
        if bytes.is_empty() || bytes.len() > MAX_TRUST_STORE_BYTES {
            return Err(DeploymentIdentityError::Key);
        }
        let store =
            serde_json::from_slice::<Self>(bytes).map_err(|_| DeploymentIdentityError::Key)?;
        store.validate()?;
        if store.canonical_bytes()? != bytes {
            return Err(DeploymentIdentityError::Key);
        }
        Ok(store)
    }

    pub fn canonical_bytes(&self) -> Result<Vec<u8>, DeploymentIdentityError> {
        self.validate()?;
        serde_json::to_vec(self).map_err(|_| DeploymentIdentityError::Key)
    }

    pub(crate) fn public_keys(&self) -> Result<Vec<(String, [u8; 32])>, DeploymentIdentityError> {
        self.validate()?;
        self.trusted_keys
            .iter()
            .map(|trusted_key| {
                let bytes = hex::decode(&trusted_key.public_key_hex)
                    .map_err(|_| DeploymentIdentityError::Key)?;
                let public_key: [u8; 32] =
                    bytes.try_into().map_err(|_| DeploymentIdentityError::Key)?;
                Ok((trusted_key.key_id.clone(), public_key))
            })
            .collect()
    }

    fn validate(&self) -> Result<(), DeploymentIdentityError> {
        if self.schema_version != 1
            || self.trust_epoch == 0
            || self.trusted_keys.is_empty()
            || self.trusted_keys.len() > 16
        {
            return Err(DeploymentIdentityError::Key);
        }
        let mut key_ids = BTreeSet::new();
        let mut previous_key_id: Option<&str> = None;
        for trusted_key in &self.trusted_keys {
            let bytes = hex::decode(&trusted_key.public_key_hex)
                .map_err(|_| DeploymentIdentityError::Key)?;
            let public_key: [u8; 32] =
                bytes.try_into().map_err(|_| DeploymentIdentityError::Key)?;
            if public_key == [0; 32]
                || !valid_key_id(&trusted_key.key_id)
                || key_id(&public_key) != trusted_key.key_id
                || previous_key_id.is_some_and(|previous| previous >= trusted_key.key_id.as_str())
                || !key_ids.insert(trusted_key.key_id.as_str())
            {
                return Err(DeploymentIdentityError::Key);
            }
            previous_key_id = Some(&trusted_key.key_id);
        }
        Ok(())
    }
}

pub(crate) fn key_id(public_key: &[u8]) -> String {
    hex::encode(Sha256::digest(public_key))
}
