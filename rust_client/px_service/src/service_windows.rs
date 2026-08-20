use std::sync::{Arc, Mutex, OnceLock};

use service_core::config::ServiceConfig;
use service_core::windows_util::{default_service_data_root, default_service_log_root};
use tokio::runtime::Runtime;
use tokio::sync::mpsc;
use tracing::{error, info, warn};
use windows::core::{w, PCWSTR};
use windows::Win32::Foundation::NO_ERROR;
use windows::Win32::System::RemoteDesktop::WTSSESSION_NOTIFICATION;
use windows::Win32::System::Services::{
    RegisterServiceCtrlHandlerExW, SetServiceStatus, StartServiceCtrlDispatcherW,
    SERVICE_ACCEPT_PAUSE_CONTINUE, SERVICE_ACCEPT_POWEREVENT, SERVICE_ACCEPT_SESSIONCHANGE,
    SERVICE_ACCEPT_SHUTDOWN, SERVICE_ACCEPT_STOP, SERVICE_CONTROL_CONTINUE,
    SERVICE_CONTROL_INTERROGATE, SERVICE_CONTROL_PAUSE, SERVICE_CONTROL_SESSIONCHANGE,
    SERVICE_CONTROL_STOP, SERVICE_RUNNING, SERVICE_STATUS, SERVICE_STATUS_HANDLE, SERVICE_STOPPED,
    SERVICE_TABLE_ENTRYW, SERVICE_WIN32_OWN_PROCESS,
};

use crate::service_host::{run_service, ControlEvent, ServiceRuntime};
use crate::windows_actions::WindowsActions;
use crate::windows_process::WindowsProcessManager;

static CONTROL_SENDER: OnceLock<Mutex<Option<mpsc::UnboundedSender<ControlEvent>>>> =
    OnceLock::new();
static BOOTSTRAP_CONFIG: OnceLock<ServiceConfig> = OnceLock::new();

const WTS_CONSOLE_CONNECT_EVENT: u32 = 0x1;
const WTS_CONSOLE_DISCONNECT_EVENT: u32 = 0x2;
const WTS_SESSION_LOGON_EVENT: u32 = 0x5;
const WTS_SESSION_LOGOFF_EVENT: u32 = 0x6;
const WTS_SESSION_LOCK_EVENT: u32 = 0x7;
const WTS_SESSION_UNLOCK_EVENT: u32 = 0x8;

fn control_sender() -> &'static Mutex<Option<mpsc::UnboundedSender<ControlEvent>>> {
    CONTROL_SENDER.get_or_init(|| Mutex::new(None))
}

pub fn dispatch_service(port: u16) -> Result<(), String> {
    info!("dispatch service requested, port={port}");
    let config = ServiceConfig::new(
        port,
        default_service_data_root(),
        default_service_log_root(),
    );
    let _ = BOOTSTRAP_CONFIG.set(config);
    let mut table = [
        SERVICE_TABLE_ENTRYW {
            lpServiceName: windows::core::PWSTR(w!("px_service").as_ptr() as *mut _),
            lpServiceProc: Some(service_main),
        },
        SERVICE_TABLE_ENTRYW {
            lpServiceName: windows::core::PWSTR::null(),
            lpServiceProc: None,
        },
    ];
    unsafe { StartServiceCtrlDispatcherW(table.as_mut_ptr()) }.map_err(|err| err.to_string())
}

unsafe extern "system" fn service_handler(
    control: u32,
    event_type: u32,
    event_data: *mut core::ffi::c_void,
    _context: *mut core::ffi::c_void,
) -> u32 {
    info!(
        "service handler received control={}, event_type={}",
        control, event_type
    );
    if let Some(sender) = control_sender().lock().unwrap().as_ref() {
        match control {
            SERVICE_CONTROL_STOP => {
                warn!("forwarding stop control event to runtime");
                let _ = sender.send(ControlEvent::Stop);
            }
            SERVICE_CONTROL_SESSIONCHANGE => {
                let session_id = if event_data.is_null() {
                    0
                } else {
                    (*(event_data as *mut WTSSESSION_NOTIFICATION)).dwSessionId
                };
                let event = match event_type {
                    WTS_CONSOLE_CONNECT_EVENT => Some(ControlEvent::ConsoleConnect(session_id)),
                    WTS_CONSOLE_DISCONNECT_EVENT => {
                        Some(ControlEvent::ConsoleDisconnect(session_id))
                    }
                    WTS_SESSION_LOGON_EVENT => Some(ControlEvent::SessionLogon(session_id)),
                    WTS_SESSION_LOGOFF_EVENT => Some(ControlEvent::SessionLogoff(session_id)),
                    WTS_SESSION_LOCK_EVENT => Some(ControlEvent::SessionLock(session_id)),
                    WTS_SESSION_UNLOCK_EVENT => Some(ControlEvent::SessionUnlock(session_id)),
                    _ => None,
                };
                if let Some(event) = event {
                    info!("forwarding session event: {:?}", event);
                    let _ = sender.send(event);
                }
            }
            SERVICE_CONTROL_CONTINUE | SERVICE_CONTROL_PAUSE | SERVICE_CONTROL_INTERROGATE => {}
            _ => {}
        }
    }
    NO_ERROR.0
}

unsafe extern "system" fn service_main(_argc: u32, _argv: *mut windows::core::PWSTR) {
    info!("service_main entered");
    let mut status = SERVICE_STATUS {
        dwServiceType: SERVICE_WIN32_OWN_PROCESS,
        dwCurrentState: SERVICE_RUNNING,
        dwControlsAccepted: SERVICE_ACCEPT_STOP
            | SERVICE_ACCEPT_PAUSE_CONTINUE
            | SERVICE_ACCEPT_POWEREVENT
            | SERVICE_ACCEPT_SHUTDOWN
            | SERVICE_ACCEPT_SESSIONCHANGE,
        dwWin32ExitCode: NO_ERROR.0,
        dwServiceSpecificExitCode: 0,
        dwCheckPoint: 0,
        dwWaitHint: 0,
    };
    let status_handle: SERVICE_STATUS_HANDLE = RegisterServiceCtrlHandlerExW(
        PCWSTR(w!("px_service").as_ptr()),
        Some(service_handler),
        None,
    )
    .expect("RegisterServiceCtrlHandlerExW failed");
    let _ = SetServiceStatus(status_handle, &status);
    info!("service status set to running");

    let (tx, rx) = mpsc::unbounded_channel();
    *control_sender().lock().unwrap() = Some(tx);

    let config = BOOTSTRAP_CONFIG.get().cloned().unwrap_or_else(|| {
        ServiceConfig::new(
            20375,
            default_service_data_root(),
            default_service_log_root(),
        )
    });
    let runtime = Arc::new(tokio::sync::Mutex::new(ServiceRuntime::new(
        config,
        Arc::new(WindowsProcessManager::new()),
        Arc::new(WindowsActions::new()),
    )));

    let rt = Runtime::new().expect("tokio runtime");
    match rt.block_on(run_service(runtime, Some(rx))) {
        Ok(()) => info!("service runtime exited normally"),
        Err(err) => error!("service runtime exited with error: {err}"),
    }

    status.dwControlsAccepted = 0;
    status.dwCurrentState = SERVICE_STOPPED;
    let _ = SetServiceStatus(status_handle, &status);
    info!("service status set to stopped");
}
