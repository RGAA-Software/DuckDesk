use std::ffi::OsStr;
use std::iter::once;
use std::os::windows::ffi::OsStrExt;
use std::thread;
use std::time::{Duration, Instant};

use windows::core::{PCWSTR, PWSTR};
use windows::Win32::Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE};
use windows::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};
use windows::Win32::System::Services::{
    ChangeServiceConfig2W, ChangeServiceConfigW, CloseServiceHandle, ControlService, CreateServiceW,
    DeleteService, ENUM_SERVICE_TYPE, OpenSCManagerW, OpenServiceW, QueryServiceConfigW,
    QueryServiceStatusEx, SERVICE_ERROR, SERVICE_START_TYPE, StartServiceW, SC_ACTION,
    SC_ACTION_RESTART, SC_HANDLE, SC_MANAGER_ALL_ACCESS, SC_MANAGER_CONNECT,
    SC_STATUS_PROCESS_INFO, SERVICE_ALL_ACCESS, SERVICE_AUTO_START, SERVICE_CONFIG_DESCRIPTION,
    SERVICE_CONFIG_FAILURE_ACTIONS, SERVICE_CONTROL_STOP, SERVICE_DESCRIPTIONW,
    SERVICE_ENUMERATE_DEPENDENTS, SERVICE_ERROR_NORMAL, SERVICE_FAILURE_ACTIONSW,
    SERVICE_NO_CHANGE, SERVICE_QUERY_CONFIG, SERVICE_QUERY_STATUS, SERVICE_RUNNING,
    SERVICE_STATUS, SERVICE_STATUS_PROCESS, SERVICE_STOP, SERVICE_STOPPED, SERVICE_STOP_PENDING,
    SERVICE_WIN32_OWN_PROCESS,
};
use windows::Win32::System::Threading::{OpenProcess, PROCESS_TERMINATE, TerminateProcess};

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum ServiceStatus {
    Unknown,
    Pending,
    Running,
    Stopped,
}

impl ServiceStatus {
    pub fn as_str(status: Self) -> &'static str {
        match status {
            Self::Unknown => "unknown",
            Self::Pending => "pending",
            Self::Running => "running",
            Self::Stopped => "stopped",
        }
    }

    pub fn from_current_state(
        state: windows::Win32::System::Services::SERVICE_STATUS_CURRENT_STATE,
    ) -> Self {
        match state {
            SERVICE_RUNNING => Self::Running,
            SERVICE_STOPPED => Self::Stopped,
            SERVICE_STOP_PENDING => Self::Pending,
            _ => Self::Unknown,
        }
    }
}

pub struct ServiceManager {
    service_name: String,
    display_name: String,
    description: String,
}

impl ServiceManager {
    pub fn new(service_name: &str, display_name: &str, description: &str) -> Self {
        Self {
            service_name: service_name.to_string(),
            display_name: display_name.to_string(),
            description: description.to_string(),
        }
    }

