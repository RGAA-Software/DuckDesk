use std::sync::Arc;

use gr_base::log_util;
use service_core::config::{ServiceConfig, DEFAULT_LISTEN_PORT, SERVICE_LOG_FILE};
use service_core::windows_util::{default_service_data_root, default_service_log_root};
use tokio::sync::Mutex;
use tracing::info;

use crate::service_host::ServiceRuntime;
use crate::windows_actions::WindowsActions;
use crate::windows_process::WindowsProcessManager;

pub async fn run(port: Option<u16>, console_mode: bool) -> Result<(), String> {
    let actual_port = port.unwrap_or(DEFAULT_LISTEN_PORT);
    let data_root = default_service_data_root();
    let log_root = default_service_log_root();
    let config = ServiceConfig::new(actual_port, data_root, log_root.clone());
    let _guard = log_util::init_log(
        log_root.to_string_lossy().to_string(),
        SERVICE_LOG_FILE.to_string(),
    );
    info!(
        "GammaRayService starting, port={}, console_mode={}, data_root={}, log_root={}",
        actual_port,
        console_mode,
        config.data_root.display(),
        config.log_root.display()
    );

    if console_mode {
        let runtime = Arc::new(Mutex::new(ServiceRuntime::new(
            config.clone(),
            Arc::new(WindowsProcessManager::new()),
            Arc::new(WindowsActions::new()),
        )));
        info!("running service stack in console mode (ws + cms + monitor)");
        // Same task set as the Windows service entry: WS for Panel, CMS client for scheduling.
        crate::service_host::run_service(runtime, None).await?;
        return Ok(());
    }

    info!("running as windows service");
    crate::service_windows::dispatch_service(actual_port)
}
