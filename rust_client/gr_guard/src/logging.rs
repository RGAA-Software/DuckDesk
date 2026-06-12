use gr_base::log_util::{self, LogGuard};

use crate::config::{guard_log_root, GUARD_LOG_FILE};

pub fn init_guard_logging() -> LogGuard {
    log_util::init_log(
        guard_log_root().to_string_lossy().to_string(),
        GUARD_LOG_FILE.to_string(),
    )
}

#[cfg(test)]
mod tests {
    #[test]
    fn init_guard_logging_uses_guard_log_name() {
        assert_eq!(crate::config::GUARD_LOG_FILE, "godesk_guard.log");
    }
}
