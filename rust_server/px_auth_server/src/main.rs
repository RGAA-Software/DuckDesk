mod app_credential;
mod author;
mod author_api_error;
mod author_appkey_generator;
mod author_claims;
mod author_customer;
mod author_database;
mod author_handler;
mod author_http_util;
mod author_keys;
mod author_license_keys;
mod author_manager;
mod author_resp;
mod author_server;
mod author_settings;
mod authorization_handler;
mod authorization_manager;
mod filter;

use crate::author_claims::init_jwt_secret;
use crate::author_database::AuthorDatabase;
use crate::author_license_keys::init_license_signer;
use crate::author_manager::AuthorManager;
use crate::author_server::AuthorServer;
use crate::author_settings::AuthorSettings;
use crate::authorization_manager::AuthorizationManager;
use px_auth_mgr::auth_license::LicenseSigner;
use px_base::log_util;
use rustls::crypto::CryptoProvider;
use rustls::crypto::ring::default_provider;
use std::sync::Arc;
use tokio::sync::Mutex;

lazy_static::lazy_static! {
    pub static ref gAuthorSettings: Arc<Mutex<AuthorSettings >> = Arc::new(Mutex::new(AuthorSettings::new()));
    pub static ref gAuthorDatabase: Arc<Mutex<AuthorDatabase >> = Arc::new(Mutex::new(AuthorDatabase::new()));
    pub static ref gAuthorManager: Arc<AuthorManager> = Arc::new(AuthorManager::new());
    pub static ref gAuthorizationManager: Arc<AuthorizationManager> = Arc::new(AuthorizationManager::new());
    pub static ref gLicenseSigner: Arc<Mutex<Option<LicenseSigner>>> = Arc::new(Mutex::new(None));
}

#[tokio::main]
async fn main() {
    // log
    let _guard = log_util::init_log("logs/px_auth_server/".to_string(), "log_author".to_string());

    let _ = px_base::create_dir_if_not_exists("./web_auth");

    //
    CryptoProvider::install_default(default_provider())
        .expect("Failed to initialize CryptoProvider");

    // settings
    if !AuthorSettings::load_settings().await {
        tracing::error!("could not initialize settings");
        return;
    }

    let jwt_secret = gAuthorSettings.lock().await.bootstrap.jwt_secret.clone();
    if !init_jwt_secret(jwt_secret) {
        tracing::error!("could not initialize JWT secret");
        return;
    }

    // database
    let db_path = gAuthorSettings.lock().await.db_path.clone();
    if !gAuthorDatabase.lock().await.init(db_path).await {
        tracing::error!("could not initialize database");
        return;
    }

    // px_auth_server manager
    if !gAuthorManager.init().await {
        tracing::error!("could not initialize author_manager");
        return;
    }

    // authorization manager
    if !gAuthorizationManager.init().await {
        tracing::error!("could not initialize authorization_manager");
        return;
    }

    // license signer
    match init_license_signer() {
        Ok(signer) => {
            *gLicenseSigner.lock().await = Some(signer);
        }
        Err(e) => {
            tracing::error!("could not initialize license signer: {}", e);
            return;
        }
    }

    // server
    let port = gAuthorSettings.lock().await.server_port;
    let server = AuthorServer::new(port);
    server.start().await;
}
