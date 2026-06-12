use std::ffi::c_void;

use service_core::process::ProcessSnapshot;
use serde::Deserialize;
use windows::core::{PCWSTR, PWSTR};
use windows::Win32::Foundation::{CloseHandle, HANDLE};
use windows::Win32::Security::{
    DuplicateTokenEx, SecurityImpersonation, TokenPrimary, TOKEN_ALL_ACCESS,
};
use windows::Win32::System::Environment::{CreateEnvironmentBlock, DestroyEnvironmentBlock};
use windows::Win32::System::RemoteDesktop::{WTSGetActiveConsoleSessionId, WTSQueryUserToken};
use windows::Win32::System::Threading::{
    CreateProcessAsUserW, OpenProcess, TerminateProcess, CREATE_UNICODE_ENVIRONMENT,
    PROCESS_CREATION_FLAGS, PROCESS_INFORMATION, PROCESS_QUERY_LIMITED_INFORMATION,
    PROCESS_TERMINATE, STARTUPINFOW,
};
use wmi::{COMLibrary, WMIConnection};

pub trait ProcessManager: Send + Sync {
    fn list_processes(&self) -> Result<Vec<ProcessSnapshot>, String>;
    fn kill_process(&self, pid: u32) -> Result<(), String>;
    fn start_process_as_active_user(
        &self,
        work_dir: &str,
        app_path: &str,
        args: &[String],
    ) -> Result<(), String>;
}

#[derive(Default)]
pub struct WindowsProcessManager;

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct Win32Process {
    process_id: u32,
    executable_path: Option<String>,
    command_line: Option<String>,
    name: Option<String>,
}

impl WindowsProcessManager {
    pub fn new() -> Self {
        Self
    }
}

impl ProcessManager for WindowsProcessManager {
    fn list_processes(&self) -> Result<Vec<ProcessSnapshot>, String> {
        let com = COMLibrary::new().map_err(|err| err.to_string())?;
        let wmi = WMIConnection::new(com).map_err(|err| err.to_string())?;
        let query = "SELECT ProcessId, ExecutablePath, CommandLine, Name FROM Win32_Process";
        let rows: Vec<Win32Process> = wmi.raw_query(query).map_err(|err| err.to_string())?;
        Ok(rows
            .into_iter()
            .map(|row| {
                let exe_path = row.executable_path.or(row.name).unwrap_or_default();
                let cmdline = row.command_line.unwrap_or_default();
                ProcessSnapshot::new(row.process_id, exe_path, cmdline)
            })
            .collect())
    }

    fn kill_process(&self, pid: u32) -> Result<(), String> {
        unsafe {
            let handle = OpenProcess(
                PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                false,
                pid,
            )
            .map_err(|err| format!("OpenProcess failed: {err}"))?;
            let result = TerminateProcess(handle, 0);
            let _ = CloseHandle(handle);
            result.map_err(|err| format!("TerminateProcess failed: {err}"))
        }
    }

    fn start_process_as_active_user(
        &self,
        work_dir: &str,
        app_path: &str,
        args: &[String],
    ) -> Result<(), String> {
        unsafe {
            let session_id = WTSGetActiveConsoleSessionId();
            let mut user_token = HANDLE::default();
            WTSQueryUserToken(session_id, &mut user_token)
                .map_err(|err| format!("WTSQueryUserToken failed: {err}"))?;

            let mut primary_token = HANDLE::default();
            DuplicateTokenEx(
                user_token,
                TOKEN_ALL_ACCESS,
                None,
                SecurityImpersonation,
                TokenPrimary,
                &mut primary_token,
            )
            .map_err(|err| format!("DuplicateTokenEx failed: {err}"))?;

            let mut environment: *mut c_void = std::ptr::null_mut();
            CreateEnvironmentBlock(&mut environment, Some(primary_token), false)
                .map_err(|err| format!("CreateEnvironmentBlock failed: {err}"))?;

            let command = std::iter::once(app_path.to_string())
                .chain(args.iter().cloned())
                .collect::<Vec<_>>()
                .join(" ");
            let mut command_w: Vec<u16> = command.encode_utf16().chain(Some(0)).collect();
            let work_dir_w: Vec<u16> = work_dir.encode_utf16().chain(Some(0)).collect();
            let mut desktop_w: Vec<u16> =
                "winsta0\\default".encode_utf16().chain(Some(0)).collect();
            let mut startup_info = STARTUPINFOW::default();
            startup_info.cb = std::mem::size_of::<STARTUPINFOW>() as u32;
            startup_info.lpDesktop = PWSTR(desktop_w.as_mut_ptr());
            let mut process_info = PROCESS_INFORMATION::default();

            let result = CreateProcessAsUserW(
                Some(primary_token),
                PCWSTR::null(),
                Some(PWSTR(command_w.as_mut_ptr())),
                None,
                None,
                false,
                PROCESS_CREATION_FLAGS(CREATE_UNICODE_ENVIRONMENT.0),
                Some(environment),
                PCWSTR(work_dir_w.as_ptr()),
                &startup_info,
                &mut process_info,
            );

            let _ = DestroyEnvironmentBlock(environment);
            let _ = CloseHandle(user_token);
            let _ = CloseHandle(primary_token);
            if result.is_ok() {
                let _ = CloseHandle(process_info.hThread);
                let _ = CloseHandle(process_info.hProcess);
                Ok(())
            } else {
                Err(result.unwrap_err().to_string())
            }
        }
    }
}
