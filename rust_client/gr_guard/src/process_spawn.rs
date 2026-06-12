use std::path::{Path, PathBuf};
use std::process::Command;

use std::os::windows::process::CommandExt;

use tracing::info;
use windows::Win32::System::Threading::DETACHED_PROCESS;

use crate::config::{sibling_exe_path, PANEL_EXE_NAME, SYSINFO_EXE_NAME};

pub trait ProcessSpawner: Send + Sync + 'static {
    fn spawn_path(&self, exe_path: &Path) -> Result<(), String>;
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
    // `CREATE_NEW_CONSOLE | DETACHED_PROCESS` is an invalid combination on Windows and
    // causes ERROR_INVALID_PARAMETER (87). The C++ implementation uses Qt detached launch
    // semantics, so using only DETACHED_PROCESS is the closest low-level equivalent here.
    DETACHED_PROCESS.0
}

pub fn panel_exe_path(app_dir: &Path) -> PathBuf {
    sibling_exe_path(app_dir, PANEL_EXE_NAME)
}

pub fn sysinfo_exe_path(app_dir: &Path) -> PathBuf {
    sibling_exe_path(app_dir, SYSINFO_EXE_NAME)
}

pub fn spawn_panel(spawner: &dyn ProcessSpawner, app_dir: &Path) -> Result<(), String> {
    let path = panel_exe_path(app_dir);
    info!("starting panel, path={}", path.display());
    spawner.spawn_path(&path)
}

pub fn spawn_sysinfo(spawner: &dyn ProcessSpawner, app_dir: &Path) -> Result<(), String> {
    let path = sysinfo_exe_path(app_dir);
    info!("starting sysinfo, path={}", path.display());
    spawner.spawn_path(&path)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

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

    #[test]
    fn panel_exe_path_uses_guard_directory() {
        assert_eq!(
            panel_exe_path(Path::new("D:/GammaRay")),
            PathBuf::from("D:/GammaRay/GammaRay.exe")
        );
    }

    #[test]
    fn sysinfo_exe_path_uses_guard_directory() {
        assert_eq!(
            sysinfo_exe_path(Path::new("D:/GammaRay")),
            PathBuf::from("D:/GammaRay/GammaRaySysInfo.exe")
        );
    }

    #[test]
    fn spawn_panel_delegates_to_spawner() {
        let spawner = RecordingSpawner::default();
        spawn_panel(&spawner, Path::new("D:/GammaRay")).expect("spawn panel");
        assert_eq!(
            spawner.paths.lock().expect("lock").as_slice(),
            &[PathBuf::from("D:/GammaRay/GammaRay.exe")]
        );
    }

    #[test]
    fn spawn_sysinfo_delegates_to_spawner() {
        let spawner = RecordingSpawner::default();
        spawn_sysinfo(&spawner, Path::new("D:/GammaRay")).expect("spawn sysinfo");
        assert_eq!(
            spawner.paths.lock().expect("lock").as_slice(),
            &[PathBuf::from("D:/GammaRay/GammaRaySysInfo.exe")]
        );
    }

    #[test]
    fn detached_spawn_flags_do_not_include_create_new_console() {
        assert_eq!(detached_spawn_flags(), DETACHED_PROCESS.0);
    }
}
