use crate::author::{Author, AuthorRole};
use crate::{gAuthorDatabase, gAuthorSettings};
use argon2::{
    Argon2,
    password_hash::{PasswordHash, PasswordHasher, PasswordVerifier, SaltString, rand_core::OsRng},
};
use futures_util::StreamExt;
use mongodb::Cursor;
use mongodb::bson::doc;

pub struct AuthorManager {}

impl AuthorManager {
    pub fn new() -> Self {
        Self {}
    }

    pub async fn init(&self) -> bool {
        let (admin_name, admin_password, visitor_name, visitor_password) = {
            let settings = gAuthorSettings.lock().await;
            (
                validated_name(&settings.bootstrap.admin_name),
                optional_secret(settings.bootstrap.admin_password.as_deref()),
                validated_name(&settings.bootstrap.visitor_name),
                optional_secret(settings.bootstrap.visitor_password.as_deref()),
            )
        };

        let Some(admin_name) = admin_name else {
            tracing::error!("bootstrap.admin_name is required to initialize the server");
            return false;
        };
        let Some(admin_password) = admin_password else {
            tracing::error!(
                "bootstrap.admin_password is required to create the initial admin account '{}'",
                admin_name,
            );
            return false;
        };

        if !self
            .upsert_bootstrap_author(admin_name.clone(), admin_password, AuthorRole::Admin)
            .await
        {
            return false;
        }

        if let Some(visitor_name) = visitor_name {
            match visitor_password {
                Some(visitor_password) => {
                    if !self
                        .upsert_bootstrap_author(
                            visitor_name.clone(),
                            visitor_password,
                            AuthorRole::Visitor,
                        )
                        .await
                    {
                        return false;
                    }
                }
                None => {
                    if !self.has_author(&visitor_name).await {
                        tracing::warn!(
                            "bootstrap.visitor_password is not set; skip creating initial visitor account '{}'",
                            visitor_name,
                        );
                    }
                }
            }
        } else if visitor_password.is_some() {
            tracing::error!(
                "bootstrap.visitor_password is set but visitor_name is empty; refusing to start"
            );
            return false;
        }

        true
    }

    async fn has_author(&self, author_name: &str) -> bool {
        self.find_author_by_name(author_name.to_string())
            .await
            .is_some()
    }

    async fn insert_bootstrap_author(
        &self,
        name: String,
        plain_password: String,
        role: AuthorRole,
    ) -> bool {
        let Ok(password_hash) = Self::hash_password(&plain_password) else {
            tracing::error!("hash password failed for bootstrap account '{}'", name);
            return false;
        };

        self.insert_author(Author {
            name,
            password_hash,
            role,
        })
        .await
    }

    /// Creates the bootstrap author if it does not exist, or updates its
    /// password hash (and role) if it already exists. This ensures that
    /// changing the password in the settings file takes effect on every
    /// restart, instead of only on first creation.
    async fn upsert_bootstrap_author(
        &self,
        name: String,
        plain_password: String,
        role: AuthorRole,
    ) -> bool {
        let Ok(password_hash) = Self::hash_password(&plain_password) else {
            tracing::error!("hash password failed for bootstrap account '{}'", name);
            return false;
        };

        if self.find_author_by_name(name.clone()).await.is_some() {
            // Author exists — update password and role from config.
            return self
                .update_author_password(&name, &password_hash, role)
                .await;
        }

        // Author does not exist — create it.
        self.insert_author(Author {
            name,
            password_hash,
            role,
        })
        .await
    }

    async fn update_author_password(
        &self,
        name: &str,
        password_hash: &str,
        role: AuthorRole,
    ) -> bool {
        let c_author = gAuthorDatabase.lock().await.author();
        let role_bson = serde_json::to_value(&role)
            .ok()
            .and_then(|v| mongodb::bson::to_bson(&v).ok())
            .unwrap_or(mongodb::bson::Bson::String("visitor".to_string()));
        let filter = doc! { "name": name };
        let update = doc! {
            "$set": {
                "password_hash": password_hash,
                "role": role_bson,
            }
        };
        let r = c_author.lock().await.update_one(filter, update).await;
        if let Err(e) = &r {
            tracing::error!("update_author_password failed for '{}': {}", name, e);
            return false;
        }
        let updated = r.map(|r| r.matched_count > 0).unwrap_or(false);
        if updated {
            tracing::info!(
                "bootstrap account '{}' password updated from settings",
                name
            );
        }
        updated
    }

    pub async fn find_author_by_name(&self, author_name: String) -> Option<Author> {
        let c_author = gAuthorDatabase.lock().await.author();
        let filter = doc! {
            "name": author_name,
        };
        c_author
            .lock()
            .await
            .find_one(filter)
            .await
            .unwrap_or_default()
    }

