use crate::console_api_error::ConsoleApiError;
use crate::gConsoleDatabase;
use crate::user::console_user::ConsoleUser;
use crate::user::console_user_keys::KEY_USER_ID;
use crate::user::password;
use futures_util::StreamExt;
use mongodb::bson::oid::ObjectId;
use mongodb::bson::{doc, Document};
use std::sync::Arc;

pub struct ConsoleUserManager {}

fn validated_username(value: &str) -> Result<(String, String), ConsoleApiError> {
    // Do not silently canonicalize surrounding whitespace: accepting a value
    // different from what the caller submitted makes account names ambiguous.
    let username = value.to_string();
    if username.chars().count() < 2
        || username.chars().count() > 64
        || username.chars().any(char::is_control)
        || username.trim() != username
        || username.contains('/')
        || username.contains('\\')
    {
        return Err(ConsoleApiError::InvalidParams);
    }
    let normalized = username.to_lowercase();
    Ok((username, normalized))
}

fn regex_literal(value: &str) -> String {
    let mut escaped = String::with_capacity(value.len());
    for character in value.chars() {
        if matches!(
            character,
            '\\' | '.' | '^' | '$' | '*' | '+' | '?' | '(' | ')' | '[' | ']' | '{' | '}' | '|'
        ) {
            escaped.push('\\');
        }
        escaped.push(character);
    }
    escaped
}

async fn encrypt_password_for_admin(plain_password: &str) -> Result<String, ConsoleApiError> {
    let installation_secret = crate::gConsoleSettings
        .lock()
        .await
        .privacy_hash_salt
        .clone();
    password::encrypt_recoverable(plain_password, &installation_secret).map_err(|error| {
        tracing::error!("encrypt recoverable user password failed: {}", error);
        ConsoleApiError::InternalError
    })
}

impl ConsoleUserManager {
    pub fn new() -> Arc<Self> {
        Arc::new(ConsoleUserManager {})
    }

    pub async fn register_user(
        &self,
        username: String,
        plain_password: String,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        let (username, username_normalized) = validated_username(&username)?;
        let r = self.query_user_by_username(username.clone()).await;
        if let Ok(_user) = r {
            tracing::warn!("the user: {} already exists", username);
            return Err(ConsoleApiError::UserAlreadyExists);
        }

        let object_id = ObjectId::new();
        let uid = px_base::md5_hex(&object_id.to_string());
        let password_hash = password::hash(&plain_password).map_err(|e| {
            tracing::warn!("invalid password while registering user: {}", e);
            ConsoleApiError::InvalidParams
        })?;
        let password_ciphertext = encrypt_password_for_admin(&plain_password).await?;
        let user = ConsoleUser {
            uid,
            username: username.clone(),
            username_normalized,
            password_hash,
            password_ciphertext,
            assigned: false,
            created_timestamp: px_base::get_current_timestamp(),
            update_timestamp: px_base::get_current_timestamp(),
            deleted: false,
            disabled: false,
            avatar_path: "".to_string(),
            auth_version: 1,
            must_change_password: false,
            version: 1,
            total: 0,
        };

        let c_user = gConsoleDatabase.lock().await.user();
        if let Err(e) = c_user.lock().await.insert_one(user).await {
            tracing::error!("insert console user failed: {}", e);
            return Err(ConsoleApiError::DatabaseError);
        }

        self.query_user_by_username(username).await
    }

    /// Creates an administrator-managed account without a forced first-login
    /// password change. Administrators may retrieve the encrypted password.
    pub async fn create_managed_user(
        &self,
        username: String,
        plain_password: String,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        self.register_user(username, plain_password).await
    }

