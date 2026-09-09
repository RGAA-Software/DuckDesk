use std::ffi::c_void;
use std::os::windows::io::{AsRawHandle, FromRawHandle, OwnedHandle};
use std::sync::Arc;

use serde::Deserialize;
use service_core::process::ProcessSnapshot;
use tracing::{error, info, warn};
use windows::core::{PCWSTR, PWSTR};
use windows::Win32::Foundation::{CloseHandle, HANDLE};
use windows::Win32::Security::{
    DuplicateTokenEx, SecurityIdentification, SecurityImpersonation, SetTokenInformation,
    TokenPrimary, TokenSessionId, TOKEN_ALL_ACCESS,
};
use windows::Win32::System::Environment::{CreateEnvironmentBlock, DestroyEnvironmentBlock};
use windows::Win32::System::RemoteDesktop::{WTSGetActiveConsoleSessionId, WTSQueryUserToken};
use windows::Win32::System::Threading::{
    CreateProcessAsUserW, GetCurrentProcess, OpenProcess, OpenProcessToken, TerminateProcess,
    CREATE_NEW_CONSOLE, CREATE_UNICODE_ENVIRONMENT, NORMAL_PRIORITY_CLASS, PROCESS_CREATION_FLAGS,
    PROCESS_INFORMATION, PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_TERMINATE,
    STARTF_USESHOWWINDOW, STARTUPINFOW,
};
use windows::Win32::UI::WindowsAndMessaging::SW_SHOW;
use wmi::{COMLibrary, WMIConnection};

pub trait ProcessManager: Send + Sync {
    fn observe_exit(&self, _pid: u32, _expected_path: &str) -> Result<Arc<dyn ProcessExitObserver>, String> {
        Err("process exit observation unavailable".into())
    }
    fn list_processes(&self) -> Result<Vec<ProcessSnapshot>, String>;
    fn kill_process(&self, pid: u32) -> Result<(), String>;
    fn start_process_as_active_user(
        &self,
        work_dir: &str,
        app_path: &str,
        args: &[String],
    ) -> Result<(), String>;

    /// RDP workers stay in the Service security context; never use an interactive user's token.
    fn start_process_as_service(&self, _work_dir: &str, _app_path: &str, _args: &[String]) -> Result<(), String> {
        Err("Service-context process launch is unavailable".into())
    }

    /// Launch strictly with the logged-on user's WTS token (no SYSTEM token
    /// fallback). Required for UserProxy, which must run as the session user.
    fn start_process_as_session_user(
        &self,
        work_dir: &str,
        app_path: &str,
        args: &[String],
    ) -> Result<(), String> {
        self.start_process_as_active_user(work_dir, app_path, args)
    }
}

pub trait ProcessExitObserver: Send + Sync {
    /// None means this exact process generation is still alive.
    fn exit_code(&self) -> Result<Option<u32>, String>;
}

struct WindowsExitObserver(OwnedHandle);

impl ProcessExitObserver for WindowsExitObserver {
    fn exit_code(&self) -> Result<Option<u32>, String> {
        use windows::Win32::Foundation::{WAIT_OBJECT_0, WAIT_TIMEOUT};
        use windows::Win32::System::Threading::{GetExitCodeProcess, WaitForSingleObject};
        unsafe {
            let handle = HANDLE(self.0.as_raw_handle());
            match WaitForSingleObject(handle, 0) {
                WAIT_TIMEOUT => Ok(None),
                WAIT_OBJECT_0 => {
                    let mut code = 0;
                    GetExitCodeProcess(handle, &mut code).map_err(|error| error.to_string())?;
                    Ok(Some(code))
                }
                _ => Err("process exit observation failed".into()),
            }
        }
    }
}

#[derive(Default)]
pub struct WindowsProcessManager;

#[derive(Debug, Deserialize)]
#[serde(rename_all = "PascalCase")]
struct Win32Process {
    process_id: u32,
    parent_process_id: Option<u32>,
    executable_path: Option<String>,
    command_line: Option<String>,
    name: Option<String>,
}

impl WindowsProcessManager {
    pub fn new() -> Self {
        Self
    }
}

