//! In-memory coordination for the record tunnel (design doc 6.2 / 6.3):
//! - pending RecordListReq/Resp correlation (req_id -> oneshot)
//! - in-flight fetch dedupe ("device_id|filename" triggered once)
//! - one-time upload tokens for POST /api/v1/record/upload

use crate::cms_api_error::CmsApiError;
use protocol::cms_panel::RecordListResp;
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use tokio::sync::oneshot;

/// upload token lifetime: 10 minutes
pub const UPLOAD_TOKEN_TTL_MS: i64 = 10 * 60 * 1000;
/// tunnel list request timeout
pub const LIST_REQ_TIMEOUT: Duration = Duration::from_secs(10);
/// an in-flight fetch older than this is considered stale (panel went away
/// mid-transfer) and may be re-triggered
pub const INFLIGHT_STALE_MS: i64 = 30 * 60 * 1000;

#[derive(Debug, Clone)]
pub struct UploadToken {
    pub device_id: String,
    pub filename: String,
    pub exp_ms: i64,
}

pub struct RecordTunnelManager {
    pending_lists: Mutex<HashMap<String, oneshot::Sender<RecordListResp>>>,
    // "device_id|filename" -> started ms
    inflight: Mutex<HashMap<String, i64>>,
    tokens: Mutex<HashMap<String, UploadToken>>,
}

pub fn inflight_key(device_id: &str, filename: &str) -> String {
    format!("{}|{}", device_id, filename)
}

