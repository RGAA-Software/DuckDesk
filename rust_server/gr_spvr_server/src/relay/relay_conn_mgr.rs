use redis::AsyncCommands;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::{Mutex, RwLock};
use crate::relay::relay_conn::RelayConn;

pub struct RelayConnManager {
    pub relay_conns: RwLock<HashMap<String, Arc<Mutex<RelayConn>>>>,
}

impl RelayConnManager {
    pub fn new() -> RelayConnManager {
        RelayConnManager {
            relay_conns: RwLock::new(HashMap::new()),
        }
    }
    
    pub async fn add_connection(&self, device_id: String, relay_conn: Arc<Mutex<RelayConn>>) {
        self.relay_conns
            .write().await
            .insert(device_id.clone(), relay_conn);
    }
    
    pub async fn remove_connection(&self, device_id: String) {
        self.relay_conns
            .write().await
            .remove(&device_id);
    }

    pub async fn get_conn(&self, device_id: String) -> Option<Arc<Mutex<RelayConn>>> {
        self.relay_conns
            .read().await
            .get(&device_id).cloned()
    }
    
    pub async fn get_connections(&self) -> Vec<Arc<Mutex<RelayConn>>> {
        self.relay_conns
            .read().await
            .values()
            .cloned()
            .collect()
    }
}