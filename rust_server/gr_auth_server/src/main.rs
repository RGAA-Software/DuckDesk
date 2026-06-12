mod author_settings;
mod author_context;
mod author_server;
mod author_database;
mod filter;
mod author_api_error;
mod author_manager;
mod author;
mod author_handler;
mod authorization_manager;
mod author_appkey_generator;
mod authorization_handler;
mod author_http_util;
mod author_customer;
mod author_keys;
mod author_claims;
mod author_resp;

use std::sync::Arc;
use rustls::crypto::CryptoProvider;
use rustls::crypto::ring::default_provider;
use tokio::sync::Mutex;
use gr_base::log_util;
use crate::author_context::AuthorContext;
use crate::author_database::AuthorDatabase;
use crate::author_manager::AuthorManager;
use crate::author_server::AuthorServer;
use crate::author_settings::AuthorSettings;
use crate::authorization_manager::AuthorizationManager;

lazy_static::lazy_static! {
    pub static ref gAuthorSettings: Arc<Mutex<AuthorSettings >> = Arc::new(Mutex::new(AuthorSettings::new()));
    pub static ref gAuthorContext: Arc<Mutex<AuthorContext >> = Arc::new(Mutex::new(AuthorContext::new()));
    pub static ref gAuthorDatabase: Arc<Mutex<AuthorDatabase >> = Arc::new(Mutex::new(AuthorDatabase::new()));
    pub static ref gAuthorManager: Arc<AuthorManager> = Arc::new(AuthorManager::new());
    pub static ref gAuthorizationManager: Arc<AuthorizationManager> = Arc::new(AuthorizationManager::new());
}

#[tokio::main]
async fn main() {
    // log
    let _guard = log_util::init_log("logs/gr_auth_server/".to_string(), "log_author".to_string());

    let _ = gr_base::create_dir_if_not_exists("./web_auth");

    //
    CryptoProvider::install_default(default_provider())
        .expect("Failed to initialize CryptoProvider");

    // settings
    AuthorSettings::load_settings().await;
    
    // database
    let db_path = gAuthorSettings
        .lock().await
        .db_path.clone();
    if !gAuthorDatabase
        .lock().await
        .init(db_path).await {
        tracing::error!("could not initialize database");
        return;
    }

    // context
    let context = Arc::new(Mutex::new(AuthorContext::new()));

    // gr_auth_server manager
    if !gAuthorManager
        .init().await {
        tracing::error!("could not initialize author_manager");
        return;
    }

    // authorization manager
    if !gAuthorizationManager
        .init().await {
        tracing::error!("could not initialize authorization_manager");
        return;
    }

    // server
    let port = gAuthorSettings
        .lock().await
        .server_port;
    let server = AuthorServer::new(port, context);
    server.start().await;
}
