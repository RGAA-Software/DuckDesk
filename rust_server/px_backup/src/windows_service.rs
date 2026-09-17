use px_backup::{BackupCancellation, BackupDaemon, BackupDaemonConfig, BackupDaemonError};
use std::{
    path::PathBuf,
    sync::{
        atomic::{AtomicBool, AtomicUsize, Ordering},
        Arc, Mutex, OnceLock,
    },
};
use windows::{
    core::{PCWSTR, PWSTR},
    Win32::{
        Foundation::{ERROR_SERVICE_SPECIFIC_ERROR, NO_ERROR},
        System::Services::{
            RegisterServiceCtrlHandlerExW, SetServiceStatus, StartServiceCtrlDispatcherW,
            SERVICE_ACCEPT_SHUTDOWN, SERVICE_ACCEPT_STOP, SERVICE_CONTROL_INTERROGATE,
            SERVICE_CONTROL_SHUTDOWN, SERVICE_CONTROL_STOP, SERVICE_RUNNING, SERVICE_START_PENDING,
            SERVICE_STATUS, SERVICE_STATUS_CURRENT_STATE, SERVICE_STATUS_HANDLE, SERVICE_STOPPED,
            SERVICE_STOP_PENDING, SERVICE_TABLE_ENTRYW, SERVICE_WIN32_OWN_PROCESS,
        },
    },
};

static CONFIG_PATH: OnceLock<PathBuf> = OnceLock::new();
static SERVICE_NAME: OnceLock<String> = OnceLock::new();
static SERVICE_STATUS_HANDLE_VALUE: AtomicUsize = AtomicUsize::new(0);
static STOPPING: OnceLock<Arc<AtomicBool>> = OnceLock::new();
static CANCELLATION: OnceLock<Mutex<Option<BackupCancellation>>> = OnceLock::new();

pub fn dispatch(config_path: PathBuf) -> Result<(), &'static str> {
    let config =
        BackupDaemonConfig::load_private(&config_path).map_err(|_| "configuration rejected")?;
    let deployment_text = config.deployment_id.simple().to_string();
    let service_name = format!("Pixels.Backup.{}", &deployment_text[..12]);
    CONFIG_PATH
        .set(config_path)
        .map_err(|_| "service configuration already initialized")?;
    SERVICE_NAME
        .set(service_name.clone())
        .map_err(|_| "service identity already initialized")?;
    let mut service_name_wide = service_name.encode_utf16().chain([0]).collect::<Vec<_>>();
    let mut service_table = [
        SERVICE_TABLE_ENTRYW {
            lpServiceName: PWSTR(service_name_wide.as_mut_ptr()),
            lpServiceProc: Some(service_main),
        },
        SERVICE_TABLE_ENTRYW {
            lpServiceName: PWSTR::null(),
            lpServiceProc: None,
        },
    ];
    unsafe { StartServiceCtrlDispatcherW(service_table.as_mut_ptr()) }
        .map_err(|_| "Windows service dispatcher rejected startup")
}

unsafe extern "system" fn service_control_handler(
    control_code: u32,
    _event_type: u32,
    _event_data: *mut core::ffi::c_void,
    _context: *mut core::ffi::c_void,
) -> u32 {
    match control_code {
        SERVICE_CONTROL_STOP | SERVICE_CONTROL_SHUTDOWN => {
            publish_service_status(SERVICE_STOP_PENDING, 0, 30_000);
            if let Some(stopping) = STOPPING.get() {
                stopping.store(true, Ordering::Release);
            }
            if let Some(cancellation) = CANCELLATION
                .get()
                .and_then(|state| state.lock().ok())
                .and_then(|state| state.clone())
            {
                cancellation.cancel();
            }
        }
        SERVICE_CONTROL_INTERROGATE => {}
        _ => {}
    }
    NO_ERROR.0
}

