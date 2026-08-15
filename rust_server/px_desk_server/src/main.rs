mod consult;
mod filter;
mod issue;
mod off_admin_handle;
mod off_api_error;
mod off_api_keys;
mod off_context;
mod off_database;
mod off_http_utils;
mod off_server;
mod off_settings;
mod version;

use crate::consult::off_consult_manager::OffConsultManager;
use crate::issue::off_issue_manager::OffIssueManager;
use crate::off_context::OffContext;
use crate::off_database::OffDatabase;
use crate::off_server::OffServer;
use crate::off_settings::OffSettings;
use crate::version::off_version_manager::OffVersionManager;
use clap::Parser as ClapParser;
use clap_derive::Parser;
use px_base::log_util;
use std::sync::Arc;
use tokio::sync::Mutex;

lazy_static::lazy_static! {
    pub static ref gOffDatabase: Arc<Mutex<OffDatabase>> = OffDatabase::new();
    pub static ref gOffConsultManager: Arc<Mutex<OffConsultManager>> = OffConsultManager::new();
    pub static ref gOffIssueManager: Arc<Mutex<OffIssueManager>> = OffIssueManager::new();
    pub static ref gOffVersionManager: Arc<Mutex<OffVersionManager>> = OffVersionManager::new();
    pub static ref gOffSettings: OffSettings = OffSettings::load_or_create();
}

#[derive(Parser)]
#[command(name = "myapp", version, about, long_about = None)]
struct Cli {
    #[arg(short, long)]
    port: Option<i32>,
}

#[tokio::main]
async fn main() {
    let args = Cli::parse();
    let _port = args.port.unwrap_or(20369);

    let _ = px_base::create_dir_if_not_exists("./static");

    // log
    let _guard = log_util::init_log("logs/px_desk_server/".to_string(), "log_off".to_string());

    // settings（exe 旁 px_desk.toml，含管理密码；缺失则自动生成）
    lazy_static::initialize(&gOffSettings);

    // database
    if !gOffDatabase.lock().await.init().await {
        tracing::error!("failed to initialize database");
        return;
    }

    // context
    let context = Arc::new(Mutex::new(OffContext::new()));

    // tls
    rustls::crypto::ring::default_provider()
        .install_default()
        .expect("Failed to install rustls crypto provider");

    OffServer::new().start(context).await;
}
