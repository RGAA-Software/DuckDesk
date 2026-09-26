use std::sync::{
    atomic::{AtomicUsize, Ordering},
    OnceLock,
};
use tokio_util::sync::CancellationToken;
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

type ServiceRunner = fn(&tokio::runtime::Runtime, CancellationToken) -> Result<(), String>;

static SERVICE_NAME: OnceLock<String> = OnceLock::new();
static SERVICE_RUNNER: OnceLock<ServiceRunner> = OnceLock::new();
static STOP_TOKEN: OnceLock<CancellationToken> = OnceLock::new();
static STATUS_HANDLE: AtomicUsize = AtomicUsize::new(0);

pub fn dispatch(service_name: &str, runner: ServiceRunner) -> Result<(), &'static str> {
    SERVICE_NAME
        .set(service_name.to_owned())
        .map_err(|_| "service name already initialized")?;
    SERVICE_RUNNER
        .set(runner)
        .map_err(|_| "service runner already initialized")?;
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

unsafe extern "system" fn control_handler(
    control_code: u32,
    _event_type: u32,
    _event_data: *mut core::ffi::c_void,
    _context: *mut core::ffi::c_void,
) -> u32 {
    match control_code {
        SERVICE_CONTROL_STOP | SERVICE_CONTROL_SHUTDOWN => {
            publish_status(SERVICE_STOP_PENDING, 0, 30_000);
            if let Some(stop_token) = STOP_TOKEN.get() {
                stop_token.cancel();
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
        Some(control_handler),
        None,
    ) else {
        return;
    };
    STATUS_HANDLE.store(status_handle.0 as usize, Ordering::Release);
    publish_status(SERVICE_START_PENDING, 0, 30_000);
    let stop_token = CancellationToken::new();
    if STOP_TOKEN.set(stop_token.clone()).is_err() {
        publish_status(SERVICE_STOPPED, 1, 0);
        return;
    }
    let Ok(runtime) = tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
    else {
        publish_status(SERVICE_STOPPED, 2, 0);
        return;
    };
    let Some(runner) = SERVICE_RUNNER.get() else {
        publish_status(SERVICE_STOPPED, 3, 0);
        return;
    };
    publish_status(SERVICE_RUNNING, 0, 0);
    match runner(&runtime, stop_token) {
        Ok(()) => publish_status(SERVICE_STOPPED, 0, 0),
        Err(_) => publish_status(SERVICE_STOPPED, 4, 0),
    }
    drop(runtime);
}

fn publish_status(current_state: SERVICE_STATUS_CURRENT_STATE, failure_code: u32, wait_hint: u32) {
    let status_handle_value = STATUS_HANDLE.load(Ordering::Acquire);
    if status_handle_value == 0 {
        return;
    }
    let status_handle = SERVICE_STATUS_HANDLE(status_handle_value as *mut core::ffi::c_void);
    let status = SERVICE_STATUS {
        dwServiceType: SERVICE_WIN32_OWN_PROCESS,
        dwCurrentState: current_state,
        dwControlsAccepted: if current_state == SERVICE_RUNNING {
            SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN
        } else {
            0
        },
        dwWin32ExitCode: if failure_code == 0 {
            NO_ERROR.0
        } else {
            ERROR_SERVICE_SPECIFIC_ERROR.0
        },
        dwServiceSpecificExitCode: failure_code,
        dwCheckPoint: 0,
        dwWaitHint: wait_hint,
    };
    let _ = unsafe { SetServiceStatus(status_handle, &status) };
}
