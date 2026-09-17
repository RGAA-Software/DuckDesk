use chrono::{DateTime, Utc};
use px_pg::DatabaseError;
use std::fmt;
use uuid::Uuid;
use zeroize::Zeroizing;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum StoreError {
    #[error("invalid input")]
    InvalidInput,
    #[error("access or revision rejected")]
    Rejected,
    #[error("no ready capacity")]
    NoCapacity,
    #[error("protected workspace requires recovery")]
    RecoveryRequired,
    #[error("cache root requires explicit recovery")]
    CacheRecoveryRequired,
    #[error(transparent)]
    File(#[from] px_private_files::FileError),
    #[error(transparent)]
    Database(#[from] DatabaseError),
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
