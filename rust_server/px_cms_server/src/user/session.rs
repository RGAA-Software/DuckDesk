use crate::cms_api_error::CmsApiError;
use crate::{gCmsDatabase, gUserManager};
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use mongodb::bson::{doc, DateTime};
use px_base::hash_util::{compute_hash, HashAlgo};
use ring::rand::{SecureRandom, SystemRandom};
use serde::{Deserialize, Serialize};
use std::sync::Arc;

const PANEL_SLIDING_MS: i64 = 30 * 24 * 60 * 60 * 1000;
const PANEL_ABSOLUTE_MS: i64 = 90 * 24 * 60 * 60 * 1000;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CmsUserSession {
    pub sid: String,
    pub token_hash: String,
    pub subject_id: String,
    pub auth_version: i64,
    pub client_type: String,
    pub created_at: i64,
    pub last_used_at: i64,
    pub expires_at: i64,
    pub absolute_expires_at: i64,
    pub cleanup_at: DateTime,
    pub revoked_at: Option<i64>,
}

#[derive(Debug, Clone)]
pub struct IssuedUserSession {
    pub access_token: String,
    pub session: CmsUserSession,
}

#[derive(Debug, Clone)]
pub struct AuthenticatedUser {
    pub sid: String,
    pub uid: String,
}

pub struct CmsUserSessionManager;

impl CmsUserSessionManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self)
    }

    fn hash_token(token: &str) -> String {
        compute_hash(HashAlgo::SHA256, token.as_bytes())
    }

    fn new_token() -> Result<String, CmsApiError> {
        let mut bytes = [0_u8; 32];
        SystemRandom::new().fill(&mut bytes).map_err(|_| {
            tracing::error!("system CSPRNG failed while issuing user session");
            CmsApiError::InternalError
        })?;
        Ok(URL_SAFE_NO_PAD.encode(bytes))
    }

    pub async fn issue_panel(
        &self,
        uid: String,
        auth_version: i64,
    ) -> Result<IssuedUserSession, CmsApiError> {
        let now = px_base::get_current_timestamp();
        let access_token = Self::new_token()?;
        let expires_at = now + PANEL_SLIDING_MS;
        let absolute_expires_at = now + PANEL_ABSOLUTE_MS;
        let session = CmsUserSession {
            sid: uuid::Uuid::new_v4().simple().to_string(),
            token_hash: Self::hash_token(&access_token),
            subject_id: uid,
            auth_version,
            client_type: "panel".to_string(),
            created_at: now,
            last_used_at: now,
            expires_at,
            absolute_expires_at,
            cleanup_at: DateTime::from_millis(expires_at.min(absolute_expires_at)),
            revoked_at: None,
        };
        gCmsDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .insert_one(session.clone())
            .await
            .map_err(|e| {
                tracing::error!("insert user session failed: {}", e);
                CmsApiError::DatabaseError
            })?;
        Ok(IssuedUserSession {
            access_token,
            session,
        })
    }

    pub async fn authenticate(&self, token: &str) -> Result<AuthenticatedUser, CmsApiError> {
        if token.len() < 32 {
            return Err(CmsApiError::AuthenticationRequired);
        }
        let token_hash = Self::hash_token(token);
        let now = px_base::get_current_timestamp();
        let collection = gCmsDatabase.lock().await.user_session();
        let session = collection
            .lock()
            .await
            .find_one(doc! {
                "token_hash": token_hash,
                "revoked_at": BSON_NULL,
                "expires_at": { "$gt": now },
                "absolute_expires_at": { "$gt": now },
            })
            .await
            .map_err(|e| {
                tracing::error!("query user session failed: {}", e);
                CmsApiError::DatabaseError
            })?
            .ok_or(CmsApiError::AuthenticationRequired)?;

        let user = gUserManager
            .query_user_by_id(session.subject_id.clone())
            .await
            .map_err(|_| CmsApiError::AuthenticationRequired)?;
        if user.deleted || user.auth_version != session.auth_version {
            return Err(CmsApiError::AuthenticationRequired);
        }

        let extended_expires_at = (now + PANEL_SLIDING_MS).min(session.absolute_expires_at);
        collection
            .lock()
            .await
            .update_one(
                doc! { "sid": &session.sid, "revoked_at": BSON_NULL },
                doc! { "$set": {
                    "last_used_at": now,
                    "expires_at": extended_expires_at,
                    "cleanup_at": DateTime::from_millis(extended_expires_at),
                }},
            )
            .await
            .map_err(|e| {
                tracing::error!("refresh user session failed: {}", e);
                CmsApiError::DatabaseError
            })?;

        Ok(AuthenticatedUser {
            sid: session.sid,
            uid: session.subject_id,
        })
    }

    pub async fn revoke(&self, sid: &str, uid: &str) -> Result<(), CmsApiError> {
        gCmsDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .update_one(
                doc! { "sid": sid, "subject_id": uid, "revoked_at": BSON_NULL },
                doc! { "$set": { "revoked_at": px_base::get_current_timestamp() } },
            )
            .await
            .map_err(|e| {
                tracing::error!("revoke user session failed: {}", e);
                CmsApiError::DatabaseError
            })?;
        Ok(())
    }

    /// Revoke the presented bearer token without requiring an otherwise-valid
    /// session. This keeps logout idempotent for expired or already-revoked
    /// sessions while still affecting only the token the caller possesses.
    pub async fn revoke_token(&self, token: &str) -> Result<(), CmsApiError> {
        if token.is_empty() {
            return Ok(());
        }
        gCmsDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .update_one(
                doc! { "token_hash": Self::hash_token(token), "revoked_at": BSON_NULL },
                doc! { "$set": { "revoked_at": px_base::get_current_timestamp() } },
            )
            .await
            .map_err(|e| {
                tracing::error!("revoke user session by token failed: {}", e);
                CmsApiError::DatabaseError
            })?;
        Ok(())
    }
}

// BSON represents Option::None as null. Keeping the value in one place avoids
// accidentally querying for a missing field and accepting malformed sessions.
const BSON_NULL: mongodb::bson::Bson = mongodb::bson::Bson::Null;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn generated_tokens_are_random_and_not_stored_verbatim() {
        let first = CmsUserSessionManager::new_token().unwrap();
        let second = CmsUserSessionManager::new_token().unwrap();
        assert_ne!(first, second);
        assert!(first.len() >= 40);
        assert_ne!(first, CmsUserSessionManager::hash_token(&first));
    }
}