const SENSITIVE_ARG_NAMES: &[&str] = &[
    "--service_ipc_token",
    "--user_session_token",
    "--connection_ticket",
    "--ticket",
    "--appkey",
    "--app_key",
    "--password",
    "--virtual-display-worker-nonce",
    "--virtual-display-worker-result",
    "--webview_url_b64",
];

fn is_sensitive_arg_name(value: &str) -> bool {
    SENSITIVE_ARG_NAMES
        .iter()
        .any(|name| value.eq_ignore_ascii_case(name))
}

fn redact_args(args: &[String]) -> Vec<String> {
    let mut redact_next = false;
    args.iter()
        .map(|arg| {
            if redact_next {
                redact_next = false;
                return "<redacted>".to_string();
            }
            if let Some((name, _)) = arg.split_once('=') {
                if is_sensitive_arg_name(name) {
                    return format!("{name}=<redacted>");
                }
            }
            if is_sensitive_arg_name(arg) {
                redact_next = true;
            }
            arg.clone()
        })
        .collect()
}

impl ProcessManager for WindowsProcessManager {
    fn observe_exit(&self, pid: u32, expected_path: &str) -> Result<Arc<dyn ProcessExitObserver>, String> {
        use windows::Win32::System::Threading::{QueryFullProcessImageNameW, PROCESS_NAME_WIN32, PROCESS_SYNCHRONIZE};
        unsafe {
            let handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SYNCHRONIZE, false, pid)
                .map_err(|error| error.to_string())?;
            let owned = OwnedHandle::from_raw_handle(handle.0);
            let mut path = vec![0_u16; 32768];
            let mut length = path.len() as u32;
            QueryFullProcessImageNameW(HANDLE(owned.as_raw_handle()), PROCESS_NAME_WIN32, PWSTR(path.as_mut_ptr()), &mut length)
                .map_err(|error| error.to_string())?;
            let actual = String::from_utf16(&path[..length as usize]).map_err(|error| error.to_string())?;
            if !ProcessSnapshot::new(pid, actual, "").exe_path_eq(expected_path) {
                return Err("process exit observer executable mismatch".into());
            }
            Ok(Arc::new(WindowsExitObserver(owned)))
        }
    }
    fn start_process_as_service(&self, work_dir: &str, app_path: &str, args: &[String]) -> Result<(), String> {
        use std::os::windows::process::CommandExt;
        let child = std::process::Command::new(app_path)
            .args(args)
            .current_dir(work_dir)
            .creation_flags(windows::Win32::System::Threading::CREATE_NO_WINDOW.0)
            .stdin(std::process::Stdio::null())
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .spawn()
            .map_err(|error| format!("Service-context Render launch failed: {error}"))?;
        info!("Service-context Render launched, pid={}", child.id());
        // Dropping Child closes our handles but does not terminate the Windows process.
        drop(child);
        Ok(())
    }
    fn list_processes(&self) -> Result<Vec<ProcessSnapshot>, String> {
        let com = COMLibrary::new().map_err(|err| err.to_string())?;
        let wmi = WMIConnection::new(com).map_err(|err| err.to_string())?;
        let query =
            "SELECT ProcessId, ParentProcessId, ExecutablePath, CommandLine, Name FROM Win32_Process";
        let rows: Vec<Win32Process> = wmi.raw_query(query).map_err(|err| err.to_string())?;
        Ok(rows
            .into_iter()
            .map(|row| {
                let exe_path = row.executable_path.or(row.name).unwrap_or_default();
                let cmdline = row.command_line.unwrap_or_default();
                let mut snap = ProcessSnapshot::new(row.process_id, exe_path, cmdline);
                snap.parent_pid = row.parent_process_id;
                snap
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
        let safe_args = redact_args(args);
        info!(
            "start process as active user requested, work_dir={}, app_path={}, args={:?}",
            work_dir, app_path, safe_args
        );
        match start_process_with_service_token_session(work_dir, app_path, args) {
            Ok(()) => {
                info!("start process as active user finished by service token session path");
                Ok(())
            }
            Err(service_err) => {
                warn!(
                    "service token session launch failed, will fallback to WTS user token: {}",
                    service_err
                );
                match start_process_with_wts_user_token(work_dir, app_path, args) {
                    Ok(()) => {
                        info!("start process as active user finished by WTS user token fallback");
                        Ok(())
                    }
                    Err(wts_err) => {
                        // Console / non-SYSTEM runs lack SeTcbPrivilege / WTSQueryUserToken.
                        // Fall back to a normal CreateProcess in the current session so
                        // Console game-hook scheduling can still be exercised locally.
                        warn!(
                            "token launch paths failed ({service_err}; {wts_err}); trying direct CreateProcess"
                        );
                        match start_process_direct(work_dir, app_path, args) {
                            Ok(()) => {
                                info!("start process finished by direct CreateProcess fallback");
                                Ok(())
                            }
                            Err(direct_err) => {
                                let combined = format!(
                                    "{service_err}; fallback failed: {wts_err}; direct failed: {direct_err}"
                                );
                                error!("start process as active user failed: {}", combined);
                                Err(combined)
                            }
                        }
                    }
                }
            }
        }
    }

    fn start_process_as_session_user(
        &self,
        work_dir: &str,
        app_path: &str,
        args: &[String],
    ) -> Result<(), String> {
        let safe_args = redact_args(args);
        info!(
            "start process as session user (WTS token only) requested, work_dir={}, app_path={}, args={:?}",
            work_dir, app_path, safe_args
        );
        start_process_with_wts_user_token(work_dir, app_path, args)
    }
}

fn start_process_with_service_token_session(
    work_dir: &str,
    app_path: &str,
    args: &[String],
) -> Result<(), String> {
    unsafe {
        info!("service token session launch begin");
        let session_id = WTSGetActiveConsoleSessionId();
        info!(
            "service token session launch active console session id={}",
            session_id
        );
        if session_id == u32::MAX {
            error!("service token session launch failed: no active console session");
            return Err("WTSGetActiveConsoleSessionId returned no active session".to_string());
        }

        let mut service_token = HANDLE::default();
        if let Err(err) =
            OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &mut service_token)
        {
            error!(
                "service token session launch OpenProcessToken failed: {}",
                err
            );
            return Err(format!("OpenProcessToken failed: {err}"));
        }
        info!("service token session launch OpenProcessToken succeeded");

        let mut primary_token = HANDLE::default();
        let duplicate_result = DuplicateTokenEx(
            service_token,
            TOKEN_ALL_ACCESS,
            None,
            SecurityIdentification,
            TokenPrimary,
            &mut primary_token,
        );
        let _ = CloseHandle(service_token);
        if let Err(err) = duplicate_result {
            error!(
                "service token session launch DuplicateTokenEx failed: {}",
                err
            );
            return Err(format!("DuplicateTokenEx failed: {err}"));
        }
        info!("service token session launch DuplicateTokenEx succeeded");

        let set_session_result = SetTokenInformation(
            primary_token,
            TokenSessionId,
            &session_id as *const _ as *const c_void,
            std::mem::size_of::<u32>() as u32,
        );
        if let Err(err) = set_session_result {
            let _ = CloseHandle(primary_token);
            error!(
                "service token session launch SetTokenInformation failed, session_id={}, error={}",
                session_id, err
            );
            return Err(format!(
                "SetTokenInformation(TokenSessionId={session_id}) failed: {err}"
            ));
        }
        info!(
            "service token session launch SetTokenInformation succeeded, session_id={}",
            session_id
        );

        create_process_with_token(
            "service_token_session",
            primary_token,
            work_dir,
            app_path,
            args,
        )
        .map_err(|err| format!("service token session launch failed: {err}"))
    }
}

fn start_process_with_wts_user_token(
    work_dir: &str,
    app_path: &str,
    args: &[String],
) -> Result<(), String> {
    unsafe {
        info!("WTS user token launch begin");
        let session_id = WTSGetActiveConsoleSessionId();
        info!(
            "WTS user token launch active console session id={}",
            session_id
        );
        if session_id == u32::MAX {
            error!("WTS user token launch failed: no active console session");
            return Err("WTSGetActiveConsoleSessionId returned no active session".to_string());
        }

        let mut user_token = HANDLE::default();
        if let Err(err) = WTSQueryUserToken(session_id, &mut user_token) {
            error!(
                "WTS user token launch WTSQueryUserToken failed, session_id={}, error={}",
                session_id, err
            );
            return Err(format!(
                "WTSQueryUserToken(session={session_id}) failed: {err}"
            ));
        }
        info!(
            "WTS user token launch WTSQueryUserToken succeeded, session_id={}",
            session_id
        );

        let mut primary_token = HANDLE::default();
        let duplicate_result = DuplicateTokenEx(
            user_token,
            TOKEN_ALL_ACCESS,
            None,
            SecurityImpersonation,
            TokenPrimary,
            &mut primary_token,
        );
        let _ = CloseHandle(user_token);
        if let Err(err) = duplicate_result {
            error!("WTS user token launch DuplicateTokenEx failed: {}", err);
            return Err(format!("DuplicateTokenEx failed: {err}"));
        }
        info!("WTS user token launch DuplicateTokenEx succeeded");

        create_process_with_token("wts_user_token", primary_token, work_dir, app_path, args)
            .map_err(|err| format!("WTS user token launch failed: {err}"))
    }
}

unsafe fn create_process_with_token(
    launch_method: &str,
    primary_token: HANDLE,
    work_dir: &str,
    app_path: &str,
    args: &[String],
) -> Result<(), String> {
    let safe_args = redact_args(args);
    info!(
        "CreateProcessAsUserW prepare begin, method={}, work_dir={}, app_path={}, args={:?}",
        launch_method, work_dir, app_path, safe_args
    );
    let mut environment: *mut c_void = std::ptr::null_mut();
    let env_result = CreateEnvironmentBlock(&mut environment, Some(primary_token), false);
    if let Err(err) = env_result {
        let _ = CloseHandle(primary_token);
        error!(
            "CreateProcessAsUserW prepare CreateEnvironmentBlock failed, method={}, error={}",
            launch_method, err
        );
        return Err(format!("CreateEnvironmentBlock failed: {err}"));
    }
    info!(
        "CreateProcessAsUserW prepare CreateEnvironmentBlock succeeded, method={}, env_ptr={:?}",
        launch_method, environment
    );

    let command = build_command_line(app_path, args);
    let safe_command = build_command_line(app_path, &safe_args);
    info!(
        "CreateProcessAsUserW launching, method={}, command={}, work_dir={}, desktop=WinSta0\\Default",
        launch_method, safe_command, work_dir
    );
    let mut command_w: Vec<u16> = command.encode_utf16().chain(Some(0)).collect();
    let work_dir_w: Vec<u16> = work_dir.encode_utf16().chain(Some(0)).collect();
    let mut desktop_w: Vec<u16> = "WinSta0\\Default".encode_utf16().chain(Some(0)).collect();
    let mut startup_info = STARTUPINFOW::default();
    startup_info.cb = std::mem::size_of::<STARTUPINFOW>() as u32;
    startup_info.lpDesktop = PWSTR(desktop_w.as_mut_ptr());
    startup_info.dwFlags = STARTF_USESHOWWINDOW;
    startup_info.wShowWindow = SW_SHOW.0 as u16;
    let mut process_info = PROCESS_INFORMATION::default();

    let creation_flags =
        NORMAL_PRIORITY_CLASS.0 | CREATE_NEW_CONSOLE.0 | CREATE_UNICODE_ENVIRONMENT.0;
    let result = CreateProcessAsUserW(
        Some(primary_token),
        PCWSTR::null(),
        Some(PWSTR(command_w.as_mut_ptr())),
        None,
        None,
        false,
        PROCESS_CREATION_FLAGS(creation_flags),
        Some(environment),
        PCWSTR(work_dir_w.as_ptr()),
        &startup_info,
        &mut process_info,
    );

    let _ = DestroyEnvironmentBlock(environment);
    let _ = CloseHandle(primary_token);
    if result.is_ok() {
        info!(
            "CreateProcessAsUserW succeeded, method={}, pid={}, tid={}",
            launch_method, process_info.dwProcessId, process_info.dwThreadId
        );
        let _ = CloseHandle(process_info.hThread);
        let _ = CloseHandle(process_info.hProcess);
        Ok(())
    } else {
        let err = result.unwrap_err();
        error!(
            "CreateProcessAsUserW failed, method={}, command={}, work_dir={}, error={}",
            launch_method, safe_command, work_dir, err
        );
        Err(format!("CreateProcessAsUserW failed: {err}"))
    }
}

fn start_process_direct(work_dir: &str, app_path: &str, args: &[String]) -> Result<(), String> {
    let safe_args = redact_args(args);
    info!(
        "direct CreateProcess begin, work_dir={}, app_path={}, args={:?}",
        work_dir, app_path, safe_args
    );
    let mut cmd = std::process::Command::new(app_path);
    cmd.args(args)
        .current_dir(work_dir)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null());
    match cmd.spawn() {
        Ok(child) => {
            info!("direct CreateProcess succeeded, pid={}", child.id());
            // Detach: do not wait; Windows keeps the process after Child drop.
            drop(child);
            Ok(())
        }
        Err(err) => {
            error!("direct CreateProcess failed: {err}");
            Err(format!("direct CreateProcess failed: {err}"))
        }
    }
}