unsafe extern "system" fn service_main(_argument_count: u32, _argument_values: *mut PWSTR) {
    let Some(service_name) = SERVICE_NAME.get() else {
        return;
    };
    let service_name_wide = service_name.encode_utf16().chain([0]).collect::<Vec<_>>();
    let Ok(status_handle) = RegisterServiceCtrlHandlerExW(
        PCWSTR(service_name_wide.as_ptr()),
        Some(service_control_handler),
        None,
    ) else {
        return;
    };
    SERVICE_STATUS_HANDLE_VALUE.store(status_handle.0 as usize, Ordering::Release);
    publish_service_status(SERVICE_START_PENDING, 0, 30_000);
    let stopping = Arc::new(AtomicBool::new(false));
    let _ = STOPPING.set(Arc::clone(&stopping));
    match run_service_runtime(stopping) {
        Ok(()) => publish_service_status(SERVICE_STOPPED, 0, 0),
        Err(service_error_code) => publish_service_failure(service_error_code),
    }
}

fn run_service_runtime(stopping: Arc<AtomicBool>) -> Result<(), u32> {
    let config_path = CONFIG_PATH.get().ok_or(1_u32)?;
    let config = BackupDaemonConfig::load_private(config_path).map_err(|_| 2_u32)?;
    let current_time = super::now_unix().map_err(|_| 3_u32)?;
    let daemon =
        BackupDaemon::open(config, current_time).map_err(|daemon_error| match daemon_error {
            BackupDaemonError::InvalidConfig => 41_u32,
            BackupDaemonError::ConfigUnavailable => 42_u32,
            BackupDaemonError::Repository => 43_u32,
            BackupDaemonError::Scheduler => 44_u32,
            BackupDaemonError::Status => 45_u32,
            BackupDaemonError::Tool => 46_u32,
        })?;
    let cancellation = daemon.cancellation();
    CANCELLATION
        .get_or_init(|| Mutex::new(None))
        .lock()
        .map_err(|_| 5_u32)?
        .replace(cancellation);
    publish_service_status(SERVICE_RUNNING, 0, 0);
    super::run_daemon(daemon, stopping).map_err(|_| 6_u32)
}

fn publish_service_failure(service_error_code: u32) {
    let status_handle_value = SERVICE_STATUS_HANDLE_VALUE.load(Ordering::Acquire);
    if status_handle_value == 0 {
        return;
    }
    let status_handle = SERVICE_STATUS_HANDLE(status_handle_value as *mut core::ffi::c_void);
    let status = SERVICE_STATUS {
        dwServiceType: SERVICE_WIN32_OWN_PROCESS,
        dwCurrentState: SERVICE_STOPPED,
        dwControlsAccepted: 0,
        dwWin32ExitCode: ERROR_SERVICE_SPECIFIC_ERROR.0,
        dwServiceSpecificExitCode: service_error_code,
        dwCheckPoint: 0,
        dwWaitHint: 0,
    };
    let _ = unsafe { SetServiceStatus(status_handle, &status) };
}

fn publish_service_status(
    current_state: SERVICE_STATUS_CURRENT_STATE,
    exit_code: u32,
    wait_hint: u32,
) {
    let status_handle_value = SERVICE_STATUS_HANDLE_VALUE.load(Ordering::Acquire);
    if status_handle_value == 0 {
        return;
    }
    let status_handle = SERVICE_STATUS_HANDLE(status_handle_value as *mut core::ffi::c_void);
    let controls_accepted = if current_state == SERVICE_RUNNING {
        SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
    } else {
        0
    };
    let status = SERVICE_STATUS {
        dwServiceType: SERVICE_WIN32_OWN_PROCESS,
        dwCurrentState: current_state,
        dwControlsAccepted: controls_accepted,
        dwWin32ExitCode: exit_code,
        dwServiceSpecificExitCode: 0,
        dwCheckPoint: 0,
        dwWaitHint: wait_hint,
    };
    let _ = unsafe { SetServiceStatus(status_handle, &status) };
}
