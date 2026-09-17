use crate::{payload::hash_text, Distribution, LicenseError, LicensePayload, Product};
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine};
use ring::signature::{Ed25519KeyPair, KeyPair, UnparsedPublicKey, ED25519};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use uuid::Uuid;

const PREFIX: &str = "PXLIC1";
const DOMAIN: &[u8] = b"Pixels-License-v1\0";
const MAX_WIRE_BYTES: usize = 8192;
fn message(payload: &[u8]) -> Vec<u8> {
    let mut message = Vec::with_capacity(DOMAIN.len() + payload.len());
    message.extend_from_slice(DOMAIN);
    message.extend_from_slice(payload);
    message
}
fn key_id(public: &[u8]) -> String {
    hex::encode(Sha256::digest(public))
}

pub struct LicenseSigner {
    key: Ed25519KeyPair,
}
impl LicenseSigner {
    /// Key provisioning is explicit and external; never silently generate a replacement key.
    pub fn from_pkcs8(bytes: &[u8]) -> Result<Self, LicenseError> {
        Ok(Self {
            key: Ed25519KeyPair::from_pkcs8(bytes).map_err(|_| LicenseError::Key)?,
        })
    }
    pub fn key_id(&self) -> String {
        key_id(self.key.public_key().as_ref())
    }
    pub fn public_key(&self) -> &[u8] {
        self.key.public_key().as_ref()
    }
    pub fn sign(&self, payload: &LicensePayload) -> Result<String, LicenseError> {
        if payload.key_id != self.key_id() {
            return Err(LicenseError::Key);
        }
        let bytes = payload.canonical_bytes()?;
        let signature = self.key.sign(&message(&bytes));
        Ok(format!(
            "{PREFIX}.{}.{}",
            URL_SAFE_NO_PAD.encode(bytes),
            URL_SAFE_NO_PAD.encode(signature.as_ref())
        ))
    }
}

pub struct VerifyContext<'a> {
    pub deployment_id: Uuid,
    pub product: Product,
    pub distribution: Distribution,
    pub machine_sha256: &'a str,
    pub now: i64,
    /// Independently retained trust state; restoring an old DB must not lower either value.
    pub minimum_revision: i64,
    pub last_trusted_time: i64,
}
pub struct LicenseVerifierSet {
    public_keys: BTreeMap<String, [u8; 32]>,
}
impl LicenseVerifierSet {
    /// Public keys come from the configured trust root, never from a license/download response.
    pub fn new(public_keys: impl IntoIterator<Item = [u8; 32]>) -> Result<Self, LicenseError> {
        let mut indexed_keys = BTreeMap::new();
        for public_key in public_keys {
            if public_key == [0; 32] {
                return Err(LicenseError::Key);
            }
            let public_key_id = key_id(&public_key);
            if indexed_keys.insert(public_key_id, public_key).is_some() {
                return Err(LicenseError::Key);
            }
        }
        if indexed_keys.is_empty() {
            return Err(LicenseError::Key);
        }
        Ok(Self {
            public_keys: indexed_keys,
        })
    }
    pub fn verify(
        &self,
        wire: &str,
        context: &VerifyContext<'_>,
    ) -> Result<LicensePayload, LicenseError> {
        if wire.len() > MAX_WIRE_BYTES {
            return Err(LicenseError::Invalid);
        }
        let mut parts = wire.split('.');
        if parts.next() != Some(PREFIX) {
            return Err(LicenseError::Invalid);
        }
        let encoded = parts.next().ok_or(LicenseError::Invalid)?;
        let encoded_signature = parts.next().ok_or(LicenseError::Invalid)?;
        if parts.next().is_some() {
            return Err(LicenseError::Invalid);
        }
        let bytes = URL_SAFE_NO_PAD
            .decode(encoded)
            .map_err(|_| LicenseError::Invalid)?;
        let signature = URL_SAFE_NO_PAD
            .decode(encoded_signature)
            .map_err(|_| LicenseError::Invalid)?;
        if signature.len() != 64 {
            return Err(LicenseError::Invalid);
        }
        let untrusted_payload: LicensePayload =
            serde_json::from_slice(&bytes).map_err(|_| LicenseError::Invalid)?;
        let public_key = self
            .public_keys
            .get(&untrusted_payload.key_id)
            .ok_or(LicenseError::Key)?;
        UnparsedPublicKey::new(&ED25519, public_key)
            .verify(&message(&bytes), &signature)
            .map_err(|_| LicenseError::Signature)?;
        // Reject whitespace, reordered/duplicate/unknown keys, alternative numbers/UUID spelling.
        if untrusted_payload.canonical_bytes()? != bytes {
            return Err(LicenseError::Invalid);
        }
        if context.minimum_revision < 1
            || context.last_trusted_time < 0
            || context.now < context.last_trusted_time
            || context.deployment_id.is_nil()
            || !hash_text(context.machine_sha256)
            || untrusted_payload.deployment_id != context.deployment_id
            || untrusted_payload.product != context.product
            || untrusted_payload.distribution != context.distribution
            || untrusted_payload.machine_sha256 != context.machine_sha256
            || untrusted_payload.revision < context.minimum_revision
            || untrusted_payload.not_before > context.now
            || untrusted_payload.expires_at <= context.now
        {
            return Err(LicenseError::Rejected);
        }
        Ok(untrusted_payload)
    }
}
