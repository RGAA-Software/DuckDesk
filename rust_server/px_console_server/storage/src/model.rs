use chrono::{DateTime, Utc};
use px_pg::DatabaseError;
use sha2::{Digest, Sha256};
use std::fmt;
use uuid::Uuid;
use zeroize::Zeroizing;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum StoreError {
    #[error("invalid input")]
    InvalidInput,
    #[error("access or revision rejected")]
    Rejected,
    #[error("requested object not found")]
    NotFound,
    #[error("no ready capacity")]
    NoCapacity,
    #[error("license entitlement rejected the operation")]
    LicenseRestriction,
    #[error("protected workspace requires recovery")]
    RecoveryRequired,
    #[error("cache root requires explicit recovery")]
    CacheRecoveryRequired,
    #[error(transparent)]
    File(#[from] px_private_files::FileError),
    #[error(transparent)]
    Database(#[from] DatabaseError),
}

/// Deployment-wide product limits supplied by the already verified Console license.
/// Storage applies these values inside the same transaction as each admission so the
/// last quota slot cannot be over-issued by concurrent HTTP requests.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeEntitlement {
    pub max_streams: u32,
    pub cloud_applications: bool,
    pub desktop: bool,
    pub rdp: bool,
}

impl RuntimeEntitlement {
    pub fn new(
        max_streams: u32,
        cloud_applications: bool,
        desktop: bool,
        rdp: bool,
    ) -> Result<Self, StoreError> {
        if max_streams == 0 {
            return Err(StoreError::InvalidInput);
        }
        Ok(Self {
            max_streams,
            cloud_applications,
            desktop,
            rdp,
        })
    }

    #[cfg(feature = "pg-integration")]
    pub(crate) fn unrestricted_for_integration() -> Self {
        Self {
            max_streams: u32::MAX,
            cloud_applications: true,
            desktop: true,
            rdp: true,
        }
    }

    pub(crate) fn permits_application_kind(self, kind: &str) -> bool {
        match kind {
            "game_hook" | "webview" => self.cloud_applications,
            "rdp" => self.rdp,
            _ => false,
        }
    }
}

impl From<sqlx::Error> for StoreError {
    fn from(error: sqlx::Error) -> Self {
        Self::Database(error.into())
    }
}

#[derive(Clone)]
pub struct Username {
    pub(crate) display: String,
    pub(crate) normalized: String,
}

impl Username {
    /// Unicode lowercase only: no silent trimming, normalization or homoglyph folding.
    pub fn parse(value: &str) -> Result<Self, StoreError> {
        let normalized =
            px_credentials::normalize_username(value).ok_or(StoreError::InvalidInput)?;
        Ok(Self {
            display: value.into(),
            normalized,
        })
    }

    pub fn normalized(&self) -> &str {
        &self.normalized
    }
}

/// Encoded Argon2id hash; neither plaintext nor reversibly encrypted passwords are stored.
pub struct PasswordDigest(Zeroizing<String>);

impl PasswordDigest {
    pub fn parse(value: String) -> Result<Self, StoreError> {
        let value = Zeroizing::new(value);
        if !px_credentials::valid_hash(&value) {
            return Err(StoreError::InvalidInput);
        }
        Ok(Self(value))
    }

    pub fn encoded(&self) -> &str {
        &self.0
    }
}

impl fmt::Debug for PasswordDigest {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("PasswordDigest(<redacted>)")
    }
}

#[derive(Clone)]
pub struct TokenDigest(pub(crate) [u8; 32]);

impl TokenDigest {
    /// Caller hashes a CSPRNG-issued bearer token with SHA-256 before entering storage.
    pub fn from_sha256(bytes: [u8; 32]) -> Self {
        Self(bytes)
    }
}

impl fmt::Debug for TokenDigest {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("TokenDigest(<redacted>)")
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ClientType {
    Panel,
    Android,
    UserWeb,
    AdminWeb,
}

impl ClientType {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Panel => "panel",
            Self::Android => "android",
            Self::UserWeb => "user_web",
            Self::AdminWeb => "admin_web",
        }
    }
}

#[derive(Debug, Clone, serde::Serialize, sqlx::FromRow)]
pub struct UserProfile {
    pub id: Uuid,
    pub username: String,
    pub authorization_revision: i64,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
}

pub const MAX_AVATAR_BYTES: usize = 2 * 1024 * 1024;

#[derive(Debug, Clone)]
pub struct AvatarContent {
    pub(crate) media_type: &'static str,
    pub(crate) image_bytes: Vec<u8>,
    pub(crate) sha256: [u8; 32],
}

