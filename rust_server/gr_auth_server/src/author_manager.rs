use futures_util::StreamExt;
use mongodb::bson::doc;
use mongodb::Cursor;
use gr_base::hash_util;
use gr_base::hash_util::HashAlgo;
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
        self.insert_author(Author {
            name,
            password: self.gen_password(plain_password),
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

    pub fn gen_token(&self, plain_password: String) -> String {
        hash_util::compute_hash(HashAlgo::SHA256, plain_password.as_bytes())
    }

    fn gen_password_by_token(&self, token: String) -> String {
        hash_util::compute_hash(HashAlgo::MD5, (token + "ce111670ed4146f0a724709846e0965b@%!").as_bytes())
    }

    pub fn gen_password(&self, plain_password: String) -> String {
        let token = self.gen_token(plain_password);
        self.gen_password_by_token(token)
    }

    pub async fn verify_author(&self, author_name: String, author_token: String) -> Option<Author> {
        if let Some(author) = self.find_author_by_name(author_name.clone()).await {
            if author.name == author_name && author.password == self.gen_password_by_token(author_token) {
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
    fn gen_token_is_stable_for_same_input() {
        let auth = AuthorManager {};
        assert_eq!(
            auth.gen_token("password".to_string()),
            auth.gen_token("password".to_string()),
        );
    }

    #[test]
    fn gen_password_is_stable_for_same_input() {
        let auth = AuthorManager {};
        assert_eq!(
            auth.gen_password("password".to_string()),
            auth.gen_password("password".to_string()),
        );
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