    pub async fn admin_update_user(
        &self,
        uid: String,
        version: i64,
        username: Option<String>,
        disabled: Option<bool>,
        avatar_path: Option<String>,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        let mut set = doc! { "update_timestamp": px_base::get_current_timestamp() };
        let mut revoke_sessions = false;
        if let Some(username) = username {
            let (username, normalized) = validated_username(&username)?;
            if let Ok(existing) = self.query_user_by_username(username.clone()).await {
                if existing.uid != uid {
                    return Err(ConsoleApiError::UserAlreadyExists);
                }
            }
            set.insert("username", username);
            set.insert("username_normalized", normalized);
        }
        if let Some(disabled) = disabled {
            set.insert("disabled", disabled);
            revoke_sessions = disabled;
        }
        if let Some(avatar_path) = avatar_path {
            let avatar_path = avatar_path.trim();
            if avatar_path.len() > 512 || avatar_path.chars().any(char::is_control) {
                return Err(ConsoleApiError::InvalidParams);
            }
            set.insert("avatar_path", avatar_path);
        }

        let mut inc = doc! { "version": 1_i64 };
        if revoke_sessions {
            inc.insert("auth_version", 1_i64);
        }
        let collection = gConsoleDatabase.lock().await.user();
        let result = collection
            .lock()
            .await
            .update_one(
                doc! { "uid": &uid, "version": version, "deleted": false },
                doc! { "$set": set, "$inc": inc },
            )
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?;
        if result.matched_count == 0 {
            return match self.query_user_by_id(uid.clone()).await {
                Ok(_) => Err(ConsoleApiError::VersionConflict),
                Err(_) => Err(ConsoleApiError::UserNotFound),
            };
        }
        if revoke_sessions {
            crate::gUserSessionManager.revoke_all(&uid).await?;
        }
        self.query_user_by_id(uid).await
    }

    pub async fn admin_delete_user(
        &self,
        uid: String,
        version: i64,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        let collection = gConsoleDatabase.lock().await.user();
        let result = collection
            .lock()
            .await
            .update_one(
                doc! { "uid": &uid, "version": version, "deleted": false },
                doc! {
                    "$set": {
                        "deleted": true,
                        "disabled": true,
                        "update_timestamp": px_base::get_current_timestamp(),
                    },
                    "$inc": { "version": 1_i64, "auth_version": 1_i64 }
                },
            )
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?;
        if result.matched_count == 0 {
            return match self.query_user_by_id(uid.clone()).await {
                Ok(user) if user.deleted => Ok(user),
                Ok(_) => Err(ConsoleApiError::VersionConflict),
                Err(_) => Err(ConsoleApiError::UserNotFound),
            };
        }
        crate::gUserSessionManager.revoke_all(&uid).await?;
        self.query_user_by_id(uid).await
    }

    pub async fn admin_reset_password(
        &self,
        uid: String,
        version: i64,
        plain_password: String,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        let password_hash =
            password::hash(&plain_password).map_err(|_| ConsoleApiError::InvalidParams)?;
        let password_ciphertext = encrypt_password_for_admin(&plain_password).await?;
        let collection = gConsoleDatabase.lock().await.user();
        let result = collection
            .lock()
            .await
            .update_one(
                doc! { "uid": &uid, "version": version, "deleted": false },
                doc! {
                    "$set": {
                        "password_hash": password_hash,
                        "password_ciphertext": password_ciphertext,
                        "must_change_password": false,
                        "update_timestamp": px_base::get_current_timestamp(),
                    },
                    "$inc": { "version": 1_i64, "auth_version": 1_i64 }
                },
            )
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?;
        if result.matched_count == 0 {
            return match self.query_user_by_id(uid.clone()).await {
                Ok(_) => Err(ConsoleApiError::VersionConflict),
                Err(_) => Err(ConsoleApiError::UserNotFound),
            };
        }
        crate::gUserSessionManager.revoke_all(&uid).await?;
        self.query_user_by_id(uid).await
    }

    pub async fn admin_recover_password(
        &self,
        uid: String,
    ) -> Result<Option<String>, ConsoleApiError> {
        let user = self.query_user_by_id(uid).await?;
        if user.password_ciphertext.is_empty() {
            return Ok(None);
        }
        let installation_secret = crate::gConsoleSettings
            .lock()
            .await
            .privacy_hash_salt
            .clone();
        password::decrypt_recoverable(&user.password_ciphertext, &installation_secret)
            .map(Some)
            .map_err(|error| {
                tracing::error!("decrypt recoverable user password failed: {}", error);
                ConsoleApiError::InternalError
            })
    }

