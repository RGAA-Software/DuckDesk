use std::sync::atomic::{AtomicI32, Ordering};
use std::sync::Mutex;

#[derive(Debug, Default)]
pub struct EchoFilter {
    remote_echo: Mutex<String>,
    suppress_outbound: AtomicI32,
}

impl EchoFilter {
    pub fn set_remote_echo(&self, text: &str) {
        if let Ok(mut guard) = self.remote_echo.lock() {
            *guard = text.to_string();
        }
    }

    pub fn get_remote_echo(&self) -> String {
        self.remote_echo
            .lock()
            .map(|v| v.clone())
            .unwrap_or_default()
    }

    pub fn begin_suppress_outbound(&self) {
        self.suppress_outbound.fetch_add(1, Ordering::Relaxed);
    }

    pub fn end_suppress_outbound(&self) {
        self.suppress_outbound.fetch_sub(1, Ordering::Relaxed);
    }

    pub fn is_outbound_suppressed(&self) -> bool {
        self.suppress_outbound.load(Ordering::Relaxed) > 0
    }

    pub fn should_skip_outbound(&self, local_text: &str) -> bool {
        if self.is_outbound_suppressed() {
            return true;
        }
        local_text == self.get_remote_echo()
    }
}

pub struct SuppressOutboundGuard<'a> {
    filter: &'a EchoFilter,
}

impl<'a> SuppressOutboundGuard<'a> {
    pub fn new(filter: &'a EchoFilter) -> Self {
        filter.begin_suppress_outbound();
        Self { filter }
    }
}

impl Drop for SuppressOutboundGuard<'_> {
    fn drop(&mut self) {
        self.filter.end_suppress_outbound();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn set_and_get_remote_echo() {
        let filter = EchoFilter::default();
        filter.set_remote_echo("hello");
        assert_eq!(filter.get_remote_echo(), "hello");
    }

    #[test]
    fn should_skip_when_matches_echo() {
        let filter = EchoFilter::default();
        filter.set_remote_echo("same");
        assert!(filter.should_skip_outbound("same"));
    }

    #[test]
    fn should_not_skip_when_different() {
        let filter = EchoFilter::default();
        filter.set_remote_echo("remote");
        assert!(!filter.should_skip_outbound("local"));
    }

    #[test]
    fn suppress_blocks_outbound() {
        let filter = EchoFilter::default();
        filter.begin_suppress_outbound();
        assert!(filter.should_skip_outbound("anything"));
    }

    #[test]
    fn suppress_nested() {
        let filter = EchoFilter::default();
        filter.begin_suppress_outbound();
        filter.begin_suppress_outbound();
        filter.end_suppress_outbound();
        assert!(filter.is_outbound_suppressed());
        filter.end_suppress_outbound();
        assert!(!filter.is_outbound_suppressed());
    }

    #[test]
    fn empty_text_not_special() {
        let filter = EchoFilter::default();
        filter.set_remote_echo("");
        assert!(filter.should_skip_outbound(""));
        assert!(!filter.should_skip_outbound("x"));
    }
}