    pub fn install(&self, service_bin: &str) -> Result<(), String> {
        let service_command_line = normalize_service_bin_command_line(service_bin);
        unsafe {
            let scm = open_scm(SC_MANAGER_ALL_ACCESS)?;
            let _scm_guard = HandleGuard(scm);

            let existing = OpenServiceW(
                scm,
                PCWSTR(to_wide(&self.service_name).as_ptr()),
                SERVICE_ALL_ACCESS,
            );
            let service = if let Ok(handle) = existing {
                update_service_config(
                    handle,
                    &self.display_name,
                    &service_command_line,
                )?;
                handle
            } else {
                CreateServiceW(
                    scm,
                    PCWSTR(to_wide(&self.service_name).as_ptr()),
                    PCWSTR(to_wide(&self.display_name).as_ptr()),
                    SERVICE_ALL_ACCESS,
                    SERVICE_WIN32_OWN_PROCESS,
                    SERVICE_AUTO_START,
                    SERVICE_ERROR_NORMAL,
                    PCWSTR(to_wide(&service_command_line).as_ptr()),
                    None,
                    None,
                    None,
                    None,
                    None,
                )
                .map_err(|err| format!("CreateServiceW failed: {err}"))?
            };
            let _service_guard = HandleGuard(service);

            let status = query_status_internal(service)?;
            if status != ServiceStatus::Running {
                StartServiceW(service, None)
                    .map_err(|err| format!("StartServiceW failed: {err}"))?;
            }

            let mut restart_action = SC_ACTION {
                Type: SC_ACTION_RESTART,
                Delay: 3000,
            };
            let failure_actions = SERVICE_FAILURE_ACTIONSW {
                dwResetPeriod: 600,
                lpRebootMsg: PWSTR::null(),
                lpCommand: PWSTR::null(),
                cActions: 1,
                lpsaActions: &mut restart_action,
            };
            let _ = ChangeServiceConfig2W(
                service,
                SERVICE_CONFIG_FAILURE_ACTIONS,
                Some(&failure_actions as *const _ as *const _),
            );
            let mut description = to_wide(&self.description);
            let description_info = SERVICE_DESCRIPTIONW {
                lpDescription: PWSTR(description.as_mut_ptr()),
            };
            let _ = ChangeServiceConfig2W(
                service,
                SERVICE_CONFIG_DESCRIPTION,
                Some(&description_info as *const _ as *const _),
            );
            Ok(())
        }
    }

    pub fn remove(&self, uninstall_service: bool) -> Result<(), String> {
        self.stop()?;
        if uninstall_service {
            unsafe {
                let scm = open_scm(SC_MANAGER_ALL_ACCESS)?;
                let _scm_guard = HandleGuard(scm);
                let service = OpenServiceW(
                    scm,
                    PCWSTR(to_wide(&self.service_name).as_ptr()),
                    SERVICE_ALL_ACCESS,
                )
                .map_err(|err| format!("OpenServiceW failed: {err}"))?;
                let _service_guard = HandleGuard(service);
                DeleteService(service).map_err(|err| format!("DeleteService failed: {err}"))?;
            }
        }
        Ok(())
    }

    pub fn stop(&self) -> Result<(), String> {
        unsafe {
            let scm = open_scm(SC_MANAGER_ALL_ACCESS)?;
            let _scm_guard = HandleGuard(scm);
            let service = OpenServiceW(
                scm,
                PCWSTR(to_wide(&self.service_name).as_ptr()),
                SERVICE_ALL_ACCESS
                    | SERVICE_STOP
                    | SERVICE_QUERY_STATUS
                    | SERVICE_ENUMERATE_DEPENDENTS,
            )
            .map_err(|err| format!("OpenServiceW failed: {err}"))?;
            let _service_guard = HandleGuard(service);

            let status = query_status_raw(service)?;
            if status.dwCurrentState != SERVICE_STOPPED {
                let mut service_status = SERVICE_STATUS::default();
                let _ = ControlService(service, SERVICE_CONTROL_STOP, &mut service_status);
                wait_for_stop(service, Duration::from_secs(30))?;
            }

            Ok(())
        }
    }

    pub fn shutdown(&self, uninstall_service: bool, current_pid: u32) -> Result<(), String> {
        let remove_result = self.remove(uninstall_service);
        let kill_result = self.kill_gamma_processes(current_pid);
        match (remove_result, kill_result) {
            (Ok(()), Ok(())) => Ok(()),
            (Err(remove_err), Ok(())) => Err(remove_err),
            (Ok(()), Err(kill_err)) => Err(kill_err),
            (Err(remove_err), Err(kill_err)) => {
                Err(format!("{remove_err}; cleanup failed: {kill_err}"))
            }
        }
    }

