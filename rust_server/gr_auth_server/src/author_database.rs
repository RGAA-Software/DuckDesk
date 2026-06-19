use crate::author::Author;
use gr_auth_mgr::authorization::Authorization;
use mongodb::{Client, Collection};
use std::sync::Arc;
use tokio::sync::Mutex;
use crate::author_customer::Customer;

pub struct AuthorDatabase {
    c_author: Option<Arc<Mutex<Collection<Author>>>>,
    c_authorization: Option<Arc<Mutex<Collection<Authorization>>>>,
    c_customer: Option<Arc<Mutex<Collection<Customer>>>>
}

impl AuthorDatabase {
    pub fn new() -> Self {
        AuthorDatabase {
            c_author: None,
            c_authorization: None,
            c_customer: None
        }
    }

    pub async fn init(&mut self, db_path: String) -> bool {
        let uri = db_path;
        if uri.trim().is_empty() {
            tracing::error!("MongoDB connection string is empty");
            return false;
        }
        // Create a new client and connect to the server
        let client = Client::with_uri_str(uri).await;
        if let Err(e) = client {
            tracing::error!("error connecting to MongoDB: {}", e);
            return false;
        }
        
        let client = client.unwrap();
        // Get a handle on the movies collection
        let database = client.database("db_gr_auth_server");

        // gr_auth_server
        let c_author: Collection<Author> = database.collection("c_author");
        self.c_author = Some(Arc::new(Mutex::new(c_author)));

        // authorization
        let c_authorization: Collection<Authorization> = database.collection("c_authorization");
        self.c_authorization = Some(Arc::new(Mutex::new(c_authorization)));

        // customer
        let c_customer: Collection<Customer> = database.collection("c_customer");
        self.c_customer = Some(Arc::new(Mutex::new(c_customer)));
        
        true
    }

    pub fn author(&self) -> Arc<Mutex<Collection<Author>>> {
        self.c_author.clone().unwrap()
    }

    pub fn authorization(&self) -> Arc<Mutex<Collection<Authorization>>> {
        self.c_authorization.clone().unwrap()
    }

    pub fn customer(&self) -> Arc<Mutex<Collection<Customer>>> {
        self.c_customer.clone().unwrap()
    }
    
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn init_rejects_empty_connection_string_before_mongodb_client() {
        let mut database = AuthorDatabase::new();

        assert!(!database.init(" ".to_string()).await);
    }
}