fn build_command_line(app_path: &str, args: &[String]) -> String {
    std::iter::once(escape_arg(app_path))
        .chain(args.iter().map(|arg| escape_arg(arg)))
        .collect::<Vec<_>>()
        .join(" ")
}

fn escape_arg(arg: &str) -> String {
    if !arg.chars().any(|ch| ch.is_whitespace() || ch == '"') {
        return arg.to_string();
    }

    let mut escaped = String::from("\"");
    let mut backslashes = 0usize;
    for ch in arg.chars() {
        if ch == '\\' {
            backslashes += 1;
            continue;
        }
        if ch == '"' {
            escaped.push_str(&"\\".repeat(backslashes * 2 + 1));
            escaped.push('"');
        } else {
            escaped.push_str(&"\\".repeat(backslashes));
            escaped.push(ch);
        }
        backslashes = 0;
    }
    escaped.push_str(&"\\".repeat(backslashes * 2));
    escaped.push('"');
    escaped
}

#[cfg(test)]
mod tests {
    use super::{build_command_line, redact_args};

    #[test]
    fn exit_observer_child_fixture() {
        if std::env::var_os("GAMMARAY_EXIT_OBSERVER_FIXTURE").is_none() { return; }
        use std::io::Read;
        let mut signal = [0_u8; 1];
        let _ = std::io::stdin().read(&mut signal);
        std::process::exit(7);
    }

