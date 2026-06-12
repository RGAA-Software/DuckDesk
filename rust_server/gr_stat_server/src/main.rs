mod stat_server;
mod stat_database;
mod stat_context;
mod stat_api_error;
mod stat_http_utils;
mod stat_api_keys;
mod auth;
mod filter;
mod using;

use crate::stat_database::StatDatabase;
use crate::stat_server::StatServer;
use gr_base::log_util;
use clap::Parser as ClapParser;
use clap_derive::Parser;
use std::sync::Arc;
use tokio::sync::Mutex;
use crate::stat_context::StatContext;
use crate::auth::auth_stat_manager::StatAuthManager;
use crate::using::stat_using_manager::StatUsingManager;

lazy_static::lazy_static! {
    pub static ref gStatDatabase: Arc<Mutex<StatDatabase >> = StatDatabase::new();
    pub static ref gStatAuthManager: Arc<StatAuthManager> = StatAuthManager::new();
    pub static ref gStatUsingManager: Arc<StatUsingManager> = Arc::new(StatUsingManager::new());
}

#[derive(Parser)]
#[command(name = "myapp", version, about, long_about = None)]
struct Cli {

    #[arg(short, long)]
    port: Option<u16>,

}

#[tokio::main]
async fn main() {
    let args = Cli::parse();
    let port = args.port.unwrap_or(30300);

    let _ = gr_base::create_dir_if_not_exists("./static");

    // log
    let _guard = log_util::init_log("logs/gr_stat_server/".to_string(), "log_stat".to_string());

    // database
    if !gStatDatabase
        .lock().await
        .init().await {
        tracing::error!("failed to initialize database");
        return;
    }

    // context
    let context = Arc::new(Mutex::new(StatContext::new()));

    // tls
    rustls::crypto::ring::default_provider()
        .install_default()
        .expect("Failed to install rustls crypto provider");

    StatServer::new().start(context, port).await;
}
