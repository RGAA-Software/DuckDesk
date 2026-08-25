use crate::console_api_error::ConsoleApiError;
use std::collections::{HashMap, VecDeque};
use std::sync::Mutex;

lazy_static::lazy_static! {
    static ref ATTEMPTS: Mutex<HashMap<String, VecDeque<i64>>> = Mutex::new(HashMap::new());
}

/// Process-local fixed-window limiter. It intentionally fails closed if its
/// lock is poisoned and bounds each key's retained history to one window.
pub fn check(key: String, limit: usize, window_ms: i64) -> Result<(), ConsoleApiError> {
    if limit == 0 {
        return Err(ConsoleApiError::RateLimited);
    }
    let now = px_base::get_current_timestamp();
    let mut all = ATTEMPTS
        .lock()
        .map_err(|_| ConsoleApiError::InternalError)?;
    let entries = all.entry(key).or_default();
    while entries.front().is_some_and(|at| now - *at >= window_ms) {
        entries.pop_front();
    }
    if entries.len() >= limit {
        return Err(ConsoleApiError::RateLimited);
    }
    entries.push_back(now);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn limiter_rejects_after_configured_count() {
        let key = format!("test:{}", uuid::Uuid::new_v4());
        assert!(check(key.clone(), 2, 60_000).is_ok());
        assert!(check(key.clone(), 2, 60_000).is_ok());
        assert_eq!(check(key, 2, 60_000), Err(ConsoleApiError::RateLimited));
    }
}
