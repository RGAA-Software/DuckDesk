use px_base::log_util::{self, LogGuard};

use crate::config::{user_proxy_log_root, USER_PROXY_LOG_FILE};

pub fn init_user_proxy_logging() -> LogGuard {
    log_util::init_log(
        user_proxy_log_root().to_string_lossy().to_string(),
        USER_PROXY_LOG_FILE.to_string(),
    )
}

#[cfg(test)]
mod tests {
    #[test]
    fn log_file_name_constant() {
        assert_eq!(crate::config::USER_PROXY_LOG_FILE, "godesk_user_proxy.log");
    }

    #[test]
    fn log_root_under_godesk() {
        assert_eq!(
            crate::config::user_proxy_log_root(),
            crate::config::app_shared_root().join("gr_logs")
        );
    }
}
