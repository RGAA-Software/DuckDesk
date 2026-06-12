use std::collections::HashMap;
use tokio::sync::Mutex;
use crate::net_relay::spvr_relay_conn::SpvrRelayConnPtr;

pub struct SpvrRelayConnManager {
    conn: Option<SpvrRelayConnPtr>,
}

impl SpvrRelayConnManager {
    pub fn new() -> Self {
        Self {
            conn: None,
        }
    }

    pub async fn add_conn(&mut self, conn: SpvrRelayConnPtr) {
        self.conn = Some(conn);
    }
    
    pub async fn remove_conn(&mut self) {
        self.conn.take();
    }

    pub async fn get_relay_conn(&self) -> Option<SpvrRelayConnPtr> {
        self.conn.clone()
    }
}