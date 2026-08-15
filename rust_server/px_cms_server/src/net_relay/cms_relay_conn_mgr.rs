use std::collections::HashMap;
use tokio::sync::Mutex;
use crate::net_px_relay::cms_relay_conn::CmsRelayConnPtr;

pub struct CmsRelayConnManager {
    conn: Option<CmsRelayConnPtr>,
}

impl CmsRelayConnManager {
    pub fn new() -> Self {
        Self {
            conn: None,
        }
    }

    pub async fn add_conn(&mut self, conn: CmsRelayConnPtr) {
        self.conn = Some(conn);
    }
    
    pub async fn remove_conn(&mut self) {
        self.conn.take();
    }

    pub async fn get_relay_conn(&self) -> Option<CmsRelayConnPtr> {
        self.conn.clone()
    }
}