    pub async fn revoke_all_sessions(&self, uid: String) -> Result<ConsoleUser, ConsoleApiError> {
        let collection = gConsoleDatabase.lock().await.user();
        let result = collection
            .lock()
            .await
            .update_one(
                doc! { "uid": &uid, "deleted": false },
                doc! {
                    "$set": { "update_timestamp": px_base::get_current_timestamp() },
                    "$inc": { "auth_version": 1_i64, "version": 1_i64 }
                },
            )
            .await
            .map_err(|_| ConsoleApiError::DatabaseError)?;
        if result.matched_count == 0 {
            return Err(ConsoleApiError::UserNotFound);
        }
        crate::gUserSessionManager.revoke_all(&uid).await?;
        self.query_user_by_id(uid).await
    }

    pub async fn update_user_password(
        &self,
        uid: String,
        plain_password: String,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        let password_hash =
            password::hash(&plain_password).map_err(|_| ConsoleApiError::InvalidParams)?;
        let password_ciphertext = encrypt_password_for_admin(&plain_password).await?;
        let filter_doc = doc! {"uid": uid.clone()};
        let update_doc = doc! {
            "$set": {
                "password_hash": password_hash,
                "password_ciphertext": password_ciphertext,
                "must_change_password": false,
                "update_timestamp": px_base::get_current_timestamp(),
            },
            "$inc": { "auth_version": 1_i64, "version": 1_i64 }
        };
        let c_user = gConsoleDatabase.lock().await.user();
        c_user
            .lock()
            .await
            .update_one(filter_doc, update_doc)
            .await
            .map_err(|e| {
                tracing::error!("update user password failed: {}", e);
                ConsoleApiError::DatabaseError
            })?;
        self.query_user_by_id(uid).await
    }

    pub async fn update_username(
        &self,
        uid: String,
        username: String,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        let (username, normalized) = validated_username(&username)?;
        if let Ok(existing) = self.query_user_by_username(username.clone()).await {
            if existing.uid != uid {
                return Err(ConsoleApiError::UserAlreadyExists);
            }
        }

        let c_user = gConsoleDatabase.lock().await.user();
        c_user
            .lock()
            .await
            .update_one(
                doc! { "uid": &uid, "deleted": false },
                doc! {
                    "$set": {
                        "username": username,
                        "username_normalized": normalized,
                        "update_timestamp": px_base::get_current_timestamp(),
                    },
                    "$inc": { "version": 1_i64 }
                },
            )
            .await
            .map_err(|e| {
                tracing::error!("update username failed: {}", e);
                ConsoleApiError::DatabaseError
            })?;
        self.query_user_by_id(uid).await
    }

    pub async fn update_avatar_path(
        &self,
        uid: String,
        avatar_path: String,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        let c_user = gConsoleDatabase.lock().await.user();
        c_user
            .lock()
            .await
            .update_one(
                doc! { "uid": &uid, "deleted": false },
                doc! {
                    "$set": {
                        "avatar_path": avatar_path,
                        "update_timestamp": px_base::get_current_timestamp(),
                    },
                    "$inc": { "version": 1_i64 }
                },
            )
            .await
            .map_err(|e| {
                tracing::error!("update avatar path failed: {}", e);
                ConsoleApiError::DatabaseError
            })?;
        self.query_user_by_id(uid).await
    }

    pub async fn query_user_by_id(&self, uid: String) -> Result<ConsoleUser, ConsoleApiError> {
        let c_user = gConsoleDatabase.lock().await.user();
        let filter = doc! {
            KEY_USER_ID: uid,
        };
        let r = c_user.lock().await.find_one(filter).await;
        if let Err(e) = r {
            tracing::error!("query user by uid error: {}", e);
            return Err(ConsoleApiError::DatabaseError);
        }
        let r = r.unwrap();
        if r.is_none() {
            return Err(ConsoleApiError::UserNotFound);
        }
        Ok(r.unwrap())
    }

