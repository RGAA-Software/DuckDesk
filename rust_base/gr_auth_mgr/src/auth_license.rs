use base64::{engine::general_purpose, Engine as _};
use ring::rand::SystemRandom;
use ring::signature::{self, Ed25519KeyPair, KeyPair, UnparsedPublicKey, ED25519};
use serde::{Deserialize, Serialize};

/// License metadata that is cryptographically signed by the auth server.
/// This is the only information needed by the CMS to enforce authorization.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct AuthLicense {
    pub auth_id: String,
    pub auth_name: String,
    pub machine_code: String,
    pub max_streams: i32,
    pub days: i32,
    pub role: i32,
    pub created_at_ms: i64,
    pub expires_at_ms: i64,
    pub appkey: String,
}

impl AuthLicense {
    /// Returns the canonical byte representation that is signed/verified.
    /// The JSON is compact and fields are emitted in declaration order, which
    /// makes the signature deterministic as long as this struct definition is stable.
    pub fn canonical_bytes(&self) -> Result<Vec<u8>, String> {
        serde_json::to_vec(self).map_err(|e| format!("failed to serialize license: {}", e))
    }
}

/// A license together with its Ed25519 signature.
#[derive(Debug, Clone)]
pub struct SignedLicense {
    pub license: AuthLicense,
    pub signature: Vec<u8>,
}

/// Deploy string format: base64(license_json) + "." + base64(signature).
const DEPLOY_STRING_PARTS: usize = 2;

impl SignedLicense {
    /// Encodes the signed license as a deploy string.
    pub fn to_deploy_string(&self) -> Result<String, String> {
        let license_json = serde_json::to_string(&self.license)
            .map_err(|e| format!("failed to serialize license: {}", e))?;
        let license_b64 = general_purpose::STANDARD.encode(license_json.as_bytes());
        let sig_b64 = general_purpose::STANDARD.encode(&self.signature);
        Ok(format!("{}.{}", license_b64, sig_b64))
    }

    /// Decodes a deploy string back into a signed license.
    pub fn parse_deploy_string(s: &str) -> Result<Self, String> {
        let parts: Vec<&str> = s.split('.').collect();
        if parts.len() != DEPLOY_STRING_PARTS {
            return Err(format!(
                "invalid signed license format: expected {} dot-separated parts, got {}",
                DEPLOY_STRING_PARTS,
                parts.len()
            ));
        }
        let license_bytes = general_purpose::STANDARD
            .decode(parts[0])
            .map_err(|e| format!("failed to decode license: {}", e))?;
        let license_json = String::from_utf8(license_bytes)
            .map_err(|e| format!("license is not valid utf-8: {}", e))?;
        let license: AuthLicense = serde_json::from_str(&license_json)
            .map_err(|e| format!("failed to parse license json: {}", e))?;
        let signature = general_purpose::STANDARD
            .decode(parts[1])
            .map_err(|e| format!("failed to decode signature: {}", e))?;
        Ok(SignedLicense { license, signature })
    }
}

/// Signs `AuthLicense` values using an Ed25519 private key.
pub struct LicenseSigner {
    key_pair: Ed25519KeyPair,
}

impl LicenseSigner {
    /// Loads a signer from raw PKCS#8 v1 private key bytes.
    pub fn from_pkcs8_bytes(bytes: &[u8]) -> Result<Self, String> {
        let key_pair = Ed25519KeyPair::from_pkcs8_maybe_unchecked(bytes)
            .map_err(|e| format!("invalid Ed25519 private key: {}", e))?;
        Ok(Self { key_pair })
    }

    /// Generates a new Ed25519 key pair. Returns (pkcs8_private_key, public_key).
    pub fn generate_keypair() -> Result<(Vec<u8>, Vec<u8>), String> {
        let rng = SystemRandom::new();
        let pkcs8_bytes = signature::Ed25519KeyPair::generate_pkcs8(&rng)
            .map_err(|e| format!("failed to generate key pair: {}", e))?
            .as_ref()
            .to_vec();
        let key_pair = Ed25519KeyPair::from_pkcs8(&pkcs8_bytes)
            .map_err(|e| format!("failed to parse generated key pair: {}", e))?;
        let public_key = key_pair.public_key().as_ref().to_vec();
        Ok((pkcs8_bytes, public_key))
    }

    /// Returns the public key bytes corresponding to this signer.
    pub fn public_key_bytes(&self) -> Vec<u8> {
        self.key_pair.public_key().as_ref().to_vec()
    }

    /// Signs a license and returns the signed representation.
    pub fn sign(&self, license: &AuthLicense) -> Result<SignedLicense, String> {
        let message = license.canonical_bytes()?;
        let signature = self.key_pair.sign(&message).as_ref().to_vec();
        Ok(SignedLicense {
            license: license.clone(),
            signature,
        })
    }
}