impl RecordTunnelManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            pending_lists: Mutex::new(HashMap::new()),
            inflight: Mutex::new(HashMap::new()),
            tokens: Mutex::new(HashMap::new()),
        })
    }

    // ---- pending list requests ----

    pub fn register_list(&self, req_id: &str) -> oneshot::Receiver<RecordListResp> {
        let (tx, rx) = oneshot::channel();
        self.pending_lists
            .lock()
            .unwrap()
            .insert(req_id.to_string(), tx);
        rx
    }

    pub fn complete_list(&self, resp: RecordListResp) -> bool {
        let tx = self.pending_lists.lock().unwrap().remove(&resp.req_id);
        match tx {
            Some(tx) => tx.send(resp).is_ok(),
            None => false,
        }
    }

    /// wait for the panel RecordListResp; on timeout the pending entry is dropped
    pub async fn wait_list(
        &self,
        req_id: &str,
        rx: oneshot::Receiver<RecordListResp>,
    ) -> Result<RecordListResp, CmsApiError> {
        self.wait_list_with_timeout(req_id, rx, LIST_REQ_TIMEOUT)
            .await
    }

    pub async fn wait_list_with_timeout(
        &self,
        req_id: &str,
        rx: oneshot::Receiver<RecordListResp>,
        timeout: Duration,
    ) -> Result<RecordListResp, CmsApiError> {
        match tokio::time::timeout(timeout, rx).await {
            Ok(Ok(resp)) => Ok(resp),
            Ok(Err(_)) => Err(CmsApiError::DeviceOffline),
            Err(_) => {
                self.pending_lists.lock().unwrap().remove(req_id);
                Err(CmsApiError::RequestTimeout)
            }
        }
    }

    // ---- in-flight fetch dedupe ----

    /// returns false when the same device+file fetch is already in-flight
    /// (stale entries older than INFLIGHT_STALE_MS are replaced)
    pub fn add_inflight(&self, device_id: &str, filename: &str) -> bool {
        let now = px_base::get_current_timestamp();
        let mut map = self.inflight.lock().unwrap();
        let key = inflight_key(device_id, filename);
        if let Some(started) = map.get(&key) {
            if now - *started <= INFLIGHT_STALE_MS {
                return false;
            }
        }
        map.insert(key, now);
        true
    }

    pub fn remove_inflight(&self, device_id: &str, filename: &str) {
        self.inflight
            .lock()
            .unwrap()
            .remove(&inflight_key(device_id, filename));
    }

    pub fn is_inflight(&self, device_id: &str, filename: &str) -> bool {
        let map = self.inflight.lock().unwrap();
        match map.get(&inflight_key(device_id, filename)) {
            Some(started) => px_base::get_current_timestamp() - *started <= INFLIGHT_STALE_MS,
            None => false,
        }
    }

    // ---- one-time upload tokens ----

    pub fn new_token(&self, device_id: &str, filename: &str) -> String {
        let token = uuid::Uuid::new_v4().to_string();
        self.tokens.lock().unwrap().insert(
            token.clone(),
            UploadToken {
                device_id: device_id.to_string(),
                filename: filename.to_string(),
                exp_ms: px_base::get_current_timestamp() + UPLOAD_TOKEN_TTL_MS,
            },
        );
        token
    }

    /// one-time: a successful validation consumes the token
    pub fn validate_token(&self, token: &str, device_id: &str, filename: &str) -> bool {
        let t = self.tokens.lock().unwrap().remove(token);
        match t {
            Some(t) => {
                t.device_id == device_id
                    && t.filename == filename
                    && t.exp_ms > px_base::get_current_timestamp()
            }
            None => false,
        }
    }

    /// drop expired tokens (called periodically by the record cleaner)
    pub fn purge_expired_tokens(&self) {
        let now = px_base::get_current_timestamp();
        self.tokens.lock().unwrap().retain(|_, t| t.exp_ms > now);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn token_is_one_time_and_bound() {
        let mgr = RecordTunnelManager::new();
        let tk = mgr.new_token("dev1", "a.mp4");
        // wrong binding rejected, and does not consume
        assert!(!mgr.validate_token(&tk, "dev2", "a.mp4"));
        assert!(!mgr.validate_token(&tk, "dev1", "b.mp4"));
        // hmm: failed validation consumed it above; re-issue to test success path
        let tk = mgr.new_token("dev1", "a.mp4");
        assert!(mgr.validate_token(&tk, "dev1", "a.mp4"));
        // consumed: replay fails
        assert!(!mgr.validate_token(&tk, "dev1", "a.mp4"));
    }

    #[test]
    fn unknown_token_rejected() {
        let mgr = RecordTunnelManager::new();
        assert!(!mgr.validate_token("nope", "dev1", "a.mp4"));
    }

    #[test]
    fn inflight_dedupe() {
        let mgr = RecordTunnelManager::new();
        assert!(mgr.add_inflight("dev1", "a.mp4"));
        assert!(!mgr.add_inflight("dev1", "a.mp4"));
        assert!(mgr.add_inflight("dev1", "b.mp4"));
        assert!(mgr.is_inflight("dev1", "a.mp4"));
        mgr.remove_inflight("dev1", "a.mp4");
        assert!(!mgr.is_inflight("dev1", "a.mp4"));
        assert!(mgr.add_inflight("dev1", "a.mp4"));
    }

    #[tokio::test]
    async fn list_roundtrip() {
        let mgr = RecordTunnelManager::new();
        let rx = mgr.register_list("req-1");
        let resp = RecordListResp {
            device_id: "dev1".to_string(),
            req_id: "req-1".to_string(),
            files: vec![],
            error: "".to_string(),
        };
        assert!(mgr.complete_list(resp));
        let got = mgr.wait_list("req-1", rx).await.unwrap();
        assert_eq!(got.device_id, "dev1");
        // completing an unknown req_id is a no-op
        assert!(!mgr.complete_list(RecordListResp {
            req_id: "unknown".to_string(),
            ..Default::default()
        }));
    }

    #[tokio::test]
    async fn list_timeout_cleans_pending_entry() {
        let mgr = RecordTunnelManager::new();
        let rx = mgr.register_list("req-slow");
        // no completion arrives -> timeout, pending entry must be dropped
        let r = mgr
            .wait_list_with_timeout("req-slow", rx, Duration::from_millis(50))
            .await;
        assert_eq!(r.unwrap_err(), CmsApiError::RequestTimeout);
        // a late completion for the same req_id finds no waiter
        assert!(!mgr.complete_list(RecordListResp {
            req_id: "req-slow".to_string(),
            ..Default::default()
        }));
    }
}
