pub mod monitor_dashboard;
pub mod monitor_app;
pub mod monitor_host_app;
pub mod monitor_model;
pub mod monitor_sender;
pub mod sys_info_mgr;
pub mod sys_panel_client;
pub mod tray;

use std::sync::Arc;
use tokio::sync::Mutex;

use sys_info_mgr::SysInfoManager;
use sys_panel_client::SysPanelClient;

lazy_static::lazy_static! {
    pub static ref gSysInfoMgr: Arc<Mutex<SysInfoManager>> = Arc::new(Mutex::new(SysInfoManager::new()));
    pub static ref gSysPanelClient: Arc<Mutex<SysPanelClient>> = Arc::new(Mutex::new(SysPanelClient::new()));
}