    #[test]
    fn exit_observer_retains_the_exact_process_after_child_handle_is_dropped() {
        use super::{ProcessManager, WindowsProcessManager};
        use std::io::Write;
        use std::os::windows::process::CommandExt;
        let command = std::env::current_exe().unwrap();
        let mut child = std::process::Command::new(&command)
            .args(["--exact", "windows_process::tests::exit_observer_child_fixture", "--nocapture"])
            .env("GAMMARAY_EXIT_OBSERVER_FIXTURE", "1")
            .creation_flags(windows::Win32::System::Threading::CREATE_NO_WINDOW.0)
            .stdin(std::process::Stdio::piped()).stdout(std::process::Stdio::null()).stderr(std::process::Stdio::null()).spawn().unwrap();
        let manager = WindowsProcessManager::new();
        let observer = manager.observe_exit(child.id(), &command.to_string_lossy()).unwrap();
        assert_eq!(observer.exit_code().unwrap(), None);
        assert!(manager.observe_exit(child.id(), "C:/different.exe").is_err());
        child.stdin.take().unwrap().write_all(b"x").unwrap();
        assert_eq!(child.wait().unwrap().code(), Some(7));
        drop(child);
        assert_eq!(observer.exit_code().unwrap(), Some(7));
        assert_eq!(observer.exit_code().unwrap(), Some(7));
    }

    #[test]
    fn sensitive_process_arguments_are_redacted_for_logs() {
        let args = vec![
            "--app_mode=desktop".to_string(),
            "--service_ipc_token=local-secret".to_string(),
            "--user_session_token".to_string(),
            "session-secret".to_string(),
        ];
        let safe = redact_args(&args);
        let line = build_command_line("px_render.exe", &safe);
        assert!(line.contains("--app_mode=desktop"));
        assert!(line.contains("--service_ipc_token=<redacted>"));
        assert!(line.contains("--user_session_token <redacted>"));
        assert!(!line.contains("local-secret"));
        assert!(!line.contains("session-secret"));
    }
}
