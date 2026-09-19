use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::Mutex;
use sysinfo::{Pid, ProcessesToUpdate, Signal, System};

const RUNNING: u8 = 0;
const EXITING: u8 = 1;
const RESTARTING: u8 = 2;

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct LocalProcessState {
    pub console_pid: Option<u32>,
}

impl LocalProcessState {
    pub fn console_running(self) -> bool {
        self.console_pid.is_some()
    }
}

/// Owns only the server process launched from the executable beside this panel.
/// The exact path and server-mode arguments prevent the panel from stopping an
/// unrelated Console process.
pub struct ConsoleProcessManager {
    console_exe: PathBuf,
    lifecycle: AtomicU8,
    operation_lock: Mutex<()>,
}

impl ConsoleProcessManager {
    pub fn for_current_exe() -> Result<Self, String> {
        let console_exe = std::env::current_exe()
            .map_err(|error| format!("cannot determine Console executable: {error}"))?;
        Ok(Self {
            console_exe,
            lifecycle: AtomicU8::new(RUNNING),
            operation_lock: Mutex::new(()),
        })
    }

    pub fn snapshot(&self) -> LocalProcessState {
        let mut system = System::new_all();
        system.refresh_processes(ProcessesToUpdate::All, true);
        self.snapshot_from(&system)
    }

    pub fn reconcile(&self) -> Result<LocalProcessState, String> {
        let _guard = self
            .operation_lock
            .lock()
            .expect("process manager lock poisoned");
        let state = self.snapshot();
        if !state.console_running() && self.lifecycle.load(Ordering::SeqCst) == RUNNING {
            self.spawn_server()?;
        }
        Ok(self.snapshot())
    }

    pub fn restart(&self) -> Result<LocalProcessState, String> {
        let _guard = self
            .operation_lock
            .lock()
            .expect("process manager lock poisoned");
        self.lifecycle.store(RESTARTING, Ordering::SeqCst);
        self.stop_local_processes();
        self.lifecycle.store(RUNNING, Ordering::SeqCst);
        self.spawn_server()?;
        Ok(self.snapshot())
    }

    /// Stops the local Console server, then permanently disables reconciliation
    /// so it is not resurrected during panel exit.
    pub fn stop_for_exit(&self) -> LocalProcessState {
        let _guard = self
            .operation_lock
            .lock()
            .expect("process manager lock poisoned");
        self.lifecycle.store(EXITING, Ordering::SeqCst);
        self.stop_local_processes();
        self.snapshot()
    }

    fn spawn_server(&self) -> Result<(), String> {
        let mut command = Command::new(&self.console_exe);
        command.arg("-r=server");
        if let Some(directory) = self.console_exe.parent() {
            command.current_dir(directory);
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            const CREATE_NO_WINDOW: u32 = 0x0800_0000;
            command.creation_flags(CREATE_NO_WINDOW);
        }
        command
            .spawn()
            .map(|_| ())
            .map_err(|error| format!("start Console server failed: {error}"))
    }

    fn stop_local_processes(&self) {
        let mut system = System::new_all();
        system.refresh_processes(ProcessesToUpdate::All, true);
        let console_pids = self.matching_pids_from(&system);
        for pid in console_pids {
            kill_pid(&system, pid);
        }

        // `kill_with` is asynchronous on Windows. Wait briefly and issue a
        // second kill if a process has not released yet; restart must not race
        // with the old listener still owning the Console ports.
        for _ in 0..20 {
            std::thread::sleep(std::time::Duration::from_millis(100));
            let mut refreshed = System::new_all();
            refreshed.refresh_processes(ProcessesToUpdate::All, true);
            let console_pids = self.matching_pids_from(&refreshed);
            if console_pids.is_empty() {
                break;
            }
            for pid in console_pids {
                kill_pid(&refreshed, pid);
            }
        }
    }

    fn snapshot_from(&self, system: &System) -> LocalProcessState {
        let console_pids = self.matching_pids_from(system);
        let mut state = LocalProcessState::default();
        state.console_pid = console_pids.into_iter().next();
        state
    }

    fn matching_pids_from(&self, system: &System) -> Vec<u32> {
        let own_pid = std::process::id();
        let mut console_pids = Vec::new();
        for (pid, process) in system.processes() {
            if pid.as_u32() == own_pid {
                continue;
            }
            let Some(exe) = process.exe() else {
                continue;
            };
            if same_path(exe, &self.console_exe) && is_server_command(process.cmd()) {
                console_pids.push(pid.as_u32());
            }
        }
        console_pids
    }
}

fn kill_pid(system: &System, raw_pid: u32) {
    if let Some(process) = system.process(Pid::from_u32(raw_pid)) {
        let _ = process.kill_with(Signal::Kill);
    }
}

fn same_path(left: &Path, right: &Path) -> bool {
    left.to_string_lossy()
        .eq_ignore_ascii_case(&right.to_string_lossy())
}

fn is_server_command(args: &[std::ffi::OsString]) -> bool {
    let args = args
        .iter()
        .map(|arg| arg.to_string_lossy())
        .collect::<Vec<_>>();
    args.iter()
        .any(|arg| arg == "-r=server" || arg == "--running-mode=server")
        || args
            .windows(2)
            .any(|pair| pair[0] == "--running-mode" && pair[1] == "server")
}

#[cfg(test)]
mod tests {
    use super::is_server_command;
    use std::ffi::OsString;

    #[test]
    fn recognizes_only_server_mode() {
        assert!(is_server_command(&[
            OsString::from("px_console.exe"),
            OsString::from("-r=server"),
        ]));
        assert!(!is_server_command(&[OsString::from("px_console.exe")]));
    }

    #[test]
    fn recognizes_all_supported_server_argument_forms() {
        assert!(is_server_command(&[
            OsString::from("px_console.exe"),
            OsString::from("--running-mode=server"),
        ]));
        assert!(is_server_command(&[
            OsString::from("px_console.exe"),
            OsString::from("--running-mode"),
            OsString::from("server"),
        ]));
    }
}
