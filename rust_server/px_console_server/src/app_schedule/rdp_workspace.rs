//! Persistent RDP account identity. Never serialize plaintext secrets into application DTOs.

use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use mongodb::{bson::doc, Collection};
use ring::{aead, rand::{SecureRandom, SystemRandom}};
use serde::{Deserialize, Serialize};
use std::path::Path;
use std::io::Read;
use uuid::Uuid;
use zeroize::Zeroizing;

const SECRET_SCHEMA: u32 = 1;
const MAX_IDENTIFIER: usize = 128;

#[derive(Clone, Serialize, Deserialize)]
pub struct RdpWorkspaceRecord {
    pub workspace_id: String,
    pub app_id: String,
    pub node_id: String,
    pub device_id: String,
    pub account_name: String,
    pub credential_version: u32,
    pub secret_schema: u32,
    pub nonce_b64: String,
    pub ciphertext_b64: String,
}

// Deliberately neither Debug nor Serialize. Call sites must explicitly cross a protected credential boundary.
pub struct RdpWorkspaceCredential {
    pub record: RdpWorkspaceRecord,
    pub password: Zeroizing<String>,
}

pub struct RdpWorkspaceVault {
    key: aead::LessSafeKey,
}

fn identifier(value: &str) -> bool {
    !value.is_empty() && value.len() <= MAX_IDENTIFIER
        && value.bytes().all(|c| c.is_ascii_alphanumeric() || matches!(c, b'-' | b'_'))
}

impl RdpWorkspaceVault {
    pub fn from_key(bytes: &[u8]) -> Result<Self, String> {
        let key = aead::UnboundKey::new(&aead::AES_256_GCM, bytes)
            .map_err(|_| "RDP master key must contain exactly 32 bytes".to_string())?;
        Ok(Self { key: aead::LessSafeKey::new(key) })
    }