    pub async fn query_user_by_username(
        &self,
        username: String,
    ) -> Result<ConsoleUser, ConsoleApiError> {
        let c_user = gConsoleDatabase.lock().await.user();
        let filter = doc! {
            "username_normalized": username.trim().to_lowercase(),
        };
        let r = c_user.lock().await.find_one(filter).await;
        if let Err(e) = r {
            tracing::error!("query user by username error: {}", e);
            return Err(ConsoleApiError::DatabaseError);
        }
        let r = r.unwrap();
        if r.is_none() {
            tracing::error!("user not found: {}", username);
            return Err(ConsoleApiError::UserNotFound);
        }
        Ok(r.unwrap())
    }

    pub async fn query_users(
        &self,
        page: i32,
        page_size: i32,
        username: String,           // Like
        uid: String,                // Like
        sort_field: Option<String>, // sort field
        sort_order: Option<i32>,    // 1= asc, -1=dec
    ) -> Result<Vec<ConsoleUser>, ConsoleApiError> {
        let c_user = gConsoleDatabase.lock().await.user();
        let skip = (page - 1) * page_size;
        let limit = page_size as i64;

        let mut and_conditions: Vec<Document> = vec![doc! { "deleted": false }];
        if !username.is_empty() {
            and_conditions.push(doc! {
                "username": {
                    "$regex": regex_literal(&username),
                    "$options": "i"
                }
            });
        }
        if !uid.is_empty() {
            and_conditions.push(doc! {
               "uid": {
                    "$regex": regex_literal(&uid),
                    "$options": "i"
                }
            });
        }

        let filter = doc! { "$and": and_conditions };

        //tracing::info!("filter: {:#?}", filter);

        let sort_doc = if let Some(field) = sort_field {
            let order = sort_order.unwrap_or(1);
            doc! { field: order }
        } else {
            doc! {}
        };

        let cursor = c_user
            .lock()
            .await
            .find(filter)
            .sort(sort_doc)
            .skip(skip as u64)
            .limit(limit)
            .await;
        if let Err(e) = cursor {
            tracing::error!("query users error: {}", e);
            return Err(ConsoleApiError::DatabaseError);
        }
        let mut cursor = cursor.unwrap();

        let mut users: Vec<ConsoleUser> = Vec::new();
        while let Some(device) = cursor.next().await {
            if let Err(e) = device {
                tracing::error!("error to get value in cursor: {}", e);
                break;
            } else {
                users.push(device.unwrap());
            }
        }
        Ok(users)
    }

    pub async fn count_users(&self) -> Result<u32, ConsoleApiError> {
        self.count_users_matching("").await
    }

    pub async fn count_users_matching(&self, username: &str) -> Result<u32, ConsoleApiError> {
        let c_user = gConsoleDatabase.lock().await.user();
        let r = c_user.lock().await;
        let mut filter = doc! { "deleted": false };
        if !username.is_empty() {
            filter.insert(
                "username",
                doc! { "$regex": regex_literal(username), "$options": "i" },
            );
        }
        if let Ok(count) = r.count_documents(filter).await {
            Ok(count as u32)
        } else {
            Err(ConsoleApiError::DatabaseError)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gen_random_users() {
        for _i in 0..10 {}
    }

    #[test]
    fn user_search_is_treated_as_literal_text() {
        assert_eq!(regex_literal("a[b].*"), r"a\[b\]\.\*");
    }

    #[test]
    fn username_validation_normalizes_and_rejects_ambiguous_names() {
        assert_eq!(
            validated_username("Alice").unwrap(),
            ("Alice".to_string(), "alice".to_string())
        );
        assert!(validated_username("ab").is_ok());
        assert!(validated_username("a").is_err());
        assert!(validated_username(" Alice ").is_err());
        assert!(validated_username("ali/ce").is_err());
        assert!(validated_username("ali\\ce").is_err());
        assert!(validated_username("ali\nce").is_err());
    }
}
