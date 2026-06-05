mod sys_info_mgr;
mod sys_panel_client;

use crate::sys_info_mgr::SysInfoManager;
use std::sync::Arc;
use clap::Parser as ClapParser;
use clap_derive::Parser;
use tokio::sync::Mutex;
use base::log_util;
use crate::sys_panel_client::SysPanelClient;

lazy_static::lazy_static! {
    pub static ref gSysInfoMgr: Arc<Mutex<SysInfoManager>> = Arc::new(Mutex::new(SysInfoManager::new()));
    pub static ref gSysPanelClient: Arc<Mutex<SysPanelClient>> = Arc::new(Mutex::new(SysPanelClient::new()));
}

#[derive(Parser)]
#[command(name = "myapp", version, about, long_about = None)]
struct Cli {
    #[arg(long)]
    print: Option<bool>,

    #[arg(short, long)]
    duration: Option<i32>,

    #[arg(short, long)]
    port: Option<i32>,

}

#[tokio::main]
async fn main() {
    let args = Cli::parse();
    let port = args.port.unwrap_or(20369);

    let _guard = log_util::init_log("logs/".to_string(), "gr_sys_info".to_string());

    gSysPanelClient
        .lock().await
        .duration = args.duration.unwrap_or(1);

    tokio::spawn(async move {
        gSysPanelClient
            .lock().await
            .connect(format!("ws://127.0.0.1:{}/sys/info", port)).await;
    });

    loop {
        if args.print.unwrap_or(false) {
            let sys_info = gSysInfoMgr
                .lock().await
                .load_system_info();
            tracing::info!("info: {:#?}", sys_info);
        }
        tokio::time::sleep(tokio::time::Duration::from_secs(5)).await;
    }

}
