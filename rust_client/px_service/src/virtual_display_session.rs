use rand::RngCore;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

use crate::usbmmidd::{MonitorSnapshot, UsbMmIddBackend, UsbMmIddError, WindowsUsbMmIddBackend};
use crate::windows_process::ProcessManager;

const QUERY_WORKER_TIMEOUT: Duration = Duration::from_secs(8);
const MUTATION_WORKER_TIMEOUT: Duration = Duration::from_secs(35);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SessionWorkerOperation {
    Query,
    Create,
    RemoveLast,
}

impl SessionWorkerOperation {
    fn cli_name(self) -> &'static str {
        match self {
            Self::Query => "query",
            Self::Create => "create",
            Self::RemoveLast => "remove-last",
        }
    }

    fn timeout(self) -> Duration {
        match self {
            Self::Query => QUERY_WORKER_TIMEOUT,
            Self::Create | Self::RemoveLast => MUTATION_WORKER_TIMEOUT,
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
struct SessionWorkerResponse {
    nonce: String,
    ok: bool,
    monitors: Vec<MonitorSnapshot>,
    monitor: Option<MonitorSnapshot>,
    error_code: String,
    error_message: String,
}

impl SessionWorkerResponse {
    fn success(
        nonce: &str,
        monitors: Vec<MonitorSnapshot>,
        monitor: Option<MonitorSnapshot>,
    ) -> Self {
        Self {
            nonce: nonce.to_string(),
            ok: true,
            monitors,
            monitor,
            error_code: String::new(),
            error_message: String::new(),
        }
    }

    fn failure(nonce: &str, error: UsbMmIddError) -> Self {
        Self {
            nonce: nonce.to_string(),
            ok: false,
            monitors: Vec::new(),
            monitor: None,
            error_code: error.code.to_string(),
            error_message: error.message,
        }
    }

    fn into_result(self) -> Result<Self, UsbMmIddError> {
        if self.ok {
            Ok(self)
        } else {
            Err(UsbMmIddError::new(
                "SESSION_WORKER_OPERATION_FAILED",
                format!("{}: {}", self.error_code, self.error_message),
            ))
        }
    }
}

pub fn run_session_worker(
    operation: SessionWorkerOperation,
    width: u32,
    height: u32,
    refresh_hz: u32,
    driver_dir: PathBuf,
    result_file: &Path,
    nonce: &str,
) -> Result<(), String> {
    let backend = WindowsUsbMmIddBackend::new(driver_dir);
    let response = match operation {
        SessionWorkerOperation::Query => backend
            .enumerate_monitors()
            .map(|monitors| SessionWorkerResponse::success(nonce, monitors, None)),
        SessionWorkerOperation::Create => backend
            .add_monitor(width, height, refresh_hz)
            .map(|monitor| SessionWorkerResponse::success(nonce, Vec::new(), Some(monitor))),
        SessionWorkerOperation::RemoveLast => backend
            .remove_last_monitor()
            .map(|_| SessionWorkerResponse::success(nonce, Vec::new(), None)),
    }
    .unwrap_or_else(|error| SessionWorkerResponse::failure(nonce, error));

    if let Some(parent) = result_file.parent() {
        fs::create_dir_all(parent).map_err(|err| err.to_string())?;
    }
    let temp = result_file.with_extension("json.tmp");
    let payload = serde_json::to_vec(&response).map_err(|err| err.to_string())?;
    fs::write(&temp, payload).map_err(|err| err.to_string())?;
    fs::rename(&temp, result_file).map_err(|err| err.to_string())
}

pub struct SessionUsbMmIddBackend {
    direct: WindowsUsbMmIddBackend,
    worker_dir: PathBuf,
    process_manager: Arc<dyn ProcessManager>,
    executable: PathBuf,
}

impl SessionUsbMmIddBackend {
    pub fn new(
        driver_dir: PathBuf,
        worker_dir: PathBuf,
        process_manager: Arc<dyn ProcessManager>,
    ) -> Result<Self, UsbMmIddError> {
        let executable = std::env::current_exe().map_err(|err| {
            UsbMmIddError::new(
                "SESSION_WORKER_EXE_NOT_FOUND",
                format!("cannot resolve px_service executable: {err}"),
            )
        })?;
        Ok(Self {
            direct: WindowsUsbMmIddBackend::new(driver_dir),
            worker_dir,
            process_manager,
            executable,
        })
    }

    fn execute(
        &self,
        operation: SessionWorkerOperation,
        width: u32,
        height: u32,
        refresh_hz: u32,
    ) -> Result<SessionWorkerResponse, UsbMmIddError> {
        fs::create_dir_all(&self.worker_dir).map_err(|err| {
            UsbMmIddError::new(
                "SESSION_WORKER_DIR_FAILED",
                format!("cannot create {}: {err}", self.worker_dir.display()),
            )
        })?;
        let mut random = [0_u8; 16];
        rand::rng().fill_bytes(&mut random);
        let nonce = random
            .iter()
            .map(|byte| format!("{byte:02x}"))
            .collect::<String>();
        let result_file = self
            .worker_dir
            .join(format!("virtual_display_worker_{nonce}.json"));
        let executable = self.executable.to_string_lossy().to_string();
        let work_dir = self
            .executable
            .parent()
            .unwrap_or_else(|| Path::new("."))
            .to_string_lossy()
            .to_string();
        let args = vec![
            "--virtual-display-session-worker".to_string(),
            operation.cli_name().to_string(),
            "--virtual-display-width".to_string(),
            width.to_string(),
            "--virtual-display-height".to_string(),
            height.to_string(),
            "--virtual-display-refresh-hz".to_string(),
            refresh_hz.to_string(),
            "--virtual-display-worker-result".to_string(),
            result_file.to_string_lossy().to_string(),
            "--virtual-display-worker-nonce".to_string(),
            nonce.clone(),
        ];
        self.process_manager
            // Display topology is owned by the logged-on desktop. Running the
            // worker as SYSTEM merely assigned to that session can issue the
            // driver IOCTL but cannot reliably observe or configure its
            // DISPLAYn output. Keep this boundary on the real WTS user token.
            .start_process_as_session_user(&work_dir, &executable, &args)
            .map_err(|err| UsbMmIddError::new("SESSION_WORKER_LAUNCH_FAILED", err))?;

        let worker_timeout = operation.timeout();
        let deadline = Instant::now() + worker_timeout;
        while Instant::now() < deadline {
            if result_file.exists() {
                let payload = fs::read(&result_file).map_err(|err| {
                    UsbMmIddError::new(
                        "SESSION_WORKER_RESULT_READ_FAILED",
                        format!("cannot read {}: {err}", result_file.display()),
                    )
                })?;
                let _ = fs::remove_file(&result_file);
                let response: SessionWorkerResponse =
                    serde_json::from_slice(&payload).map_err(|err| {
                        UsbMmIddError::new("SESSION_WORKER_RESULT_INVALID", err.to_string())
                    })?;
                if response.nonce != nonce {
                    return Err(UsbMmIddError::new(
                        "SESSION_WORKER_NONCE_MISMATCH",
                        "session worker response nonce did not match",
                    ));
                }
                return response.into_result();
            }
            thread::sleep(Duration::from_millis(100));
        }
        Err(UsbMmIddError::new(
            "SESSION_WORKER_TIMEOUT",
            format!(
                "interactive virtual display worker did not answer within {:?}",
                worker_timeout
            ),
        ))
    }
}

impl UsbMmIddBackend for SessionUsbMmIddBackend {
    fn verify_package(&self) -> Result<(), UsbMmIddError> {
        self.direct.verify_package()
    }

    fn driver_installed(&self) -> bool {
        self.direct.driver_installed()
    }

    fn install_driver(&self) -> Result<(), UsbMmIddError> {
        self.direct.install_driver()
    }

    fn enumerate_monitors(&self) -> Result<Vec<MonitorSnapshot>, UsbMmIddError> {
        self.execute(SessionWorkerOperation::Query, 0, 0, 0)
            .map(|response| response.monitors)
    }

    fn add_monitor(
        &self,
        width: u32,
        height: u32,
        refresh_hz: u32,
    ) -> Result<MonitorSnapshot, UsbMmIddError> {
        // Driver installation is system-wide and remains Service-owned.
        self.direct.install_driver()?;
        self.execute(SessionWorkerOperation::Create, width, height, refresh_hz)?
            .monitor
            .ok_or_else(|| {
                UsbMmIddError::new(
                    "SESSION_WORKER_RESULT_INVALID",
                    "create worker returned no monitor",
                )
            })
    }

    fn remove_last_monitor(&self) -> Result<(), UsbMmIddError> {
        self.execute(SessionWorkerOperation::RemoveLast, 0, 0, 0)
            .map(|_| ())
    }
}

#[cfg(test)]
fn identify_created_monitor(
    before: &[MonitorSnapshot],
    after: &[MonitorSnapshot],
) -> Result<MonitorSnapshot, UsbMmIddError> {
    let before_names: std::collections::HashSet<_> = before
        .iter()
        .map(|monitor| monitor.device_name.as_str())
        .collect();
    after
        .iter()
        .find(|monitor| !before_names.contains(monitor.device_name.as_str()))
        .cloned()
        .ok_or_else(|| {
            UsbMmIddError::new(
                "MONITOR_ENUMERATION_FAILED",
                "USBMMIDD count increased but no new monitor could be identified",
            )
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn worker_operation_names_match_hidden_cli() {
        assert_eq!(SessionWorkerOperation::Query.cli_name(), "query");
        assert_eq!(SessionWorkerOperation::Create.cli_name(), "create");
        assert_eq!(SessionWorkerOperation::RemoveLast.cli_name(), "remove-last");
    }

    #[test]
    fn query_timeout_cannot_block_service_startup_as_long_as_mutations() {
        assert_eq!(
            SessionWorkerOperation::Query.timeout(),
            Duration::from_secs(8)
        );
        assert_eq!(
            SessionWorkerOperation::Create.timeout(),
            Duration::from_secs(35)
        );
        assert_eq!(
            SessionWorkerOperation::RemoveLast.timeout(),
            Duration::from_secs(35)
        );
    }

    #[test]
    fn worker_response_rejects_failure() {
        let response =
            SessionWorkerResponse::failure("nonce", UsbMmIddError::new("TEST", "failed"));
        assert!(response.into_result().is_err());
    }

    #[test]
    fn identifies_the_new_monitor_by_device_name() {
        let before = vec![MonitorSnapshot {
            device_name: r"\\.\DISPLAY4".to_string(),
            width: 1920,
            height: 1080,
            refresh_hz: 60,
        }];
        let added = MonitorSnapshot {
            device_name: r"\\.\DISPLAY5".to_string(),
            width: 1920,
            height: 1080,
            refresh_hz: 60,
        };
        assert_eq!(
            identify_created_monitor(&before, &[before[0].clone(), added.clone()]).unwrap(),
            added
        );
    }

    #[test]
    fn rejects_a_topology_change_without_a_new_monitor_identity() {
        let monitor = MonitorSnapshot {
            device_name: r"\\.\DISPLAY4".to_string(),
            width: 1920,
            height: 1080,
            refresh_hz: 60,
        };
        let error = identify_created_monitor(&[monitor.clone()], &[monitor]).unwrap_err();
        assert_eq!(error.code, "MONITOR_ENUMERATION_FAILED");
    }
}