    pub fn query_status(&self) -> Result<ServiceStatus, String> {
        unsafe {
            let scm = open_scm(SC_MANAGER_CONNECT)?;
            let _scm_guard = HandleGuard(scm);
            let service = OpenServiceW(
                scm,
                PCWSTR(to_wide(&self.service_name).as_ptr()),
                SERVICE_QUERY_STATUS,
            )
            .map_err(|err| format!("OpenServiceW failed: {err}"))?;
            let _service_guard = HandleGuard(service);
            query_status_internal(service)
        }
    }

    pub fn get_service_executable_path(&self) -> Result<Option<String>, String> {
        unsafe {
            let scm = open_scm(SC_MANAGER_CONNECT)?;
            let _scm_guard = HandleGuard(scm);
            let service = OpenServiceW(
                scm,
                PCWSTR(to_wide(&self.service_name).as_ptr()),
                SERVICE_QUERY_CONFIG,
            )
            .map_err(|err| format!("OpenServiceW failed: {err}"))?;
            let _service_guard = HandleGuard(service);

            let mut needed = 0;
            let _ = QueryServiceConfigW(service, None, 0, &mut needed);
            if needed == 0 {
                return Ok(None);
            }
            let mut buffer = vec![0u8; needed as usize];
            let config_ptr =
                buffer.as_mut_ptr() as *mut windows::Win32::System::Services::QUERY_SERVICE_CONFIGW;
            QueryServiceConfigW(service, Some(config_ptr), needed, &mut needed)
                .map_err(|err| format!("QueryServiceConfigW failed: {err}"))?;
            let path = parse_service_binary_path(&pwstr_to_string((*config_ptr).lpBinaryPathName));
            Ok(Some(path))
        }
    }

    fn kill_gamma_processes(&self, current_pid: u32) -> Result<(), String> {
        let target_names = gamma_cleanup_process_names();
        unsafe {
            let snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
                .map_err(|err| format!("CreateToolhelp32Snapshot failed: {err}"))?;
            let _snapshot_guard = WinHandleGuard(snapshot);
            if snapshot == INVALID_HANDLE_VALUE {
                return Err("CreateToolhelp32Snapshot returned invalid handle".to_string());
            }

            let mut entry = PROCESSENTRY32W::default();
            entry.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;
            let mut has_entry =
                Process32FirstW(snapshot, &mut entry).map(|_| true).unwrap_or(false);
            while has_entry {
                let exe_name = wide_cstr_to_string(&entry.szExeFile);
                let should_kill = target_names
                    .iter()
                    .any(|name| exe_name.eq_ignore_ascii_case(name));
                if should_kill || entry.th32ProcessID == current_pid {
                    let _ = kill_process(entry.th32ProcessID);
                }
                has_entry = Process32NextW(snapshot, &mut entry).map(|_| true).unwrap_or(false);
            }
        }

        Ok(())
    }
}

fn gamma_cleanup_process_names() -> &'static [&'static str] {
    &[
        "GammaRayRender.exe",
        "GammaRayClientInner.exe",
        "GammaRaySysInfo.exe",
        "GammaRayUserProxy.exe",
    ]
}

unsafe fn open_scm(access: u32) -> Result<SC_HANDLE, String> {
    OpenSCManagerW(None, None, access).map_err(|err| format!("OpenSCManagerW failed: {err}"))
}

unsafe fn query_status_internal(service: SC_HANDLE) -> Result<ServiceStatus, String> {
    let raw = query_status_raw(service)?;
    Ok(ServiceStatus::from_current_state(raw.dwCurrentState))
}

unsafe fn query_status_raw(service: SC_HANDLE) -> Result<SERVICE_STATUS_PROCESS, String> {
    let mut status = SERVICE_STATUS_PROCESS::default();
    let mut needed = 0;
    let status_bytes = std::slice::from_raw_parts_mut(
        (&mut status as *mut SERVICE_STATUS_PROCESS).cast::<u8>(),
        std::mem::size_of::<SERVICE_STATUS_PROCESS>(),
    );
    QueryServiceStatusEx(
        service,
        SC_STATUS_PROCESS_INFO,
        Some(status_bytes),
        &mut needed,
    )
    .map_err(|err| format!("QueryServiceStatusEx failed: {err}"))?;
    Ok(status)
}