/// Verifies `SignedLicense` values using an Ed25519 public key.
pub struct LicenseVerifier {
    public_key: Vec<u8>,
}

impl LicenseVerifier {
    /// Loads a verifier from raw 32-byte Ed25519 public key bytes.
    pub fn from_public_key_bytes(bytes: &[u8]) -> Result<Self, String> {
        if bytes.len() != 32 {
            return Err(format!(
                "invalid Ed25519 public key length: expected 32, got {}",
                bytes.len()
            ));
        }
        Ok(Self {
            public_key: bytes.to_vec(),
        })
    }

    /// Verifies the cryptographic signature only.
    pub fn verify_signature(&self, signed: &SignedLicense) -> Result<bool, String> {
        let message = signed.license.canonical_bytes()?;
        let public_key = UnparsedPublicKey::new(&ED25519, &self.public_key);
        match public_key.verify(&message, &signed.signature) {
            Ok(()) => Ok(true),
            Err(_) => Ok(false),
        }
    }

    /// Verifies signature, machine code binding, and expiration.
    pub fn verify(
        &self,
        signed: &SignedLicense,
        machine_code: &str,
        now_ms: i64,
    ) -> Result<bool, String> {
        if !self.verify_signature(signed)? {
            return Ok(false);
        }
        if signed.license.machine_code != machine_code {
            return Ok(false);
        }
        if signed.license.expires_at_ms <= now_ms {
            return Ok(false);
        }
        Ok(true)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_license() -> AuthLicense {
        AuthLicense {
            auth_id: "auth-123".to_string(),
            auth_name: "customer-a".to_string(),
            machine_code: "mc-abc".to_string(),
            max_streams: 4,
            days: 30,
            role: 1,
            created_at_ms: 1000,
            expires_at_ms: 2_592_001_000, // 30 days + 1s
            appkey: "appkey-xyz".to_string(),
        }
    }

    #[test]
    fn sign_and_verify_roundtrip() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        let license = sample_license();
        let signed = signer.sign(&license).unwrap();
        assert!(verifier.verify_signature(&signed).unwrap());
    }

    #[test]
    fn tampered_license_fails_verification() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        let mut signed = signer.sign(&sample_license()).unwrap();
        signed.license.max_streams = 9999;
        assert!(!verifier.verify_signature(&signed).unwrap());
    }

    #[test]
    fn wrong_public_key_fails_verification() {
        let (priv_key, _) = LicenseSigner::generate_keypair().unwrap();
        let (_, other_pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&other_pub_key).unwrap();
        let signed = signer.sign(&sample_license()).unwrap();
        assert!(!verifier.verify_signature(&signed).unwrap());
    }

    #[test]
    fn machine_code_mismatch_fails() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        let signed = signer.sign(&sample_license()).unwrap();
        assert!(!verifier
            .verify(&signed, "wrong-machine", signed.license.expires_at_ms - 1)
            .unwrap());
    }

    #[test]
    fn expired_license_fails() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        let signed = signer.sign(&sample_license()).unwrap();
        assert!(!verifier
            .verify(
                &signed,
                &signed.license.machine_code,
                signed.license.expires_at_ms + 1
            )
            .unwrap());
    }

    #[test]
    fn valid_license_passes_full_verification() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        let signed = signer.sign(&sample_license()).unwrap();
        assert!(verifier
            .verify(
                &signed,
                &signed.license.machine_code,
                signed.license.expires_at_ms - 1
            )
            .unwrap());
    }

    #[test]
    fn deploy_string_roundtrip() {
        let (priv_key, pub_key) = LicenseSigner::generate_keypair().unwrap();
        let signer = LicenseSigner::from_pkcs8_bytes(&priv_key).unwrap();
        let verifier = LicenseVerifier::from_public_key_bytes(&pub_key).unwrap();
        let signed = signer.sign(&sample_license()).unwrap();
        let deploy = signed.to_deploy_string().unwrap();
        let parsed = SignedLicense::parse_deploy_string(&deploy).unwrap();
        assert_eq!(parsed.license, signed.license);
        assert_eq!(parsed.signature, signed.signature);
        assert!(verifier.verify_signature(&parsed).unwrap());
    }

    #[test]
    fn malformed_deploy_string_rejected() {
        assert!(SignedLicense::parse_deploy_string("not-a-valid-string").is_err());
        assert!(SignedLicense::parse_deploy_string("only-one-part").is_err());
        assert!(SignedLicense::parse_deploy_string("a.b.c").is_err());
        assert!(SignedLicense::parse_deploy_string("bm90LWpzb24.bm90LXNpZw==").is_err());
    }
}
