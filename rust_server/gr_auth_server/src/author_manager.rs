use futures_util::StreamExt;
use mongodb::bson::doc;
use mongodb::Cursor;
use gr_auth_mgr::authorization::Authorization;
use gr_base::hash_util;
use gr_base::hash_util::HashAlgo;
use crate::author::Author;
use crate::authorization_manager::AuthorizationError;
use crate::gAuthorDatabase;

pub const AUTHOR_ADMIN: &str = "Admin";
pub const AUTHOR_ADMIN_PASSWORD: &str = "Admin@321%!";
pub const AUTHOR_VISITOR: &str = "Visitor";
pub const AUTHOR_VISITOR_PASSWORD: &str = "Visitor@321%!";
pub const AUTHOR_PERM_ALL: &str = "perm_all";
pub const AUTHOR_PERM_VISITOR: &str = "perm_visitor";

pub struct AuthorManager {

}

impl AuthorManager {

    pub fn new() -> Self {
        Self {}
    }

    pub async fn init(&self) -> bool {
        if !self.has_admin().await {
            if !self.insert_admin().await {
                return false;
            }
        }

        if !self.has_visitor().await {
            if !self.insert_visitor().await {
                return false;
            }
        }
        true
    }

    async fn has_admin(&self) -> bool {
        self
            .find_author_by_name(AUTHOR_ADMIN.to_string())
            .await.is_some()
    }

    async fn insert_admin(&self) -> bool {
        self.insert_author(Author {
            name: AUTHOR_ADMIN.to_string(),
            password: self.gen_password(AUTHOR_ADMIN_PASSWORD.to_string()),
            permission: AUTHOR_PERM_ALL.to_string(),
        }).await
    }

    async fn has_visitor(&self) -> bool {
        self
            .find_author_by_name(AUTHOR_VISITOR.to_string())
            .await.is_some()
    }

    async fn insert_visitor(&self) -> bool {
        self.insert_author(Author {
            name: AUTHOR_VISITOR.to_string(),
            password: self.gen_password(AUTHOR_VISITOR_PASSWORD.to_string()),
            permission: AUTHOR_PERM_VISITOR.to_string(),
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

#[cfg(test)]
mod tests {
    use crate::author_manager::{AuthorManager, AUTHOR_ADMIN_PASSWORD, AUTHOR_VISITOR_PASSWORD};

    #[test]
    fn test_gen_visitor_token() {
        let auth = AuthorManager {};
        let token = auth.gen_token(AUTHOR_VISITOR_PASSWORD.to_string());
        println!("the password: {}", token);
    }

    #[test]
    fn test_gen_admin_password() {
        let auth = AuthorManager {};
        let token = auth.gen_token(AUTHOR_ADMIN_PASSWORD.to_string());
        println!("the password: {}", token);
    }
}