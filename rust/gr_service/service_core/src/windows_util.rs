use std::path::PathBuf;

use crate::config::{SERVICE_DATA_DIR, SERVICE_LOG_DIR};

const DEFAULT_APP_NAME: &str = "GoDesk";

pub fn public_share_dir() -> PathBuf {
    match std::env::var_os("PUBLIC") {
        Some(value) => PathBuf::from(value),
        None => PathBuf::from("C:/Users/Public"),
    }
}

pub fn app_shared_root() -> PathBuf {
    public_share_dir().join(DEFAULT_APP_NAME)
}

pub fn default_service_data_root() -> PathBuf {
    app_shared_root().join(SERVICE_DATA_DIR)
}

pub fn default_service_log_root() -> PathBuf {
    app_shared_root().join(SERVICE_LOG_DIR)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_roots_extend_public_share_app_root() {
        let base = public_share_dir().join(DEFAULT_APP_NAME);
        assert_eq!(app_shared_root(), base);
        assert_eq!(default_service_data_root(), base.join(SERVICE_DATA_DIR));
        assert_eq!(default_service_log_root(), base.join(SERVICE_LOG_DIR));
    }
}
