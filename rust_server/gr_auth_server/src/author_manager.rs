use futures_util::StreamExt;
use mongodb::bson::doc;
use mongodb::Cursor;
use argon2::{
    Argon2,
    password_hash::{
        PasswordHash,
        PasswordHasher,
        PasswordVerifier,
        SaltString,
        rand_core::OsRng,
    },
};
use crate::author::Author;
use crate::{gAuthorDatabase, gAuthorSettings};

pub const AUTHOR_ADMIN: &str = "Admin";
pub const AUTHOR_VISITOR: &str = "Visitor";
pub const AUTHOR_PERM_ALL: &str = "perm_all";
pub const AUTHOR_PERM_VISITOR: &str = "perm_visitor";

pub struct AuthorManager {

}

impl AuthorManager {

    pub fn new() -> Self {
        Self {}
    }

    pub async fn init(&self) -> bool {
        let (admin_name, admin_password, visitor_name, visitor_password) = {
            let settings = gAuthorSettings.lock().await;
            (
                configured_name(&settings.bootstrap.admin_name, AUTHOR_ADMIN),
                optional_secret(settings.bootstrap.admin_password.as_deref()),
                configured_name(&settings.bootstrap.visitor_name, AUTHOR_VISITOR),
                optional_secret(settings.bootstrap.visitor_password.as_deref()),
            )
        };

        if !self.has_author(&admin_name).await {
            let Some(admin_password) = admin_password else {
                tracing::error!(
                    "bootstrap.admin_password is required to create initial admin account '{}'",
                    admin_name,
                );
                return false;
            };

            if !self.insert_bootstrap_author(admin_name.clone(), admin_password, AUTHOR_PERM_ALL).await {
                return false;
            }
        }

        if !self.has_author(&visitor_name).await {
            match visitor_password {
                Some(visitor_password) => {
                    if !self.insert_bootstrap_author(visitor_name.clone(), visitor_password, AUTHOR_PERM_VISITOR).await {
                        return false;
                    }
                }
                None => {
                    tracing::warn!(
                        "bootstrap.visitor_password is not set; skip creating initial visitor account '{}'",
                        visitor_name,
                    );
                }
            }
        }

        true
    }

    async fn has_author(&self, author_name: &str) -> bool {
        self
            .find_author_by_name(author_name.to_string())
            .await.is_some()
    }

    async fn insert_bootstrap_author(&self, name: String, plain_password: String, permission: &str) -> bool {
        let Ok(password_hash) = Self::hash_password(&plain_password) else {
            tracing::error!("hash password failed for bootstrap account '{}'", name);
            return false;
        };

        self.insert_author(Author {
            name,
            password_hash,
            permission: permission.to_string(),
        }).await
    }

    pub async fn find_author_by_name(&self, author_name: String) -> Option<Author> {
        let c_author = gAuthorDatabase
            .lock().await
            .author();
        let filter = doc! {
            "name": author_name,
        };
        if let Ok(opt_author) = c_author
            .lock().await
            .find_one(filter, ).await {
            opt_author
        }
        else {
            None
        }
    }

    pub async fn find_authors(&self) -> Result<Vec<Author>, String> {
        let c_author = gAuthorDatabase
            .lock().await
            .author();
        let cursor = c_author
            .lock().await
            .find(doc! {}, ).await;
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
                println!("device: {:?}", author);
                authors.push(author);
            }
        }
        Ok(authors)
    }

    pub async fn insert_author(&self, author: Author) -> bool {
        let c_author = gAuthorDatabase
            .lock().await
            .author();
        let r = c_author
            .lock().await
            .insert_one(author).await;
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

    pub async fn verify_author(&self, author_name: String, plain_password: String) -> Option<Author> {
        if let Some(author) = self.find_author_by_name(author_name.clone()).await {
            if author.name == author_name && Self::verify_password(&plain_password, &author.password_hash) {
                return Some(author);
            }
        }
        None
    }

}

fn configured_name(configured: &str, default_name: &str) -> String {
    let configured = configured.trim();
    if configured.is_empty() {
        default_name.to_string()
    } else {
        configured.to_string()
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
    use crate::author_manager::{configured_name, optional_secret, AuthorManager};

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
    fn configured_name_uses_configured_name() {
        assert_eq!(
            configured_name("ConfiguredAdmin", "Admin"),
            "ConfiguredAdmin",
        );
    }

    #[test]
    fn configured_name_falls_back_to_default_when_configured_name_is_empty() {
        assert_eq!(
            configured_name(" ", "Admin"),
            "Admin",
        );
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
