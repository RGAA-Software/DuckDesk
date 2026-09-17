//! No file loading, key generation fallback, Debug or Serialize for private material.
//! The composition root must supply independently protected deployment keys.
use crate::StoreError;
use ring::{
    aead,
    rand::{SecureRandom, SystemRandom},
};
use std::collections::BTreeMap;
use uuid::Uuid;
use zeroize::Zeroizing;

pub struct WorkspaceKey {
    pub id: Uuid,
    pub bytes: Zeroizing<[u8; 32]>,
}
pub struct WorkspaceVault {
    active: Uuid,
    keys: BTreeMap<Uuid, aead::LessSafeKey>,
}
#[derive(Clone)]
pub(crate) struct SecretBinding {
    pub deployment: Uuid,
    pub workspace: Uuid,
    pub application: Uuid,
    pub placement: Uuid,
    pub node: Uuid,
    pub account: String,
    pub credential_revision: i64,
}
pub(crate) struct SealedCredential {
    pub key_id: Uuid,
    pub nonce: Vec<u8>,
    pub ciphertext: Vec<u8>,
}
impl SecretBinding {
    fn aad(&self, key_id: Uuid) -> Result<Vec<u8>, StoreError> {
        if [
            self.deployment,
            self.workspace,
            self.application,
            self.placement,
            self.node,
            key_id,
        ]
        .iter()
        .any(Uuid::is_nil)
            || self.credential_revision <= 0
            || self.account.len() != 20
            || !self.account.starts_with("pxrdp_")
            || !self.account[6..]
                .bytes()
                .all(|value| value.is_ascii_digit() || (b'a'..=b'f').contains(&value))
        {
            return Err(StoreError::RecoveryRequired);
        }
        let mut aad = b"Pixels-PG-RDP-Workspace-v1\0".to_vec();
        for id in [
            self.deployment,
            self.workspace,
            self.application,
            self.placement,
            self.node,
            key_id,
        ] {
            aad.extend_from_slice(id.as_bytes());
        }
        aad.extend_from_slice(&self.credential_revision.to_be_bytes());
        aad.extend_from_slice(self.account.as_bytes());
        Ok(aad)
    }
}
impl WorkspaceVault {
    pub fn new(active: Uuid, keys: Vec<WorkspaceKey>) -> Result<Self, StoreError> {
        if active.is_nil() || keys.is_empty() || keys.len() > 32 {
            return Err(StoreError::InvalidInput);
        }
        let mut result = BTreeMap::new();
        for key in keys {
            if key.id.is_nil() || result.contains_key(&key.id) {
                return Err(StoreError::InvalidInput);
            }
            let cipher = aead::UnboundKey::new(&aead::AES_256_GCM, key.bytes.as_ref())
                .map_err(|_| StoreError::RecoveryRequired)?;
            result.insert(key.id, aead::LessSafeKey::new(cipher));
        }
        if !result.contains_key(&active) {
            return Err(StoreError::RecoveryRequired);
        }
        Ok(Self {
            active,
            keys: result,
        })
    }
    pub(crate) fn create(&self, binding: &SecretBinding) -> Result<SealedCredential, StoreError> {
        let mut random = Zeroizing::new([0u8; 32]);
        SystemRandom::new()
            .fill(random.as_mut())
            .map_err(|_| StoreError::RecoveryRequired)?;
        let mut password = Zeroizing::new(String::with_capacity(68));
        password.push_str("aA1!");
        const HEX: &[u8; 16] = b"0123456789abcdef";
        for value in random.iter() {
            password.push(HEX[usize::from(value >> 4)] as char);
            password.push(HEX[usize::from(value & 15)] as char);
        }
        self.seal(binding, &password)
    }
    pub(crate) fn seal(
        &self,
        binding: &SecretBinding,
        password: &str,
    ) -> Result<SealedCredential, StoreError> {
        if password.len() != 68
            || !password.starts_with("aA1!")
            || !password[4..].bytes().all(|value| value.is_ascii_hexdigit())
        {
            return Err(StoreError::RecoveryRequired);
        }
        let aad = binding.aad(self.active)?;
        let mut nonce = [0u8; 12];
        SystemRandom::new()
            .fill(&mut nonce)
            .map_err(|_| StoreError::RecoveryRequired)?;
        let mut ciphertext = Zeroizing::new(password.as_bytes().to_vec());
        self.keys
            .get(&self.active)
            .ok_or(StoreError::RecoveryRequired)?
            .seal_in_place_append_tag(
                aead::Nonce::assume_unique_for_key(nonce),
                aead::Aad::from(aad),
                &mut *ciphertext,
            )
            .map_err(|_| StoreError::RecoveryRequired)?;
        Ok(SealedCredential {
            key_id: self.active,
            nonce: nonce.to_vec(),
            ciphertext: ciphertext.to_vec(),
        })
    }
    pub(crate) fn open(
        &self,
        binding: &SecretBinding,
        secret: &SealedCredential,
    ) -> Result<Zeroizing<String>, StoreError> {
        let aad = binding.aad(secret.key_id)?;
        if secret.ciphertext.len() != 84 {
            return Err(StoreError::RecoveryRequired);
        }
        let nonce: [u8; 12] = secret
            .nonce
            .as_slice()
            .try_into()
            .map_err(|_| StoreError::RecoveryRequired)?;
        let mut bytes = Zeroizing::new(secret.ciphertext.clone());
        let clear = self
            .keys
            .get(&secret.key_id)
            .ok_or(StoreError::RecoveryRequired)?
            .open_in_place(
                aead::Nonce::assume_unique_for_key(nonce),
                aead::Aad::from(aad),
                &mut bytes,
            )
            .map_err(|_| StoreError::RecoveryRequired)?;
        let password = Zeroizing::new(
            std::str::from_utf8(clear)
                .map_err(|_| StoreError::RecoveryRequired)?
                .to_owned(),
        );
        if password.len() != 68
            || !password.starts_with("aA1!")
            || !password[4..].bytes().all(|value| value.is_ascii_hexdigit())
        {
            return Err(StoreError::RecoveryRequired);
        }
        Ok(password)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn independent_node_openssl_vector_pins_the_aad_byte_contract() {
        // Generated independently with Node v22.15.0 / OpenSSL 3.0.16,
        // createCipheriv("aes-256-gcm"). All values are public synthetic test material.
        let id = |suffix: u8| {
            Uuid::parse_str(&format!("00000000-0000-4000-8000-0000000000{suffix:02x}")).unwrap()
        };
        let decode = |value: &str| {
            value
                .as_bytes()
                .chunks_exact(2)
                .map(|part| u8::from_str_radix(std::str::from_utf8(part).unwrap(), 16).unwrap())
                .collect::<Vec<u8>>()
        };
        let context = SecretBinding {
            deployment: id(1),
            workspace: id(2),
            application: id(3),
            placement: id(4),
            node: id(5),
            account: "pxrdp_0123456789abcd".into(),
            credential_revision: 1,
        };
        assert_eq!(context.aad(id(6)).unwrap(),decode("506978656c732d50472d5244502d576f726b73706163652d763100000000000000400080000000000000010000000000004000800000000000000200000000000040008000000000000003000000000000400080000000000000040000000000004000800000000000000500000000000040008000000000000006000000000000000170787264705f3031323334353637383961626364"));
        let secret = SealedCredential { key_id:id(6), nonce:decode("000102030405060708090a0b"),
            ciphertext:decode("4bc29fbdc7c160e098f44aada64bfd069d93181bb1918608d545cccdfe6658c54fa9df39070f87724766a2a83f40394270f2415572979e8f9d812a2274a571860ccd191871158e1d5db98c07ba53de6a39fae2f2") };
        let vault = WorkspaceVault::new(id(6), vec![key(id(6), 61)]).unwrap();
        assert_eq!(
            *vault.open(&context, &secret).unwrap(),
            format!("aA1!{}", "12".repeat(32))
        );
    }
    fn key(id: Uuid, byte: u8) -> WorkspaceKey {
        WorkspaceKey {
            id,
            bytes: Zeroizing::new([byte; 32]),
        }
    }
    fn binding() -> SecretBinding {
        SecretBinding {
            deployment: Uuid::new_v4(),
            workspace: Uuid::new_v4(),
            application: Uuid::new_v4(),
            placement: Uuid::new_v4(),
            node: Uuid::new_v4(),
            account: "pxrdp_0123456789abcd".into(),
            credential_revision: 1,
        }
    }
    #[test]
    fn authenticated_binding_covers_every_identity_and_rejects_wrong_keys() {
        let id = Uuid::new_v4();
        let vault = WorkspaceVault::new(id, vec![key(id, 41)]).unwrap();
        let context = binding();
        let secret = vault.create(&context).unwrap();
        assert_eq!(vault.open(&context, &secret).unwrap().len(), 68);
        for field in 0..7 {
            let mut changed = context.clone();
            match field {
                0 => changed.deployment = Uuid::new_v4(),
                1 => changed.workspace = Uuid::new_v4(),
                2 => changed.application = Uuid::new_v4(),
                3 => changed.placement = Uuid::new_v4(),
                4 => changed.node = Uuid::new_v4(),
                5 => changed.account = "pxrdp_f123456789abcd".into(),
                _ => changed.credential_revision += 1,
            }
            assert!(vault.open(&changed, &secret).is_err());
        }
        assert!(WorkspaceVault::new(id, vec![key(id, 42)])
            .unwrap()
            .open(&context, &secret)
            .is_err());
        let other = Uuid::new_v4();
        assert!(WorkspaceVault::new(other, vec![key(other, 41)])
            .unwrap()
            .open(&context, &secret)
            .is_err());
    }
    #[test]
    fn key_rotation_preserves_password_and_uses_new_random_nonces() {
        let first = Uuid::new_v4();
        let next = Uuid::new_v4();
        let context = binding();
        let old = WorkspaceVault::new(first, vec![key(first, 41)]).unwrap();
        let original = old.create(&context).unwrap();
        let password = old.open(&context, &original).unwrap();
        let rotation = WorkspaceVault::new(next, vec![key(first, 41), key(next, 42)]).unwrap();
        let rotated = rotation
            .seal(&context, &rotation.open(&context, &original).unwrap())
            .unwrap();
        assert_eq!(rotated.key_id, next);
        assert_ne!(rotated.nonce, original.nonce);
        assert_ne!(rotated.ciphertext, original.ciphertext);
        let new = WorkspaceVault::new(next, vec![key(next, 42)]).unwrap();
        assert_eq!(*new.open(&context, &rotated).unwrap(), *password);
        assert!(new.open(&context, &original).is_err());
        assert_ne!(
            *old.open(&context, &old.create(&context).unwrap()).unwrap(),
            *password
        );
    }
    #[test]
    fn malformed_material_and_ciphertext_fail_without_plaintext_errors() {
        let id = Uuid::new_v4();
        let context = binding();
        assert!(WorkspaceVault::new(id, Vec::new()).is_err());
        assert!(WorkspaceVault::new(id, vec![key(id, 1), key(id, 2)]).is_err());
        assert!(WorkspaceVault::new(Uuid::nil(), vec![key(id, 1)]).is_err());
        let vault = WorkspaceVault::new(id, vec![key(id, 41)]).unwrap();
        let mut secret = vault.create(&context).unwrap();
        secret.ciphertext[0] ^= 1;
        assert_eq!(
            vault.open(&context, &secret).unwrap_err().to_string(),
            "protected workspace requires recovery"
        );
        secret.ciphertext.pop();
        assert!(vault.open(&context, &secret).is_err());
        secret.nonce.clear();
        assert!(vault.open(&context, &secret).is_err());
        let mut bad = context.clone();
        bad.account = "pxrdp_错误错误错误".into();
        assert!(vault.create(&bad).is_err());
    }
}
