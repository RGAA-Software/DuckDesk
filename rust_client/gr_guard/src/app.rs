use std::fs;
use std::path::PathBuf;
use std::sync::Arc;

use tracing::info;
use windows::Win32::UI::WindowsAndMessaging::{DispatchMessageW, GetMessageW, MSG, TranslateMessage};

use crate::config::{guard_data_root, guard_log_file, guard_log_root, panel_ws_url, GUARD_LOCK_NAME, GUARD_TASK_AUTHOR};
use crate::hidden_window::HiddenWindow;
use crate::logging::init_guard_logging;
use crate::panel_client::{PanelClient, PanelClientConfig};
use crate::process_lister::ToolhelpProcessLister;
use crate::process_spawn::WindowsProcessSpawner;
use crate::runtime::GuardRuntime;
use crate::single_instance::SingleInstanceGuard;
use crate::task_scheduler::{ensure_guard_logon_task, SchtasksBackend};

pub struct GuardApp {
    _log_guard: gr_base::log_util::LogGuard,
    _instance_guard: SingleInstanceGuard,
    _window: HiddenWindow,
    runtime: GuardRuntime,
    panel_client: PanelClient,
}

impl GuardApp {
    pub fn bootstrap() -> Result<Self, String> {
        let exe_path = std::env::current_exe().map_err(|err| format!("current_exe failed: {err}"))?;
        let exe_dir = exe_path
            .parent()
            .map(PathBuf::from)
            .ok_or_else(|| format!("current_exe has no parent: {}", exe_path.display()))?;

        fs::create_dir_all(guard_data_root()).map_err(|err| format!("create guard data dir failed: {err}"))?;
        fs::create_dir_all(guard_log_root()).map_err(|err| format!("create guard log dir failed: {err}"))?;

        let log_guard = init_guard_logging();
        info!(
            "GammaRayGuard starting, exe_path={}, data_root={}, log_file={}",
            exe_path.display(),
            guard_data_root().display(),
            guard_log_file().display()
        );

        let instance_guard = SingleInstanceGuard::acquire(GUARD_LOCK_NAME)?;
        ensure_guard_logon_task(&SchtasksBackend, &exe_path, GUARD_TASK_AUTHOR)?;
        let window = HiddenWindow::create()?;
        info!("guard hidden window created, hwnd={:?}", window.hwnd());

        let runtime = GuardRuntime::start(
            exe_dir,
            Arc::new(ToolhelpProcessLister),
            Arc::new(WindowsProcessSpawner),
        );
        let panel_client = PanelClient::start(PanelClientConfig::new(panel_ws_url()));

        Ok(Self {
            _log_guard: log_guard,
            _instance_guard: instance_guard,
            _window: window,
            runtime,
            panel_client,
        })
    }

    pub fn run_message_loop(&mut self) -> Result<i32, String> {
        let mut message = MSG::default();
        loop {
            let status = unsafe { GetMessageW(&mut message, None, 0, 0) }.0;
            match status {
                -1 => return Err("GetMessageW failed".to_string()),
                0 => return Ok(message.wParam.0 as i32),
                _ => unsafe {
                    let _ = TranslateMessage(&message);
                    DispatchMessageW(&message);
                },
            }
        }
    }
}

impl Drop for GuardApp {
    fn drop(&mut self) {
        self.panel_client.stop();
        self.runtime.stop();
    }
}