    /// The deployment owns this ACL-restricted file independently of Mongo backup.
    /// Never regenerate it here: a missing key must not overwrite access to existing accounts.
    pub fn load(path: &Path) -> Result<Self, String> {
        let metadata = std::fs::symlink_metadata(path)
            .map_err(|_| "RDP master key unavailable; restore the deployment key".to_string())?;
        if !metadata.is_file() || metadata.len() != 32 { return Err("RDP master key must be a regular 32-byte file".into()); }
        #[cfg(windows)] {
            use std::os::windows::fs::MetadataExt;
            if metadata.file_attributes() & 0x400 != 0 { return Err("RDP master key reparse point refused".into()); }
        }
        #[cfg(unix)] {
            use std::os::unix::fs::PermissionsExt;
            if metadata.permissions().mode() & 0o077 != 0 { return Err("RDP master key must not be accessible to group/other users".into()); }
        }
        let mut options = std::fs::OpenOptions::new();
        options.read(true);
        #[cfg(windows)] {
            use std::os::windows::fs::OpenOptionsExt;
            options.share_mode(1).custom_flags(0x00200000); // Read sharing only, open the reparse point itself.
        }
        let file = options.open(path).map_err(|_| "RDP master key open failed".to_string())?;
        if !file.metadata().is_ok_and(|metadata| metadata.is_file() && metadata.len() == 32) {
            return Err("RDP master key changed during open".into());
        }
        let mut bytes = Zeroizing::new(Vec::with_capacity(33));
        file.take(33).read_to_end(&mut bytes).map_err(|_| "RDP master key read failed".to_string())?;
        Self::from_key(&bytes)
    }

    fn aad(record: &RdpWorkspaceRecord) -> Result<Vec<u8>, String> {
        if record.secret_schema != SECRET_SCHEMA || record.credential_version == 0
            || !identifier(&record.workspace_id) || !identifier(&record.app_id)
            || !identifier(&record.node_id) || !identifier(&record.device_id)
            || !identifier(&record.account_name) || !record.account_name.starts_with("grdp_")
            || record.account_name.len() > 20 {
            return Err("RDP workspace metadata invalid".to_string());
        }
        serde_json::to_vec(&(
            "GammaRay/RdpWorkspace", record.secret_schema, &record.workspace_id,
            &record.app_id, &record.node_id, &record.device_id, &record.account_name,
            record.credential_version,
        )).map_err(|_| "RDP workspace metadata encoding failed".to_string())
    }

    pub fn create(&self, app_id: &str, node_id: &str, device_id: &str) -> Result<RdpWorkspaceRecord, String> {
        let random = SystemRandom::new();
        let mut entropy = Zeroizing::new([0u8; 32]);
        random.fill(entropy.as_mut()).map_err(|_| "RDP secure random unavailable".to_string())?;
        // Random material plus explicit Windows password-complexity categories.
        let password = Zeroizing::new(format!("aA1!{}", URL_SAFE_NO_PAD.encode(entropy.as_ref())));
        let mut nonce = [0u8; 12];
        random.fill(&mut nonce).map_err(|_| "RDP secure random unavailable".to_string())?;
        let mut record = RdpWorkspaceRecord {
            workspace_id: Uuid::new_v4().to_string(),
            app_id: app_id.to_owned(), node_id: node_id.to_owned(), device_id: device_id.to_owned(),
            account_name: format!("grdp_{}", &Uuid::new_v4().simple().to_string()[..15]),
            credential_version: 1, secret_schema: SECRET_SCHEMA,
            nonce_b64: URL_SAFE_NO_PAD.encode(nonce), ciphertext_b64: String::new(),
        };
        let aad = Self::aad(&record)?;
        let mut encrypted = Zeroizing::new(password.as_bytes().to_vec());
        self.key.seal_in_place_append_tag(aead::Nonce::assume_unique_for_key(nonce), aead::Aad::from(aad), &mut *encrypted)
            .map_err(|_| "RDP credential encryption failed".to_string())?;
        record.ciphertext_b64 = URL_SAFE_NO_PAD.encode(&*encrypted);
        Ok(record)
    }

    pub fn open(&self, record: RdpWorkspaceRecord) -> Result<RdpWorkspaceCredential, String> {
        let aad = Self::aad(&record)?;
        if record.nonce_b64.len() > 32 || record.ciphertext_b64.len() > 1024 {
            return Err("RDP encrypted credential length invalid".to_string());
        }
        let nonce: [u8; 12] = URL_SAFE_NO_PAD.decode(&record.nonce_b64)
            .map_err(|_| "RDP credential nonce invalid".to_string())?
            .try_into().map_err(|_| "RDP credential nonce invalid".to_string())?;
        let mut encrypted = Zeroizing::new(URL_SAFE_NO_PAD.decode(&record.ciphertext_b64)
            .map_err(|_| "RDP encrypted credential invalid".to_string())?);
        let plaintext = self.key.open_in_place(aead::Nonce::assume_unique_for_key(nonce), aead::Aad::from(aad), &mut encrypted)
            .map_err(|_| "RDP credential authentication failed; key or workspace mismatch".to_string())?;
        let password = Zeroizing::new(std::str::from_utf8(plaintext)
            .map_err(|_| "RDP credential encoding invalid".to_string())?.to_owned());
        if password.len() < 32 || password.len() > 256 {
            return Err("RDP credential length invalid".to_string());
        }
        Ok(RdpWorkspaceCredential { record, password })
    }

    /// Ticket issuance must never create or rotate a live workspace identity.
    pub async fn open_existing(&self, collection: &Collection<RdpWorkspaceRecord>, app_id: &str,
                               node_id: &str, device_id: &str) -> Result<RdpWorkspaceCredential, String> {
        if !identifier(app_id) || !identifier(node_id) || !identifier(device_id) {
            return Err("RDP workspace identity invalid".into());
        }
        let record = collection.find_one(doc! { "app_id": app_id, "node_id": node_id, "device_id": device_id })
            .await.map_err(|_| "RDP workspace database unavailable".to_string())?
            .ok_or_else(|| "RDP workspace has not been provisioned".to_string())?;
        self.open(record)
    }

    /// Unique (app_id, node_id) index chooses a single identity even across Console processes.
    pub async fn ensure(&self, collection: &Collection<RdpWorkspaceRecord>, app_id: &str, node_id: &str,
                        device_id: &str) -> Result<RdpWorkspaceCredential, String> {
        if !identifier(app_id) || !identifier(node_id) || !identifier(device_id) {
            return Err("RDP workspace identity invalid".to_string());
        }
        let filter = doc! { "app_id": app_id, "node_id": node_id };
        let existing = collection.find_one(filter.clone()).await
            .map_err(|_| "RDP workspace database unavailable".to_string())?;
        let record = if let Some(record) = existing { record } else {
            let candidate = self.create(app_id, node_id, device_id)?;
            // A duplicate-key race is resolved by reading the winner; no Windows account has been created yet.
            let inserted = collection.insert_one(&candidate).await.is_ok();
            if inserted { candidate } else {
                collection.find_one(filter).await
                    .map_err(|_| "RDP workspace persistence failed".to_string())?
                    .ok_or_else(|| "RDP workspace persistence failed".to_string())?
            }
        };
        if record.device_id != device_id {
            return Err("RDP workspace is pinned to another device; automatic migration is forbidden".to_string());
        }
        self.open(record)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vault() -> RdpWorkspaceVault { RdpWorkspaceVault::from_key(&[7u8; 32]).unwrap() }

    #[test]
    fn credential_roundtrip_has_no_plaintext_database_field() {
        let vault = vault();
        let record = vault.create("app-1", "node-1", "device-1").unwrap();
        let encoded = serde_json::to_string(&record).unwrap();
        let opened = vault.open(record).unwrap();
        assert_eq!(opened.record.account_name.len(), 20);
        assert!(!encoded.contains(opened.password.as_str()));
        assert!(opened.password.starts_with("aA1!"));
    }

    #[test]
    fn wrong_key_and_modified_bindings_are_rejected() {
        let vault = vault();
        let record = vault.create("app-1", "node-1", "device-1").unwrap();
        assert!(RdpWorkspaceVault::from_key(&[8u8; 32]).unwrap().open(record.clone()).is_err());
        for index in 0..6 {
            let mut changed = record.clone();
            match index {
                0 => changed.app_id = "app-2".into(),
                1 => changed.node_id = "node-2".into(),
                2 => changed.device_id = "device-2".into(),
                3 => changed.account_name = "grdp_another".into(),
                4 => changed.credential_version += 1,
                _ => changed.workspace_id = Uuid::new_v4().to_string(),
            }
            assert!(vault.open(changed).is_err());
        }
    }

    #[test]
    fn generated_accounts_passwords_and_nonces_are_distinct() {
        let vault = vault();
        let first = vault.create("app-1", "node-1", "device-1").unwrap();
        let second = vault.create("app-1", "node-2", "device-1").unwrap();
        assert_ne!(first.account_name, second.account_name);
        assert_ne!(first.nonce_b64, second.nonce_b64);
        assert_ne!(*vault.open(first).unwrap().password, *vault.open(second).unwrap().password);
    }

    #[test]
    fn invalid_identifiers_and_envelopes_fail_closed() {
        let vault = vault();
        assert!(vault.create("", "node-1", "device-1").is_err());
        assert!(vault.create("app-1", "../node", "device-1").is_err());
        assert!(RdpWorkspaceVault::from_key(&[0u8; 31]).is_err());
        let mut record = vault.create("app-1", "node-1", "device-1").unwrap();
        record.ciphertext_b64 = "x".repeat(1025);
        assert!(vault.open(record).is_err());
    }

    #[test]
    fn deployment_key_load_is_bounded_and_never_regenerates_missing_or_invalid_files() {
        use std::io::Write;
        struct TestKey(std::path::PathBuf);
        impl Drop for TestKey {
            fn drop(&mut self) { let _ = std::fs::remove_file(&self.0); }
        }
        for size in [0usize, 31, 32, 33, 65536] {
            let key = TestKey(std::env::temp_dir().join(format!("gammaray-vault-test-{}", Uuid::new_v4())));
            assert!(RdpWorkspaceVault::load(&key.0).is_err());
            assert!(!key.0.exists());
            let mut options = std::fs::OpenOptions::new();
            options.write(true).create_new(true);
            #[cfg(unix)] {
                use std::os::unix::fs::OpenOptionsExt;
                options.mode(0o600);
            }
            let mut file = options.open(&key.0).unwrap();
            file.write_all(&vec![7u8; size]).unwrap();
            file.sync_all().unwrap();
            drop(file);
            assert_eq!(RdpWorkspaceVault::load(&key.0).is_ok(), size == 32);
            assert_eq!(std::fs::read(&key.0).unwrap(), vec![7u8; size]);
        }
        assert!(RdpWorkspaceVault::load(&std::env::temp_dir()).is_err());
    }
}