impl AvatarContent {
    pub fn new(media_type: &str, image_bytes: Vec<u8>) -> Result<Self, StoreError> {
        if image_bytes.is_empty()
            || image_bytes.len() > MAX_AVATAR_BYTES
            || !valid_avatar(media_type, &image_bytes)
        {
            return Err(StoreError::InvalidInput);
        }
        let media_type = match media_type {
            "image/png" => "image/png",
            "image/jpeg" => "image/jpeg",
            "image/webp" => "image/webp",
            _ => return Err(StoreError::InvalidInput),
        };
        let sha256 = Sha256::digest(&image_bytes).into();
        Ok(Self {
            media_type,
            image_bytes,
            sha256,
        })
    }
}

fn valid_avatar(media_type: &str, image_bytes: &[u8]) -> bool {
    match media_type {
        "image/png" => {
            image_bytes.len() >= 20
                && image_bytes.starts_with(b"\x89PNG\r\n\x1a\n")
                && image_bytes[12..16] == *b"IHDR"
                && image_bytes.ends_with(b"IEND\xaeB\x60\x82")
        }
        "image/jpeg" => {
            image_bytes.starts_with(&[0xff, 0xd8, 0xff]) && image_bytes.ends_with(&[0xff, 0xd9])
        }
        "image/webp" if image_bytes.len() >= 12 => {
            let declared_size =
                u32::from_le_bytes(image_bytes[4..8].try_into().unwrap_or_default());
            image_bytes.starts_with(b"RIFF")
                && &image_bytes[8..12] == b"WEBP"
                && declared_size.checked_add(8) == u32::try_from(image_bytes.len()).ok()
        }
        _ => false,
    }
}

#[derive(Debug, Clone, sqlx::FromRow)]
pub struct UserAvatar {
    pub media_type: String,
    pub image_bytes: Vec<u8>,
    pub sha256: Vec<u8>,
    pub revision: i64,
}

#[derive(Debug)]
pub struct Credential {
    pub user: UserProfile,
    pub role: crate::Role,
    pub password: PasswordDigest,
}

#[derive(sqlx::FromRow)]
pub(crate) struct CredentialRow {
    pub id: Uuid,
    pub username: String,
    pub authorization_revision: i64,
    pub revision: i64,
    pub created_at: DateTime<Utc>,
    pub password_hash: String,
    pub role: String,
}
impl CredentialRow {
    pub(crate) fn credential(self) -> Result<Credential, StoreError> {
        let password = PasswordDigest::parse(self.password_hash)
            .map_err(|_| StoreError::Database(DatabaseError::Operation))?;
        let role = match self.role.as_str() {
            "admin" => crate::Role::Admin,
            "viewer" => crate::Role::Viewer,
            "user" => crate::Role::User,
            _ => return Err(StoreError::Database(DatabaseError::Operation)),
        };
        Ok(Credential {
            user: UserProfile {
                id: self.id,
                username: self.username,
                authorization_revision: self.authorization_revision,
                revision: self.revision,
                created_at: self.created_at,
            },
            role,
            password,
        })
    }
}

#[derive(Debug, Clone, serde::Serialize, sqlx::FromRow)]
pub struct AuthenticatedSession {
    pub session_id: Uuid,
    pub user_id: Uuid,
    pub client_type: String,
    pub authorization_revision: i64,
    pub expires_at: DateTime<Utc>,
    pub absolute_expires_at: DateTime<Utc>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn username_rules_are_explicit_and_do_not_guess_identifiers() {
        for input in [
            "",
            "x",
            " name",
            "name ",
            "n\n",
            "a/b",
            "a\\b",
            &"a".repeat(65),
        ] {
            assert!(Username::parse(input).is_err());
        }
        assert_eq!(
            Username::parse("Pixels用户").unwrap().normalized(),
            "pixels用户"
        );
        assert_ne!(
            Username::parse("éa").unwrap().normalized(),
            Username::parse("e\u{301}a").unwrap().normalized()
        );
        assert_eq!(
            Username::parse(&"中".repeat(64))
                .unwrap()
                .normalized()
                .chars()
                .count(),
            64
        );
    }

    #[test]
    fn plaintext_and_unsupported_password_formats_are_rejected() {
        for input in [
            "plaintext",
            "$argon2i$v=19$m=19456,t=2,p=1$c2FsdHNhbHQ$YWJjZA",
            "$argon2id$v=19$m=1,t=1,p=1$c2FsdHNhbHQ$YWJjZA",
        ] {
            assert!(PasswordDigest::parse(input.into()).is_err());
        }
        assert!(!format!("{:?}", TokenDigest::from_sha256([42; 32])).contains("42"));
    }
}