    pub async fn find_authors(&self) -> Result<Vec<Author>, String> {
        let c_author = gAuthorDatabase.lock().await.author();
        let cursor = c_author.lock().await.find(doc! {}).await;
        if let Err(e) = cursor {
            return Err(e.to_string());
        }

        let mut cursor: Cursor<Author> = cursor.unwrap();
        let mut authors: Vec<Author> = Vec::new();
        while let Some(author) = cursor.next().await {
            if let Err(e) = author {
                tracing::error!("query group error: {}", e);
                break;
            } else {
                let author = author.unwrap();
                authors.push(author);
            }
        }
        Ok(authors)
    }

    pub async fn insert_author(&self, author: Author) -> bool {
        let c_author = gAuthorDatabase.lock().await.author();
        let r = c_author.lock().await.insert_one(author).await;
        if let Err(e) = &r {
            tracing::error!("insert_author failed: {}", e);
        }
        r.is_ok()
    }

    pub fn hash_password(plain_password: &str) -> Result<String, String> {
        if plain_password.is_empty() {
            return Err("password is empty".to_string());
        }

        let salt = SaltString::generate(&mut OsRng);
        Argon2::default()
            .hash_password(plain_password.as_bytes(), &salt)
            .map(|hash| hash.to_string())
            .map_err(|e| e.to_string())
    }

    pub fn verify_password(plain_password: &str, password_hash: &str) -> bool {
        if plain_password.is_empty() || password_hash.is_empty() {
            return false;
        }

        let Ok(parsed_hash) = PasswordHash::new(password_hash) else {
            return false;
        };

        Argon2::default()
            .verify_password(plain_password.as_bytes(), &parsed_hash)
            .is_ok()
    }

    pub async fn verify_author(
        &self,
        author_name: String,
        plain_password: String,
    ) -> Option<Author> {
        if let Some(author) = self.find_author_by_name(author_name.clone()).await
            && author.name == author_name
            && Self::verify_password(&plain_password, &author.password_hash)
        {
            return Some(author);
        }
        None
    }
}

fn validated_name(name: &str) -> Option<String> {
    let name = name.trim();
    if name.is_empty() {
        None
    } else {
        Some(name.to_string())
    }
}

fn optional_secret(secret: Option<&str>) -> Option<String> {
    secret
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .filter(|value| !value.starts_with('<'))
        .filter(|value| !value.starts_with("CHANGE_ME"))
        .map(ToString::to_string)
}

#[cfg(test)]
mod tests {
    use crate::author_manager::{AuthorManager, optional_secret, validated_name};

    #[test]
    fn hash_password_does_not_return_plaintext() {
        let hash = AuthorManager::hash_password("password").expect("password should hash");

        assert_ne!(hash, "password");
        assert!(hash.starts_with("$argon2"));
    }

    #[test]
    fn hash_password_uses_unique_salt() {
        let first = AuthorManager::hash_password("password").expect("password should hash");
        let second = AuthorManager::hash_password("password").expect("password should hash");

        assert_ne!(first, second);
    }

    #[test]
    fn verify_password_accepts_correct_password() {
        let hash = AuthorManager::hash_password("password").expect("password should hash");

        assert!(AuthorManager::verify_password("password", &hash));
    }

    #[test]
    fn verify_password_rejects_wrong_password() {
        let hash = AuthorManager::hash_password("password").expect("password should hash");

        assert!(!AuthorManager::verify_password("wrong-password", &hash));
    }

    #[test]
    fn hash_password_rejects_empty_password() {
        assert!(AuthorManager::hash_password("").is_err());
        assert!(!AuthorManager::verify_password("", ""));
    }

    #[test]
    fn validated_name_uses_configured_name() {
        assert_eq!(
            validated_name("ConfiguredAdmin"),
            Some("ConfiguredAdmin".to_string())
        );
    }

    #[test]
    fn validated_name_rejects_empty_or_whitespace() {
        assert_eq!(validated_name(""), None);
        assert_eq!(validated_name("   "), None);
    }

    #[test]
    fn optional_secret_rejects_empty_and_placeholder_values() {
        assert!(optional_secret(None).is_none());
        assert!(optional_secret(Some(" ")).is_none());
        assert!(optional_secret(Some("<set-password>")).is_none());
        assert!(optional_secret(Some("CHANGE_ME_ADMIN_PASSWORD")).is_none());
        assert_eq!(optional_secret(Some("secret")).unwrap(), "secret");
    }
}