unsafe fn wait_for_stop(service: SC_HANDLE, timeout: Duration) -> Result<(), String> {
    let start = Instant::now();
    loop {
        let status = query_status_raw(service)?;
        if status.dwCurrentState == SERVICE_STOPPED {
            return Ok(());
        }
        if start.elapsed() > timeout {
            return Err("waiting for service stop timed out".to_string());
        }
        thread::sleep(Duration::from_millis(500));
    }
}

fn to_wide(value: &str) -> Vec<u16> {
    OsStr::new(value).encode_wide().chain(once(0)).collect()
}

fn normalize_service_bin_command_line(value: &str) -> String {
    let trimmed = value.trim();
    if trimmed.is_empty() || trimmed.starts_with('"') {
        return trimmed.to_string();
    }

    let lower = trimmed.to_ascii_lowercase();
    if let Some(exe_index) = lower.find(".exe") {
        let exe_end = exe_index + 4;
        let exe_path = &trimmed[..exe_end];
        let args = trimmed[exe_end..].trim_start();
        if args.is_empty() {
            format!("\"{exe_path}\"")
        } else {
            format!("\"{exe_path}\" {args}")
        }
    } else {
        trimmed.to_string()
    }
}

fn parse_service_binary_path(value: &str) -> String {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        return String::new();
    }

    if let Some(rest) = trimmed.strip_prefix('"') {
        if let Some(end_quote) = rest.find('"') {
            return rest[..end_quote].to_string();
        }
    }

    let lower = trimmed.to_ascii_lowercase();
    if let Some(exe_index) = lower.find(".exe") {
        return trimmed[..exe_index + 4].trim().to_string();
    }

    trimmed.to_string()
}

unsafe fn update_service_config(
    service: SC_HANDLE,
    display_name: &str,
    service_command_line: &str,
) -> Result<(), String> {
    ChangeServiceConfigW(
        service,
        ENUM_SERVICE_TYPE(SERVICE_NO_CHANGE),
        SERVICE_START_TYPE(SERVICE_NO_CHANGE),
        SERVICE_ERROR(SERVICE_NO_CHANGE),
        PCWSTR(to_wide(service_command_line).as_ptr()),
        None,
        None,
        None,
        None,
        None,
        PCWSTR(to_wide(display_name).as_ptr()),
    )
    .map_err(|err| format!("ChangeServiceConfigW failed: {err}"))?;
    Ok(())
}

unsafe fn pwstr_to_string(value: PWSTR) -> String {
    if value.is_null() {
        return String::new();
    }
    let mut len = 0usize;
    while *value.0.add(len) != 0 {
        len += 1;
    }
    String::from_utf16_lossy(std::slice::from_raw_parts(value.0, len))
}

struct HandleGuard(SC_HANDLE);

impl Drop for HandleGuard {
    fn drop(&mut self) {
        unsafe {
            let _ = CloseServiceHandle(self.0);
        }
    }
}

struct WinHandleGuard(HANDLE);

impl Drop for WinHandleGuard {
    fn drop(&mut self) {
        unsafe {
            let _ = CloseHandle(self.0);
        }
    }
}

unsafe fn kill_process(pid: u32) -> Result<(), String> {
    let process = OpenProcess(PROCESS_TERMINATE, false, pid)
        .map_err(|err| format!("OpenProcess failed for pid {pid}: {err}"))?;
    let _guard = WinHandleGuard(process);
    TerminateProcess(process, 0)
        .map_err(|err| format!("TerminateProcess failed for pid {pid}: {err}"))
}

