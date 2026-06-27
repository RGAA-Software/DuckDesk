mod update_api_error;
mod update_context;
mod update_database;
mod update_handle;
mod update_http_utils;
mod update_info;
mod update_info_manager;
mod update_keys;
mod update_server;

use crate::update_database::UpdateDatabase;
use crate::update_info_manager::UpdateInfoManager;
use gr_base::log_util;
use std::sync::Arc;
use tokio::sync::Mutex;
use update_context::UpdateContext;
use update_server::UpdateServer;

lazy_static::lazy_static! {
    pub static ref gUpdateDatabase: Arc<Mutex<UpdateDatabase>> = UpdateDatabase::new();
    pub static ref gUpdateInfoManager: Arc<Mutex<UpdateInfoManager>> = UpdateInfoManager::new();
}

#[tokio::main]
async fn main() {
    let _ = gr_base::create_dir_if_not_exists("./static");

    // log
    let _guard = log_util::init_log("logs/gr_updater/".to_string(), "log_updater".to_string());

    // upload file save dir
    let _ = gr_base::create_dir_all_if_not_exists("./uploads/update_info");

    // database
    if !gUpdateDatabase.lock().await.init().await {
        tracing::error!("failed to initialize database");
        return;
    }

    // context
    let context = Arc::new(Mutex::new(UpdateContext::new()));

    // tls
    rustls::crypto::ring::default_provider()
        .install_default()
        .expect("Failed to install rustls crypto provider");

    UpdateServer::new().start(context).await;
}
