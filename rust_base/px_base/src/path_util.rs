use std::path::PathBuf;

pub const DEFAULT_APP_NAME: &str = "GoDesk";
pub const PX_LOG_DIR: &str = "px_logs";
pub const PX_DATA_DIR: &str = "px_data";

/// Matches C++ FolderUtil::GetProgramDataPath("GoDesk") — %PUBLIC%/GoDesk
pub fn public_share_dir() -> PathBuf {
    match std::env::var_os("PUBLIC") {
        Some(value) => PathBuf::from(value),
        None => PathBuf::from("C:/Users/Public"),
    }
}

pub fn app_shared_root() -> PathBuf {
    public_share_dir().join(DEFAULT_APP_NAME)
}

pub fn default_log_root() -> PathBuf {
    app_shared_root().join(PX_LOG_DIR)
}

pub fn default_data_root() -> PathBuf {
    app_shared_root().join(PX_DATA_DIR)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shared_root_extends_public_dir() {
        assert_eq!(app_shared_root(), public_share_dir().join("GoDesk"));
    }

    #[test]
    fn default_roots_live_under_godesk() {
        let root = app_shared_root();
        assert_eq!(default_data_root(), root.join("px_data"));
        assert_eq!(default_log_root(), root.join("px_logs"));
    }
}
