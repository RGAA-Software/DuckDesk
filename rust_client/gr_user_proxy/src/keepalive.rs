//! Keepalive for `GammaRay.exe` and `GammaRaySysInfo.exe` (merged from gr_guard).
//!
//! Every `KEEPALIVE_POLL_INTERVAL` the running processes are enumerated via a
//! Toolhelp snapshot; missing targets are restarted. The panel is started
//! through the `GammaRay_Panel_Start` scheduled task (elevated, no UAC) with a
//! direct spawn as fallback; SysInfo is spawned directly.

use std::path::{Path, PathBuf};
use std::process::Command;

use std::os::windows::process::CommandExt;

use tracing::{error, info, warn};
use windows::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
use windows::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};
use windows::Win32::System::Threading::{CREATE_NO_WINDOW, DETACHED_PROCESS};

use crate::config::{
    sibling_exe_path, KEEPALIVE_POLL_INTERVAL, PANEL_EXE_NAME, PANEL_TASK_NAME, SYSINFO_EXE_NAME,
};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProcessEntry {
    pub exe_name: String,
}

pub trait ProcessLister: Send + Sync + 'static {
    fn list_processes(&self) -> Result<Vec<ProcessEntry>, String>;
}

#[derive(Default)]
pub struct ToolhelpProcessLister;

impl ProcessLister for ToolhelpProcessLister {
    fn list_processes(&self) -> Result<Vec<ProcessEntry>, String> {
        let snapshot = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) }
            .map_err(|err| err.to_string())?;
        if snapshot == INVALID_HANDLE_VALUE {
            return Err("CreateToolhelp32Snapshot returned invalid handle".to_string());
        }

        let mut entries = Vec::new();
        let mut process_entry = PROCESSENTRY32W::default();
        process_entry.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;

        let first = unsafe { Process32FirstW(snapshot, &mut process_entry) };
        if let Err(err) = first {
            unsafe {
                let _ = CloseHandle(snapshot);
            }
            return Err(format!("Process32FirstW failed: {err}"));
        }

        loop {
            entries.push(ProcessEntry {
                exe_name: utf16z_to_string(&process_entry.szExeFile),
            });
            if unsafe { Process32NextW(snapshot, &mut process_entry) }.is_err() {
                break;
            }
        }

        unsafe {
            let _ = CloseHandle(snapshot);
        }

        Ok(entries)
    }
}

fn utf16z_to_string(buffer: &[u16]) -> String {
    let end = buffer.iter().position(|value| *value == 0).unwrap_or(buffer.len());
    String::from_utf16_lossy(&buffer[..end])
}

pub trait ProcessSpawner: Send + Sync + 'static {
    fn spawn_path(&self, exe_path: &Path) -> Result<(), String>;

    /// Start the panel via the registered `GammaRay_Panel_Start` scheduled
    /// task (keeps the elevated-start semantics without a UAC prompt); fall
    /// back to a direct spawn when the task run fails.
    fn start_panel(&self, exe_path: &Path) -> Result<(), String> {
        match run_panel_scheduled_task() {
            Ok(()) => Ok(()),
            Err(err) => {
                warn!(
                    "schtasks /Run /TN {} failed: {err}, falling back to direct spawn",
                    PANEL_TASK_NAME
                );
                self.spawn_path(exe_path)
            }
        }
    }
}

#[derive(Default)]
pub struct WindowsProcessSpawner;

impl ProcessSpawner for WindowsProcessSpawner {
    fn spawn_path(&self, exe_path: &Path) -> Result<(), String> {
        let work_dir = exe_path
            .parent()
            .ok_or_else(|| format!("exe path has no parent: {}", exe_path.display()))?;
        let mut command = Command::new(exe_path);
        command.current_dir(work_dir);
        command.creation_flags(detached_spawn_flags());
        command
            .spawn()
            .map(|_| ())
            .map_err(|err| format!("spawn {} failed: {err}", exe_path.display()))
    }
}

