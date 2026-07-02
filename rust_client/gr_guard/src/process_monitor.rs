use std::path::Path;

use tracing::{info, warn};

use crate::config::{PANEL_EXE_NAME, SYSINFO_EXE_NAME};
use crate::process_spawn::{spawn_panel, spawn_sysinfo, ProcessSpawner};

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ProcessEntry {
    pub exe_name: String,
}

pub trait ProcessLister: Send + Sync + 'static {
    fn list_processes(&self) -> Result<Vec<ProcessEntry>, String>;
}

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct GuardTickOutcome {
    pub panel_alive: bool,
    pub sysinfo_alive: bool,
    pub started_panel: bool,
    pub started_sysinfo: bool,
}

pub fn has_process_named(processes: &[ProcessEntry], exe_name: &str) -> bool {
    processes
        .iter()
        .any(|process| process.exe_name.eq_ignore_ascii_case(exe_name))
}

pub fn run_guard_tick(
    lister: &dyn ProcessLister,
    spawner: &dyn ProcessSpawner,
    app_dir: &Path,
) -> Result<GuardTickOutcome, String> {
    let processes = lister.list_processes()?;
    let panel_alive = has_process_named(&processes, PANEL_EXE_NAME);
    let sysinfo_alive = has_process_named(&processes, SYSINFO_EXE_NAME);
    let mut outcome = GuardTickOutcome {
        panel_alive,
        sysinfo_alive,
        started_panel: false,
        started_sysinfo: false,
    };

    if !panel_alive {
        warn!("GammaRay.exe missing, starting");
        spawn_panel(spawner, app_dir)?;
        outcome.started_panel = true;
    }

    if !sysinfo_alive {
        warn!("GammaRaySysInfo.exe missing, starting");
        spawn_sysinfo(spawner, app_dir)?;
        outcome.started_sysinfo = true;
    }

    info!(
        "guard tick complete, panel_alive={}, sysinfo_alive={}, started_panel={}, started_sysinfo={}",
        outcome.panel_alive,
        outcome.sysinfo_alive,
        outcome.started_panel,
        outcome.started_sysinfo
    );

    Ok(outcome)
}

pub fn run_initial_sysinfo_check(
    lister: &dyn ProcessLister,
    spawner: &dyn ProcessSpawner,
    app_dir: &Path,
) -> Result<bool, String> {
    let processes = lister.list_processes()?;
    if has_process_named(&processes, SYSINFO_EXE_NAME) {
        return Ok(false);
    }

    warn!("GammaRaySysInfo.exe missing during bootstrap, starting");
    spawn_sysinfo(spawner, app_dir)?;
    Ok(true)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::process_spawn::ProcessSpawner;
    use std::path::PathBuf;
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
    }

    fn entry(name: &str) -> ProcessEntry {
        ProcessEntry {
            exe_name: name.to_string(),
        }
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
        let outcome = run_guard_tick(&lister, &spawner, Path::new("D:/GammaRay")).expect("tick");
        assert_eq!(
            outcome,
            GuardTickOutcome {
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
        let outcome = run_guard_tick(&lister, &spawner, Path::new("D:/GammaRay")).expect("tick");
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
        let outcome = run_guard_tick(&lister, &spawner, Path::new("D:/GammaRay")).expect("tick");
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
        let outcome = run_guard_tick(&lister, &spawner, Path::new("D:/GammaRay")).expect("tick");
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
        let started = run_initial_sysinfo_check(&lister, &spawner, Path::new("D:/GammaRay"))
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
        let started = run_initial_sysinfo_check(&lister, &spawner, Path::new("D:/GammaRay"))
            .expect("initial check");
        assert!(!started);
        assert!(spawner.paths.lock().expect("lock").is_empty());
    }
}