fn wide_cstr_to_string(value: &[u16]) -> String {
    let len = value.iter().position(|ch| *ch == 0).unwrap_or(value.len());
    String::from_utf16_lossy(&value[..len])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_mapping_matches_windows_values() {
        assert_eq!(
            ServiceStatus::from_current_state(SERVICE_RUNNING),
            ServiceStatus::Running
        );
        assert_eq!(
            ServiceStatus::from_current_state(SERVICE_STOPPED),
            ServiceStatus::Stopped
        );
        assert_eq!(
            ServiceStatus::from_current_state(SERVICE_STOP_PENDING),
            ServiceStatus::Pending
        );
    }

    #[test]
    fn status_strings_are_stable() {
        assert_eq!(ServiceStatus::as_str(ServiceStatus::Running), "running");
        assert_eq!(ServiceStatus::as_str(ServiceStatus::Stopped), "stopped");
        assert_eq!(ServiceStatus::as_str(ServiceStatus::Pending), "pending");
        assert_eq!(ServiceStatus::as_str(ServiceStatus::Unknown), "unknown");
    }

    #[test]
    fn wide_conversion_adds_trailing_nul() {
        let value = to_wide("GammaRayService");
        assert_eq!(*value.last().unwrap(), 0);
        assert_eq!(
            String::from_utf16_lossy(&value[..value.len() - 1]),
            "GammaRayService"
        );
    }

    #[test]
    fn manager_stores_metadata() {
        let manager = ServiceManager::new(
            "GammaRayService",
            "GammaRayService",
            "** GammaRay Service **",
        );
        assert_eq!(manager.service_name, "GammaRayService");
        assert_eq!(manager.display_name, "GammaRayService");
        assert_eq!(manager.description, "** GammaRay Service **");
    }

    #[test]
    fn normalize_service_bin_quotes_exe_with_args() {
        let value = normalize_service_bin_command_line(
            "C:/Program Files/GammaRay/GammaRayService.exe 20375",
        );
        assert_eq!(
            value,
            "\"C:/Program Files/GammaRay/GammaRayService.exe\" 20375"
        );
    }

    #[test]
    fn normalize_service_bin_keeps_quoted_value() {
        let value = normalize_service_bin_command_line(
            "\"C:/Program Files/GammaRay/GammaRayService.exe\" 20375",
        );
        assert_eq!(
            value,
            "\"C:/Program Files/GammaRay/GammaRayService.exe\" 20375"
        );
    }

    #[test]
    fn normalize_service_bin_quotes_exe_without_args() {
        let value =
            normalize_service_bin_command_line("C:/Program Files/GammaRay/GammaRayService.exe");
        assert_eq!(value, "\"C:/Program Files/GammaRay/GammaRayService.exe\"");
    }

    #[test]
    fn parse_service_binary_path_extracts_quoted_exe() {
        let value = parse_service_binary_path(
            "\"D:/GammaRay/GammaRayService.exe\" 20375",
        );
        assert_eq!(value, "D:/GammaRay/GammaRayService.exe");
    }

    #[test]
    fn parse_service_binary_path_extracts_unquoted_exe() {
        let value = parse_service_binary_path(
            "D:/GammaRay/GammaRayService.exe 20375",
        );
        assert_eq!(value, "D:/GammaRay/GammaRayService.exe");
    }

    #[test]
    fn parse_service_binary_path_keeps_plain_exe() {
        let value = parse_service_binary_path(
            "D:/GammaRay/GammaRayService.exe",
        );
        assert_eq!(value, "D:/GammaRay/GammaRayService.exe");
    }

    #[test]
    fn gamma_cleanup_process_names_match_expected_targets() {
        assert_eq!(
            gamma_cleanup_process_names(),
            &[
                "GammaRayRender.exe",
                "GammaRayClientInner.exe",
                "GammaRaySysInfo.exe",
                "GammaRayUserProxy.exe",
            ]
        );
    }
}