fn detached_spawn_flags() -> u32 {
    DETACHED_PROCESS.0
}

fn run_panel_scheduled_task() -> Result<(), String> {
    let status = Command::new("schtasks")
        .args(["/Run", "/TN", PANEL_TASK_NAME])
        .creation_flags(CREATE_NO_WINDOW.0)
        .status()
        .map_err(|err| format!("spawn schtasks failed: {err}"))?;
    if status.success() {
        info!("panel start requested via scheduled task {}", PANEL_TASK_NAME);
        Ok(())
    } else {
        Err(format!("schtasks exited with {status}"))
    }
}

pub fn panel_exe_path(app_dir: &Path) -> PathBuf {
    sibling_exe_path(app_dir, PANEL_EXE_NAME)
}

pub fn sysinfo_exe_path(app_dir: &Path) -> PathBuf {
    sibling_exe_path(app_dir, SYSINFO_EXE_NAME)
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct KeepaliveTickOutcome {
    pub panel_alive: bool,
    pub sysinfo_alive: bool,
    pub started_panel: bool,
    pub started_sysinfo: bool,
}

/// Keepalive loop state. The panel is started via `schtasks /Run`, which is
/// asynchronous — the process may take several seconds to appear. Without a
/// cooldown, every 5s tick would re-trigger the task and launch multiple
/// panel instances (racing the SharedPreference/LevelDB LOCK and showing the
/// "SharedPreference init failed" dialog).
#[derive(Debug, Default)]
pub struct KeepaliveState {
    panel_respawn_not_before: Option<std::time::Instant>,
    sysinfo_respawn_not_before: Option<std::time::Instant>,
}

pub fn has_process_named(processes: &[ProcessEntry], exe_name: &str) -> bool {
    processes
        .iter()
        .any(|process| process.exe_name.eq_ignore_ascii_case(exe_name))
}

pub fn run_keepalive_tick(
    lister: &dyn ProcessLister,
    spawner: &dyn ProcessSpawner,
    app_dir: &Path,
    state: &mut KeepaliveState,
) -> Result<KeepaliveTickOutcome, String> {
    let processes = lister.list_processes()?;
    let panel_alive = has_process_named(&processes, PANEL_EXE_NAME);
    let sysinfo_alive = has_process_named(&processes, SYSINFO_EXE_NAME);
    let mut outcome = KeepaliveTickOutcome {
        panel_alive,
        sysinfo_alive,
        started_panel: false,
        started_sysinfo: false,
    };

    if panel_alive {
        state.panel_respawn_not_before = None;
    } else if state
        .panel_respawn_not_before
        .is_some_and(|not_before| std::time::Instant::now() < not_before)
    {
        info!("panel start already triggered, waiting for it to appear (cooldown)");
    } else {
        warn!("GammaRay.exe missing, starting");
        let path = panel_exe_path(app_dir);
        info!("starting panel, path={}", path.display());
        spawner.start_panel(&path)?;
        state.panel_respawn_not_before =
            Some(std::time::Instant::now() + crate::config::PANEL_RESPAWN_COOLDOWN);
        outcome.started_panel = true;
    }

    if sysinfo_alive {
        state.sysinfo_respawn_not_before = None;
    } else if state
        .sysinfo_respawn_not_before
        .is_some_and(|not_before| std::time::Instant::now() < not_before)
    {
        info!("sysinfo start already triggered, waiting for it to appear (cooldown)");
    } else {
        warn!("GammaRaySysInfo.exe missing, starting");
        let path = sysinfo_exe_path(app_dir);
        info!("starting sysinfo, path={}", path.display());
        spawner.spawn_path(&path)?;
        state.sysinfo_respawn_not_before =
            Some(std::time::Instant::now() + crate::config::SYSINFO_RESPAWN_COOLDOWN);
        outcome.started_sysinfo = true;
    }

    info!(
        "keepalive tick complete, panel_alive={}, sysinfo_alive={}, started_panel={}, started_sysinfo={}",
        outcome.panel_alive,
        outcome.sysinfo_alive,
        outcome.started_panel,
        outcome.started_sysinfo
    );

    Ok(outcome)
}

pub fn run_initial_check(
    lister: &dyn ProcessLister,
    spawner: &dyn ProcessSpawner,
    app_dir: &Path,
) -> Result<bool, String> {
    let processes = lister.list_processes()?;
    if has_process_named(&processes, SYSINFO_EXE_NAME) {
        return Ok(false);
    }

    warn!("GammaRaySysInfo.exe missing during bootstrap, starting");
    let path = sysinfo_exe_path(app_dir);
    info!("starting sysinfo, path={}", path.display());
    spawner.spawn_path(&path)?;
    Ok(true)
}

/// Spawn the keepalive loop on the tokio runtime: one initial check, then a
/// tick every `KEEPALIVE_POLL_INTERVAL`.
pub fn spawn_keepalive_loop(app_dir: PathBuf) -> tokio::task::JoinHandle<()> {
    tokio::spawn(async move {
        info!("keepalive loop started, app_dir={}", app_dir.display());
        let lister = ToolhelpProcessLister;
        let spawner = WindowsProcessSpawner;
        if let Err(err) = run_initial_check(&lister, &spawner, &app_dir) {
            error!("initial sysinfo check failed: {err}");
        }
        // First tick is delayed by the poll interval (plain `interval` fires
        // immediately): the initial check above may have just spawned SysInfo,
        // and an immediate tick could take a process snapshot before it shows
        // up and spawn a duplicate.
        let mut interval = tokio::time::interval_at(
            tokio::time::Instant::now() + KEEPALIVE_POLL_INTERVAL,
            KEEPALIVE_POLL_INTERVAL,
        );
        let mut state = KeepaliveState::default();
        loop {
            interval.tick().await;
            if let Err(err) = run_keepalive_tick(&lister, &spawner, &app_dir, &mut state) {
                error!("keepalive tick failed: {err}");
            }
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    #[derive(Clone)]
    struct StaticLister {
        processes: Vec<ProcessEntry>,
    }

    impl ProcessLister for StaticLister {
        fn list_processes(&self) -> Result<Vec<ProcessEntry>, String> {
            Ok(self.processes.clone())
        }
    }

    #[derive(Default)]
    struct RecordingSpawner {
        paths: Arc<Mutex<Vec<PathBuf>>>,
    }

    impl ProcessSpawner for RecordingSpawner {
        fn spawn_path(&self, exe_path: &Path) -> Result<(), String> {
            self.paths.lock().expect("lock").push(exe_path.to_path_buf());
            Ok(())
        }

        fn start_panel(&self, exe_path: &Path) -> Result<(), String> {
            self.paths.lock().expect("lock").push(exe_path.to_path_buf());
            Ok(())
        }
    }

    fn entry(name: &str) -> ProcessEntry {
        ProcessEntry {
            exe_name: name.to_string(),
        }
    }

    #[test]
    fn utf16z_to_string_stops_at_zero_terminator() {
        let data = ['G' as u16, 'R' as u16, 0, 'X' as u16];
        assert_eq!(utf16z_to_string(&data), "GR");
    }

    #[test]
    fn finds_panel_case_insensitively() {
        assert!(has_process_named(&[entry("gammaray.exe")], PANEL_EXE_NAME));
    }

    #[test]
    fn finds_sysinfo_case_insensitively() {
        assert!(has_process_named(
            &[entry("gammaraysysinfo.exe")],
            SYSINFO_EXE_NAME
        ));
    }

    #[test]
    fn does_not_start_when_all_targets_exist() {
        let lister = StaticLister {
            processes: vec![entry(PANEL_EXE_NAME), entry(SYSINFO_EXE_NAME)],
        };
        let spawner = RecordingSpawner::default();
        let outcome = run_keepalive_tick(
            &lister,
            &spawner,
            Path::new("D:/GammaRay"),
            &mut KeepaliveState::default(),
        )
        .expect("tick");
        assert_eq!(
            outcome,
            KeepaliveTickOutcome {
                panel_alive: true,
                sysinfo_alive: true,
                started_panel: false,
                started_sysinfo: false,
            }
        );
        assert!(spawner.paths.lock().expect("lock").is_empty());
    }

    #[test]
    fn starts_panel_when_missing() {
        let lister = StaticLister {
            processes: vec![entry(SYSINFO_EXE_NAME)],
        };
        let spawner = RecordingSpawner::default();
        let outcome = run_keepalive_tick(
            &lister,
            &spawner,
            Path::new("D:/GammaRay"),
            &mut KeepaliveState::default(),
        )
        .expect("tick");
        assert!(!outcome.panel_alive);
        assert!(outcome.started_panel);
        assert_eq!(
            spawner.paths.lock().expect("lock").as_slice(),
            &[PathBuf::from("D:/GammaRay/GammaRay.exe")]
        );
    }

    #[test]
    fn starts_sysinfo_when_missing() {
        let lister = StaticLister {
            processes: vec![entry(PANEL_EXE_NAME)],
        };
        let spawner = RecordingSpawner::default();
        let outcome = run_keepalive_tick(
            &lister,
            &spawner,
            Path::new("D:/GammaRay"),
            &mut KeepaliveState::default(),
        )
        .expect("tick");
        assert!(!outcome.sysinfo_alive);
        assert!(!outcome.started_panel);
        assert!(outcome.started_sysinfo);
        assert_eq!(
            spawner.paths.lock().expect("lock").as_slice(),
            &[PathBuf::from("D:/GammaRay/GammaRaySysInfo.exe")]
        );
    }

    #[test]
    fn starts_all_targets_when_all_missing() {
        let lister = StaticLister { processes: vec![] };
        let spawner = RecordingSpawner::default();
        let outcome = run_keepalive_tick(
            &lister,
            &spawner,
            Path::new("D:/GammaRay"),
            &mut KeepaliveState::default(),
        )
        .expect("tick");
        assert!(outcome.started_panel);
        assert!(outcome.started_sysinfo);
        assert_eq!(
            spawner.paths.lock().expect("lock").as_slice(),
            &[
                PathBuf::from("D:/GammaRay/GammaRay.exe"),
                PathBuf::from("D:/GammaRay/GammaRaySysInfo.exe"),
            ]
        );
    }

    #[test]
    fn panel_respawn_is_suppressed_during_cooldown() {
        // schtasks /Run 是异步的,panel 可能要几秒才出现;冷却期内不得重复拉起,
        // 否则会起多个实例抢 godesk.dat 的 LOCK。
        let lister = StaticLister { processes: vec![] };
        let spawner = RecordingSpawner::default();
        let mut state = KeepaliveState::default();
        let first = run_keepalive_tick(&lister, &spawner, Path::new("D:/GammaRay"), &mut state)
            .expect("tick 1");
        assert!(first.started_panel);
        let second =
            run_keepalive_tick(&lister, &spawner, Path::new("D:/GammaRay"), &mut state)
                .expect("tick 2");
        assert!(!second.started_panel, "cooldown must suppress respawn");
        let panel_spawns = spawner
            .paths
            .lock()
            .expect("lock")
            .iter()
            .filter(|p| p.ends_with(PANEL_EXE_NAME))
            .count();
        assert_eq!(panel_spawns, 1, "panel must be started only once");
    }

    #[test]
    fn sysinfo_respawn_is_suppressed_during_cooldown() {
        // SysInfo 刚被拉起时可能还没出现在进程快照里(或直接启动即崩),
        // 冷却期内不得重复拉起。
        let lister = StaticLister {
            processes: vec![entry(PANEL_EXE_NAME)],
        };
        let spawner = RecordingSpawner::default();
        let mut state = KeepaliveState::default();
        let first = run_keepalive_tick(&lister, &spawner, Path::new("D:/GammaRay"), &mut state)
            .expect("tick 1");
        assert!(first.started_sysinfo);
        let second =
            run_keepalive_tick(&lister, &spawner, Path::new("D:/GammaRay"), &mut state)
                .expect("tick 2");
        assert!(!second.started_sysinfo, "cooldown must suppress respawn");
        let sysinfo_spawns = spawner
            .paths
            .lock()
            .expect("lock")
            .iter()
            .filter(|p| p.ends_with(SYSINFO_EXE_NAME))
            .count();
        assert_eq!(sysinfo_spawns, 1, "sysinfo must be started only once");
    }

    #[test]
    fn sysinfo_cooldown_cleared_once_sysinfo_alive() {
        let spawner = RecordingSpawner::default();
        let mut state = KeepaliveState::default();
        let missing = StaticLister {
            processes: vec![entry(PANEL_EXE_NAME)],
        };
        run_keepalive_tick(&missing, &spawner, Path::new("D:/GammaRay"), &mut state)
            .expect("tick 1");
        assert!(state.sysinfo_respawn_not_before.is_some());
        let alive = StaticLister {
            processes: vec![entry(PANEL_EXE_NAME), entry(SYSINFO_EXE_NAME)],
        };
        run_keepalive_tick(&alive, &spawner, Path::new("D:/GammaRay"), &mut state)
            .expect("tick 2");
        assert!(state.sysinfo_respawn_not_before.is_none());
    }

    #[test]
    fn panel_cooldown_cleared_once_panel_alive() {
        let spawner = RecordingSpawner::default();
        let mut state = KeepaliveState::default();
        let missing = StaticLister { processes: vec![] };
        run_keepalive_tick(&missing, &spawner, Path::new("D:/GammaRay"), &mut state)
            .expect("tick 1");
        assert!(state.panel_respawn_not_before.is_some());
        let alive = StaticLister {
            processes: vec![entry(PANEL_EXE_NAME)],
        };
        run_keepalive_tick(&alive, &spawner, Path::new("D:/GammaRay"), &mut state)
            .expect("tick 2");
        assert!(state.panel_respawn_not_before.is_none());
    }

    #[test]
    fn similar_process_names_do_not_count_as_panel() {
        assert!(!has_process_named(
            &[entry("GammaRayGuard.exe")],
            PANEL_EXE_NAME
        ));
    }

    #[test]
    fn initial_check_only_starts_sysinfo() {
        let lister = StaticLister { processes: vec![] };
        let spawner = RecordingSpawner::default();
        let started = run_initial_check(&lister, &spawner, Path::new("D:/GammaRay"))
            .expect("initial check");
        assert!(started);
        assert_eq!(
            spawner.paths.lock().expect("lock").as_slice(),
            &[PathBuf::from("D:/GammaRay/GammaRaySysInfo.exe")]
        );
    }

    #[test]
    fn initial_check_skips_when_sysinfo_exists() {
        let lister = StaticLister {
            processes: vec![entry(SYSINFO_EXE_NAME)],
        };
        let spawner = RecordingSpawner::default();
        let started = run_initial_check(&lister, &spawner, Path::new("D:/GammaRay"))
            .expect("initial check");
        assert!(!started);
        assert!(spawner.paths.lock().expect("lock").is_empty());
    }

    #[test]
    fn panel_exe_path_uses_app_directory() {
        assert_eq!(
            panel_exe_path(Path::new("D:/GammaRay")),
            PathBuf::from("D:/GammaRay/GammaRay.exe")
        );
    }

    #[test]
    fn sysinfo_exe_path_uses_app_directory() {
        assert_eq!(
            sysinfo_exe_path(Path::new("D:/GammaRay")),
            PathBuf::from("D:/GammaRay/GammaRaySysInfo.exe")
        );
    }

    #[test]
    fn detached_spawn_flags_do_not_include_create_new_console() {
        assert_eq!(detached_spawn_flags(), DETACHED_PROCESS.0);
    }
}
