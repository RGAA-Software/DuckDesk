use std::path::{Path, PathBuf};
use std::time::Duration;

const DEFAULT_APP_NAME: &str = "GoDesk";

pub const GUARD_LOG_DIR: &str = "gr_logs";
pub const GUARD_DATA_DIR: &str = "gr_data";
pub const GUARD_LOG_FILE: &str = "godesk_guard.log";
pub const GUARD_LOCK_NAME: &str = "GammaRayGuard.Singleton";
pub const GUARD_TASK_NAME: &str = "GammaRay_Guard_Start";
pub const GUARD_TASK_AUTHOR: &str = "GR";
pub const PANEL_EXE_NAME: &str = "GammaRay.exe";
pub const SYSINFO_EXE_NAME: &str = "GammaRaySysInfo.exe";
pub const PANEL_HOST: &str = "127.0.0.1";
pub const PANEL_PORT: u16 = 20369;
pub const PANEL_PATH: &str = "/panel/renderer?from=guard";
pub const GUARD_POLL_INTERVAL: Duration = Duration::from_secs(5);

pub fn public_share_dir() -> PathBuf {
    match std::env::var_os("PUBLIC") {
        Some(value) => PathBuf::from(value),
        None => PathBuf::from("C:/Users/Public"),
    }
}

pub fn app_shared_root() -> PathBuf {
    public_share_dir().join(DEFAULT_APP_NAME)
}

pub fn guard_data_root() -> PathBuf {
    app_shared_root().join(GUARD_DATA_DIR)
}

pub fn guard_log_root() -> PathBuf {
    app_shared_root().join(GUARD_LOG_DIR)
}

pub fn guard_log_file() -> PathBuf {
    guard_log_root().join(GUARD_LOG_FILE)
}

pub fn panel_ws_url() -> String {
    format!("ws://{PANEL_HOST}:{PANEL_PORT}{PANEL_PATH}")
}

pub fn sibling_exe_path(base_dir: &Path, exe_name: &str) -> PathBuf {
    base_dir.join(exe_name)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shared_root_extends_public_dir() {
        assert_eq!(app_shared_root(), public_share_dir().join("GoDesk"));
    }

    #[test]
    fn guard_roots_live_under_godesk() {
        let root = app_shared_root();
        assert_eq!(guard_data_root(), root.join("gr_data"));
        assert_eq!(guard_log_root(), root.join("gr_logs"));
        assert_eq!(guard_log_file(), root.join("gr_logs").join("godesk_guard.log"));
    }

    #[test]
    fn panel_ws_url_matches_cpp_guard_endpoint() {
        assert_eq!(panel_ws_url(), "ws://127.0.0.1:20369/panel/renderer?from=guard");
    }

    #[test]
    fn sibling_exe_path_reuses_current_directory() {
        let path = sibling_exe_path(Path::new("D:/GammaRay"), PANEL_EXE_NAME);
        assert_eq!(path, PathBuf::from("D:/GammaRay").join("GammaRay.exe"));
    }

    #[test]
    fn guard_poll_interval_matches_five_seconds() {
        assert_eq!(GUARD_POLL_INTERVAL, Duration::from_secs(5));
    }
}
