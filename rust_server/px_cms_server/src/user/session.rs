use crate::cms_api_error::CmsApiError;
use crate::{gCmsDatabase, gUserManager};
use base64::{engine::general_purpose::URL_SAFE_NO_PAD, Engine as _};
use futures_util::StreamExt;
use mongodb::bson::{doc, DateTime};
use px_base::hash_util::{compute_hash, HashAlgo};
use ring::rand::{SecureRandom, SystemRandom};
use serde::{Deserialize, Serialize};
use std::sync::Arc;

const HOUR_MS: i64 = 60 * 60 * 1000;
const DAY_MS: i64 = 24 * HOUR_MS;

fn positive_duration(value: i64, unit_ms: i64) -> i64 {
    value.clamp(1, 3650).saturating_mul(unit_ms)
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CmsUserSession {
    pub sid: String,
    pub token_hash: String,
    #[serde(default = "default_user_subject_type")]
    pub subject_type: String,
    pub subject_id: String,
    pub auth_version: i64,
    pub client_type: String,
    pub created_at: i64,
    pub last_used_at: i64,
    pub expires_at: i64,
    pub absolute_expires_at: i64,
    pub cleanup_at: DateTime,
    pub revoked_at: Option<i64>,
    #[serde(default)]
    pub csrf_hash: String,
    #[serde(default)]
    pub ip_hash: String,
    #[serde(default)]
    pub user_agent_hash: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GuestBlock {
    pub kind: String,
    pub value: String,
    pub created_at: i64,
    pub reason: String,
}

fn default_user_subject_type() -> String {
    "user".to_string()
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
    pub must_change_password: bool,
    pub client_type: String,
    pub csrf_hash: String,
}

#[derive(Debug, Clone)]
pub struct AuthenticatedAdmin {
    pub sid: String,
    pub auth_id: String,
    pub csrf_hash: String,
}

#[derive(Debug, Clone)]
pub struct AuthenticatedGuest {
    pub sid: String,
    pub guest_id: String,
    pub csrf_hash: String,
}

#[derive(Debug, Clone)]
pub struct IssuedAdminSession {
    pub session_token: String,
    pub csrf_token: String,
    pub session: CmsUserSession,
}

pub type IssuedWebUserSession = IssuedAdminSession;

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

    pub async fn list_subject_sessions(
        &self,
        subject_type: &str,
        subject_id: &str,
    ) -> Result<Vec<CmsUserSession>, CmsApiError> {
        let collection = gCmsDatabase.lock().await.user_session();
        let mut cursor = collection
            .lock()
            .await
            .find(doc! {
                "subject_type": subject_type,
                "subject_id": subject_id,
            })
            .sort(doc! { "created_at": -1 })
            .limit(200)
            .await
            .map_err(|error| {
                tracing::error!("list subject sessions failed: {}", error);
                CmsApiError::DatabaseError
            })?;
        let mut sessions = Vec::new();
        while let Some(item) = cursor.next().await {
            sessions.push(item.map_err(|error| {
                tracing::error!("read subject session failed: {}", error);
                CmsApiError::DatabaseError
            })?);
        }
        Ok(sessions)
    }

    pub async fn list_guest_sessions(&self) -> Result<Vec<CmsUserSession>, CmsApiError> {
        let collection = gCmsDatabase.lock().await.user_session();
        let mut cursor = collection
            .lock()
            .await
            .find(doc! { "subject_type": "guest" })
            .sort(doc! { "created_at": -1 })
            .limit(500)
            .await
            .map_err(|error| {
                tracing::error!("list guest sessions failed: {}", error);
                CmsApiError::DatabaseError
            })?;
        let mut sessions = Vec::new();
        while let Some(item) = cursor.next().await {
            sessions.push(item.map_err(|error| {
                tracing::error!("read guest session failed: {}", error);
                CmsApiError::DatabaseError
            })?);
        }
        Ok(sessions)
    }

    pub async fn is_guest_blocked(
        &self,
        guest_id: Option<&str>,
        ip_hash: &str,
    ) -> Result<bool, CmsApiError> {
        let mut choices = vec![doc! { "kind": "ip_hash", "value": ip_hash }];
        if let Some(guest_id) = guest_id.filter(|value| !value.is_empty()) {
            choices.push(doc! { "kind": "guest_id", "value": guest_id });
        }
        gCmsDatabase
            .lock()
            .await
            .guest_block()
            .lock()
            .await
            .find_one(doc! { "$or": choices })
            .await
            .map(|row| row.is_some())
            .map_err(|error| {
                tracing::error!("query guest block failed: {}", error);
                CmsApiError::DatabaseError
            })
    }

    pub async fn block_guest_session(
        &self,
        sid: &str,
        block_guest_id: bool,
        block_ip_hash: bool,
        reason: &str,
    ) -> Result<CmsUserSession, CmsApiError> {
        if !block_guest_id && !block_ip_hash {
            return Err(CmsApiError::InvalidParams);
        }
        let sessions = gCmsDatabase.lock().await.user_session();
        let session = sessions
            .lock()
            .await
            .find_one(doc! { "sid": sid, "subject_type": "guest" })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?
            .ok_or(CmsApiError::ResourceNotFound)?;
        let now = px_base::get_current_timestamp();
        let mut blocked_values = Vec::new();
        if block_guest_id {
            blocked_values.push(("guest_id", session.subject_id.as_str()));
        }
        if block_ip_hash && !session.ip_hash.is_empty() {
            blocked_values.push(("ip_hash", session.ip_hash.as_str()));
        }
        let blocks = gCmsDatabase.lock().await.guest_block();
        for (kind, value) in &blocked_values {
            blocks
                .lock()
                .await
                .update_one(
                    doc! { "kind": kind, "value": value },
                    doc! { "$setOnInsert": {
                        "kind": kind,
                        "value": value,
                        "created_at": now,
                        "reason": reason,
                    }},
                )
                .upsert(true)
                .await
                .map_err(|error| {
                    tracing::error!("persist guest block failed: {}", error);
                    CmsApiError::DatabaseError
                })?;
        }
        let mut revoke_choices = Vec::new();
        if block_guest_id {
            revoke_choices.push(doc! { "subject_id": &session.subject_id });
        }
        if block_ip_hash && !session.ip_hash.is_empty() {
            revoke_choices.push(doc! { "ip_hash": &session.ip_hash });
        }
        sessions
            .lock()
            .await
            .update_many(
                doc! {
                    "subject_type": "guest",
                    "revoked_at": BSON_NULL,
                    "$or": revoke_choices,
                },
                doc! { "$set": { "revoked_at": now } },
            )
            .await
            .map_err(|_| CmsApiError::DatabaseError)?;
        Ok(session)
    }

    pub async fn issue_panel(
        &self,
        uid: String,
        auth_version: i64,
    ) -> Result<IssuedUserSession, CmsApiError> {
        let now = px_base::get_current_timestamp();
        let access_token = Self::new_token()?;
        let policy = crate::gCmsSettings.lock().await.user.clone();
        let absolute_expires_at = now + positive_duration(policy.panel_absolute_days, DAY_MS);
        let expires_at =
            (now + positive_duration(policy.panel_sliding_days, DAY_MS)).min(absolute_expires_at);
        let session = CmsUserSession {
            sid: uuid::Uuid::new_v4().simple().to_string(),
            token_hash: Self::hash_token(&access_token),
            subject_type: "user".to_string(),
            subject_id: uid,
            auth_version,
            client_type: "panel".to_string(),
            created_at: now,
            last_used_at: now,
            expires_at,
            absolute_expires_at,
            cleanup_at: DateTime::from_millis(expires_at),
            revoked_at: None,
            csrf_hash: String::new(),
            ip_hash: String::new(),
            user_agent_hash: String::new(),
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

    pub async fn issue_admin(
        &self,
        auth_id: String,
        ip_hash: String,
        user_agent_hash: String,
    ) -> Result<IssuedAdminSession, CmsApiError> {
        let now = px_base::get_current_timestamp();
        let session_token = Self::new_token()?;
        let csrf_token = Self::new_token()?;
        let policy = crate::gCmsSettings.lock().await.user.clone();
        let absolute_expires_at = now + positive_duration(policy.admin_absolute_hours, HOUR_MS);
        let expires_at =
            (now + positive_duration(policy.admin_sliding_hours, HOUR_MS)).min(absolute_expires_at);
        let session = CmsUserSession {
            sid: uuid::Uuid::new_v4().simple().to_string(),
            token_hash: Self::hash_token(&session_token),
            subject_type: "admin".to_string(),
            subject_id: auth_id,
            auth_version: 0,
            client_type: "admin_web".to_string(),
            created_at: now,
            last_used_at: now,
            expires_at,
            absolute_expires_at,
            cleanup_at: DateTime::from_millis(expires_at),
            revoked_at: None,
            csrf_hash: Self::hash_token(&csrf_token),
            ip_hash,
            user_agent_hash,
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
                tracing::error!("insert admin session failed: {}", e);
                CmsApiError::DatabaseError
            })?;
        Ok(IssuedAdminSession {
            session_token,
            csrf_token,
            session,
        })
    }

    pub async fn issue_user_web(
        &self,
        uid: String,
        auth_version: i64,
        ip_hash: String,
        user_agent_hash: String,
    ) -> Result<IssuedWebUserSession, CmsApiError> {
        let now = px_base::get_current_timestamp();
        let session_token = Self::new_token()?;
        let csrf_token = Self::new_token()?;
        let policy = crate::gCmsSettings.lock().await.user.clone();
        let absolute_expires_at = now + positive_duration(policy.web_absolute_days, DAY_MS);
        let expires_at =
            (now + positive_duration(policy.web_sliding_hours, HOUR_MS)).min(absolute_expires_at);
        let session = CmsUserSession {
            sid: uuid::Uuid::new_v4().simple().to_string(),
            token_hash: Self::hash_token(&session_token),
            subject_type: "user".to_string(),
            subject_id: uid,
            auth_version,
            client_type: "user_web".to_string(),
            created_at: now,
            last_used_at: now,
            expires_at,
            absolute_expires_at,
            cleanup_at: DateTime::from_millis(expires_at),
            revoked_at: None,
            csrf_hash: Self::hash_token(&csrf_token),
            ip_hash,
            user_agent_hash,
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
                tracing::error!("insert browser user session failed: {}", e);
                CmsApiError::DatabaseError
            })?;
        Ok(IssuedWebUserSession {
            session_token,
            csrf_token,
            session,
        })
    }

    pub async fn issue_guest(
        &self,
        guest_id: String,
        ip_hash: String,
        user_agent_hash: String,
        client_type: String,
    ) -> Result<IssuedAdminSession, CmsApiError> {
        let now = px_base::get_current_timestamp();
        let hours = crate::gCmsSettings
            .lock()
            .await
            .user
            .guest_absolute_hours
            .max(1);
        let absolute_expires_at = now + positive_duration(hours, HOUR_MS);
        let session_token = Self::new_token()?;
        let csrf_token = Self::new_token()?;
        let session = CmsUserSession {
            sid: uuid::Uuid::new_v4().simple().to_string(),
            token_hash: Self::hash_token(&session_token),
            subject_type: "guest".to_string(),
            subject_id: guest_id,
            auth_version: 0,
            client_type,
            created_at: now,
            last_used_at: now,
            expires_at: absolute_expires_at,
            absolute_expires_at,
            cleanup_at: DateTime::from_millis(absolute_expires_at),
            revoked_at: None,
            csrf_hash: Self::hash_token(&csrf_token),
            ip_hash,
            user_agent_hash,
        };
        gCmsDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .insert_one(session.clone())
            .await
            .map_err(|error| {
                tracing::error!("insert guest session failed: {}", error);
                CmsApiError::DatabaseError
            })?;
        Ok(IssuedAdminSession {
            session_token,
            csrf_token,
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
                "subject_type": "user",
                "client_type": "panel",
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
        if user.deleted || user.disabled || user.auth_version != session.auth_version {
            return Err(CmsApiError::AuthenticationRequired);
        }

        let sliding_days = crate::gCmsSettings.lock().await.user.panel_sliding_days;
        let extended_expires_at =
            (now + positive_duration(sliding_days, DAY_MS)).min(session.absolute_expires_at);
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
            must_change_password: user.must_change_password,
            client_type: session.client_type,
            csrf_hash: session.csrf_hash,
        })
    }

    pub async fn authenticate_user_web(
        &self,
        token: &str,
    ) -> Result<AuthenticatedUser, CmsApiError> {
        if token.len() < 32 {
            return Err(CmsApiError::AuthenticationRequired);
        }
        let now = px_base::get_current_timestamp();
        let collection = gCmsDatabase.lock().await.user_session();
        let session = collection
            .lock()
            .await
            .find_one(doc! {
                "token_hash": Self::hash_token(token),
                "subject_type": "user",
                "client_type": "user_web",
                "revoked_at": BSON_NULL,
                "expires_at": { "$gt": now },
                "absolute_expires_at": { "$gt": now },
            })
            .await
            .map_err(|e| {
                tracing::error!("query browser user session failed: {}", e);
                CmsApiError::DatabaseError
            })?
            .ok_or(CmsApiError::AuthenticationRequired)?;
        let user = gUserManager
            .query_user_by_id(session.subject_id.clone())
            .await
            .map_err(|_| CmsApiError::AuthenticationRequired)?;
        if user.deleted || user.disabled || user.auth_version != session.auth_version {
            return Err(CmsApiError::AuthenticationRequired);
        }
        let sliding_hours = crate::gCmsSettings.lock().await.user.web_sliding_hours;
        let extended_expires_at =
            (now + positive_duration(sliding_hours, HOUR_MS)).min(session.absolute_expires_at);
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
                tracing::error!("refresh browser user session failed: {}", e);
                CmsApiError::DatabaseError
            })?;
        Ok(AuthenticatedUser {
            sid: session.sid,
            uid: session.subject_id,
            must_change_password: user.must_change_password,
            client_type: session.client_type,
            csrf_hash: session.csrf_hash,
        })
    }

    pub async fn authenticate_admin(&self, token: &str) -> Result<AuthenticatedAdmin, CmsApiError> {
        if token.len() < 32 {
            return Err(CmsApiError::AuthenticationRequired);
        }
        let now = px_base::get_current_timestamp();
        let collection = gCmsDatabase.lock().await.user_session();
        let session = collection
            .lock()
            .await
            .find_one(doc! {
                "token_hash": Self::hash_token(token),
                "subject_type": "admin",
                "client_type": "admin_web",
                "revoked_at": BSON_NULL,
                "expires_at": { "$gt": now },
                "absolute_expires_at": { "$gt": now },
            })
            .await
            .map_err(|e| {
                tracing::error!("query admin session failed: {}", e);
                CmsApiError::DatabaseError
            })?
            .ok_or(CmsApiError::AuthenticationRequired)?;
        let auth = crate::gAuthManager.lock().await.get_auth().await;
        if auth.auth_id.is_empty() || auth.auth_id != session.subject_id {
            return Err(CmsApiError::AuthenticationRequired);
        }
        let sliding_hours = crate::gCmsSettings.lock().await.user.admin_sliding_hours;
        let extended_expires_at =
            (now + positive_duration(sliding_hours, HOUR_MS)).min(session.absolute_expires_at);
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
                tracing::error!("refresh admin session failed: {}", e);
                CmsApiError::DatabaseError
            })?;
        Ok(AuthenticatedAdmin {
            sid: session.sid,
            auth_id: session.subject_id,
            csrf_hash: session.csrf_hash,
        })
    }

    pub async fn authenticate_guest(&self, token: &str) -> Result<AuthenticatedGuest, CmsApiError> {
        self.authenticate_guest_client(token, "guest_web").await
    }

    pub async fn authenticate_guest_panel(
        &self,
        token: &str,
    ) -> Result<AuthenticatedGuest, CmsApiError> {
        self.authenticate_guest_client(token, "guest_panel").await
    }

    async fn authenticate_guest_client(
        &self,
        token: &str,
        client_type: &str,
    ) -> Result<AuthenticatedGuest, CmsApiError> {
        if token.len() < 32 {
            return Err(CmsApiError::AuthenticationRequired);
        }
        let now = px_base::get_current_timestamp();
        let session = gCmsDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .find_one(doc! {
                "token_hash": Self::hash_token(token),
                "subject_type": "guest",
                "client_type": client_type,
                "revoked_at": BSON_NULL,
                "expires_at": { "$gt": now },
                "absolute_expires_at": { "$gt": now },
            })
            .await
            .map_err(|_| CmsApiError::DatabaseError)?
            .ok_or(CmsApiError::AuthenticationRequired)?;
        if self
            .is_guest_blocked(Some(&session.subject_id), &session.ip_hash)
            .await?
        {
            return Err(CmsApiError::Forbidden);
        }
        Ok(AuthenticatedGuest {
            sid: session.sid,
            guest_id: session.subject_id,
            csrf_hash: session.csrf_hash,
        })
    }

    pub fn verify_csrf(subject: &AuthenticatedAdmin, csrf_token: &str) -> bool {
        csrf_token.len() >= 32 && Self::hash_token(csrf_token) == subject.csrf_hash
    }

    pub fn verify_user_csrf(subject: &AuthenticatedUser, csrf_token: &str) -> bool {
        csrf_token.len() >= 32 && Self::hash_token(csrf_token) == subject.csrf_hash
    }

    pub fn verify_guest_csrf(subject: &AuthenticatedGuest, csrf_token: &str) -> bool {
        csrf_token.len() >= 32 && Self::hash_token(csrf_token) == subject.csrf_hash
    }

    /// Rotate the double-submit token for an already authenticated browser
    /// session. The HttpOnly session cookie remains unchanged, so a fresh tab
    /// can recover write access without asking the user to sign in again.
    pub async fn refresh_user_csrf(&self, sid: &str) -> Result<String, CmsApiError> {
        let csrf_token = Self::new_token()?;
        let result = gCmsDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .update_one(
                doc! {
                    "sid": sid,
                    "subject_type": "user",
                    "client_type": "user_web",
                    "revoked_at": BSON_NULL,
                    "expires_at": { "$gt": px_base::get_current_timestamp() },
                },
                doc! { "$set": { "csrf_hash": Self::hash_token(&csrf_token) } },
            )
            .await
            .map_err(|error| {
                tracing::error!("refresh browser user csrf failed: {}", error);
                CmsApiError::DatabaseError
            })?;
        if result.matched_count != 1 {
            return Err(CmsApiError::AuthenticationRequired);
        }
        Ok(csrf_token)
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

    pub async fn revoke_all(&self, uid: &str) -> Result<(), CmsApiError> {
        gCmsDatabase
            .lock()
            .await
            .user_session()
            .lock()
            .await
            .update_many(
                doc! { "subject_id": uid, "revoked_at": BSON_NULL },
                doc! { "$set": { "revoked_at": px_base::get_current_timestamp() } },
            )
            .await
            .map_err(|e| {
                tracing::error!("revoke all user sessions failed: {}", e);
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
