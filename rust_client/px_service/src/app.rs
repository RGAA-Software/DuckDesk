use std::sync::Arc;

use px_base::log_util;
use service_core::config::{ServiceConfig, SERVICE_LOG_FILE};
use service_core::windows_util::{default_service_data_root, default_service_log_root};
use tokio::sync::Mutex;
use tracing::info;

use crate::service_host::ServiceRuntime;
use crate::virtual_display_manager::VirtualDisplayManager;
use crate::windows_actions::WindowsActions;
use crate::windows_process::WindowsProcessManager;
use crate::VirtualDisplayCliOperation;
use crate::VirtualDisplaySessionWorkerOperation;

pub fn run_virtual_display_command(
    operation: VirtualDisplayCliOperation,
    width: u32,
    height: u32,
    refresh_hz: u32,
) -> Result<(), String> {
    let exe_dir = std::env::current_exe()
        .map_err(|err| err.to_string())?
        .parent()
        .ok_or("px_service executable has no parent directory")?
        .to_path_buf();
    let manager = VirtualDisplayManager::new_windows_session_aware(
        default_service_data_root(),
        exe_dir.join("parsec_vdd"),
        Arc::new(WindowsProcessManager::new()),
    )
    .map_err(|err| err.to_string())?;
    let result = match operation {
        VirtualDisplayCliOperation::Query => manager.query(),
        VirtualDisplayCliOperation::Create => manager.create(width, height, refresh_hz),
        VirtualDisplayCliOperation::RemoveLast => manager.remove_last(),
        VirtualDisplayCliOperation::ResetOwned => manager.reset_owned(),
    }
    .map_err(|err| err.to_string())?;
    println!(
        "{}",
        serde_json::to_string_pretty(&result).map_err(|err| err.to_string())?
    );
    Ok(())
}

pub fn run_virtual_display_session_worker(
    operation: VirtualDisplaySessionWorkerOperation,
    width: u32,
    height: u32,
    refresh_hz: u32,
    result_file: &std::path::Path,
    nonce: &str,
) -> Result<(), String> {
    let exe_dir = std::env::current_exe()
        .map_err(|err| err.to_string())?
        .parent()
        .ok_or("px_service executable has no parent directory")?
        .to_path_buf();
    let operation = match operation {
        VirtualDisplaySessionWorkerOperation::Query => {
            crate::virtual_display_session::SessionWorkerOperation::Query
        }
        VirtualDisplaySessionWorkerOperation::Create => {
            crate::virtual_display_session::SessionWorkerOperation::Create
        }
        VirtualDisplaySessionWorkerOperation::RemoveLast => {
            crate::virtual_display_session::SessionWorkerOperation::RemoveLast
        }
    };
    crate::virtual_display_session::run_session_worker(
        operation,
        width,
        height,
        refresh_hz,
        exe_dir.join("parsec_vdd"),
        result_file,
        nonce,
    )
}

pub async fn run(port: Option<u16>, console_mode: bool) -> Result<(), String> {
    let config_path = std::env::current_exe().map_err(|err| err.to_string())?.with_file_name("px_service.toml");
    let mut node = service_core::node_config::NodeConfig::load(&config_path)?;
    if let Some(port) = port { node.network.listen_port = port; }
    node.validate()?;
    let actual_port = node.network.listen_port;
    let data_root = default_service_data_root();
    let log_root = default_service_log_root();
    let mut config = ServiceConfig::new(actual_port, data_root, log_root.clone());
    config.listen_host = node.network.listen_host.clone();
    config.node = node;
    let _guard = log_util::init_log(
        log_root.to_string_lossy().to_string(),
        SERVICE_LOG_FILE.to_string(),
    );
    info!(
        "px_service starting, port={}, console_mode={}, data_root={}, log_root={}",
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
        info!("running service stack in console mode (ws + console + monitor)");
        // Same task set as the Windows service entry: WS for Panel, Console client for scheduling.
        crate::service_host::run_service(runtime, None).await?;
        return Ok(());
    }

    info!("running as windows service");
    crate::service_windows::dispatch_service(config)
}
