use crate::cms_settings::CmsLiveSettings;
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
    pub cms_pid: Option<u32>,
    pub media_pid: Option<u32>,
    pub media_managed: bool,
}

impl LocalProcessState {
    pub fn cms_running(self) -> bool {
        self.cms_pid.is_some()
    }

    pub fn media_running(self) -> bool {
        self.media_pid.is_some()
    }
}

/// Owns only the programs deployed beside this panel executable.  The exact
/// executable paths are intentional: a separately installed CMS/ZLM process
/// must never be stopped by the local panel.
pub struct CmsProcessManager {
    cms_exe: PathBuf,
    media_exe: PathBuf,
    turn_exe: PathBuf,
    media_managed: bool,
    lifecycle: AtomicU8,
    operation_lock: Mutex<()>,
}

impl CmsProcessManager {
    pub fn for_current_exe(live: &CmsLiveSettings) -> Result<Self, String> {
        let cms_exe = std::env::current_exe()
            .map_err(|error| format!("cannot determine CMS executable: {error}"))?;
        let directory = cms_exe
            .parent()
            .ok_or_else(|| "CMS executable has no parent directory".to_string())?;
        Ok(Self {
            media_exe: directory.join("px_media.exe"),
            turn_exe: directory.join("px_turn.exe"),
            cms_exe,
            // `auto_start_media_server` only controls startup. When the media
            // URL is local, panel exit must always clean up the adjacent
            // px_media.exe as requested, even if it was started manually.
            media_managed: crate::media_sidecar::is_local_sidecar_url(&live.media_server_url),
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
        if !state.cms_running() && self.lifecycle.load(Ordering::SeqCst) == RUNNING {
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

    /// Stops the local CMS server and the local sidecar, then permanently
    /// disables reconciliation so neither is resurrected during panel exit.
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
        let mut command = Command::new(&self.cms_exe);
        command.arg("-r=server");
        if let Some(directory) = self.cms_exe.parent() {
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
            .map_err(|error| format!("start CMS server failed: {error}"))
    }

    fn stop_local_processes(&self) {
        let mut system = System::new_all();
        system.refresh_processes(ProcessesToUpdate::All, true);
        let (cms_pids, media_pids, turn_pids) = self.matching_pids_from(&system);

        // Stop the server first, then the media sidecar. The former may still
        // be writing to ZLM while it exits, but both are guaranteed gone before
        // the panel runtime closes.
        for pid in cms_pids {
            kill_pid(&system, pid);
        }
        if self.media_managed {
            for pid in media_pids {
                kill_pid(&system, pid);
            }
        }
        for pid in turn_pids {
            kill_pid(&system, pid);
        }

        // `kill_with` is asynchronous on Windows. Wait briefly and issue a
        // second kill if a process has not released yet; restart must not race
        // with the old listener still owning the CMS ports.
        for _ in 0..20 {
            std::thread::sleep(std::time::Duration::from_millis(100));
            let mut refreshed = System::new_all();
            refreshed.refresh_processes(ProcessesToUpdate::All, true);
            let (cms_pids, media_pids, turn_pids) = self.matching_pids_from(&refreshed);
            if cms_pids.is_empty()
                && (!self.media_managed || media_pids.is_empty())
                && turn_pids.is_empty()
            {
                break;
            }
            for pid in cms_pids {
                kill_pid(&refreshed, pid);
            }
            if self.media_managed {
                for pid in media_pids {
                    kill_pid(&refreshed, pid);
                }
            }
            for pid in turn_pids {
                kill_pid(&refreshed, pid);
            }
        }
    }

    fn snapshot_from(&self, system: &System) -> LocalProcessState {
        let (cms_pids, media_pids, _) = self.matching_pids_from(system);
        let mut state = LocalProcessState {
            media_managed: self.media_managed,
            ..Default::default()
        };
        state.cms_pid = cms_pids.into_iter().next();
        state.media_pid = media_pids.into_iter().next();
        state
    }

    fn matching_pids_from(&self, system: &System) -> (Vec<u32>, Vec<u32>, Vec<u32>) {
        let own_pid = std::process::id();
        let mut cms_pids = Vec::new();
        let mut media_pids = Vec::new();
        let mut turn_pids = Vec::new();
        for (pid, process) in system.processes() {
            if pid.as_u32() == own_pid {
                continue;
            }
            let Some(exe) = process.exe() else {
                continue;
            };
            if same_path(exe, &self.cms_exe) && is_server_command(process.cmd()) {
                cms_pids.push(pid.as_u32());
            } else if self.media_managed && same_path(exe, &self.media_exe) {
                media_pids.push(pid.as_u32());
            } else if same_path(exe, &self.turn_exe) {
                turn_pids.push(pid.as_u32());
            }
        }
        (cms_pids, media_pids, turn_pids)
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
    args.iter().any(|arg| arg == "-r=server" || arg == "--running-mode=server")
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
            OsString::from("px_cms.exe"),
            OsString::from("-r=server"),
        ]));
        assert!(!is_server_command(&[OsString::from("px_cms.exe")]));
    }

    #[test]
    fn recognizes_all_supported_server_argument_forms() {
        assert!(is_server_command(&[
            OsString::from("px_cms.exe"),
            OsString::from("--running-mode=server"),
        ]));
        assert!(is_server_command(&[
            OsString::from("px_cms.exe"),
            OsString::from("--running-mode"),
            OsString::from("server"),
        ]));
    }
}
