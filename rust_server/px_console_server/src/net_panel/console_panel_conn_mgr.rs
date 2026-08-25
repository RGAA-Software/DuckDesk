use crate::console_api_error::ConsoleApiError;
use crate::net_panel::console_panel_conn::{
    ConsolePanelConn, ConsolePanelConnPtr, ConsolePanelConnVo,
};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

fn remove_if_current<T>(
    connections: &mut HashMap<String, Arc<Mutex<T>>>,
    device_id: &str,
    closing_conn: &Arc<Mutex<T>>,
) -> bool {
    if connections
        .get(device_id)
        .is_some_and(|current| Arc::ptr_eq(current, closing_conn))
    {
        connections.remove(device_id);
        return true;
    }
    false
}

pub struct ConsolePanelConnManager {
    connections: Mutex<HashMap<String, ConsolePanelConnPtr>>,
}

impl ConsolePanelConnManager {
    pub fn new() -> Self {
        Self {
            connections: Mutex::new(Default::default()),
        }
    }

    pub async fn add_conn(&self, device_id: String, conn: ConsolePanelConnPtr) {
        self.connections.lock().await.insert(device_id, conn);
    }

    /// Remove only the connection whose receive loop is closing. A reconnect
    /// replaces the map entry before the stale loop necessarily exits, so an
    /// unconditional device-id removal can incorrectly mark the new connection
    /// offline.
    pub async fn remove_conn(&self, device_id: String, conn: &ConsolePanelConnPtr) -> bool {
        let mut connections = self.connections.lock().await;
        remove_if_current(&mut connections, &device_id, conn)
    }

    pub async fn get_conn(
        &self,
        device_id: String,
    ) -> Result<ConsolePanelConnPtr, ConsoleApiError> {
        let conn = self.connections.lock().await.get(&device_id).cloned();
        if let Some(conn) = conn {
            Ok(conn)
        } else {
            Err(ConsoleApiError::DeviceNotFound)
        }
    }

    pub async fn get_conn_info(
        &self,
        device_id: String,
    ) -> Result<ConsolePanelConnVo, ConsoleApiError> {
        let conn = self.get_conn(device_id).await?;
        let conn = conn.lock().await.clone();
        Ok(conn.as_info())
    }

    pub async fn get_all_conn(&self) -> Result<Vec<ConsolePanelConn>, ConsoleApiError> {
        let mut all_conn = Vec::new();
        for conn in self.connections.lock().await.values() {
            all_conn.push(conn.lock().await.clone());
        }
        if all_conn.is_empty() {
            Err(ConsoleApiError::ConnectionNotFound)
        } else {
            Ok(all_conn)
        }
    }

    pub async fn get_all_conn_info(&self) -> Result<Vec<ConsolePanelConnVo>, ConsoleApiError> {
        let mut all_conn = Vec::new();
        for conn in self.connections.lock().await.values() {
            all_conn.push(conn.lock().await.as_info());
        }
        if all_conn.is_empty() {
            Err(ConsoleApiError::ConnectionNotFound)
        } else {
            Ok(all_conn)
        }
    }

    pub async fn get_all_conn_count(&self) -> usize {
        self.connections.lock().await.len()
    }

    pub async fn is_panel_online(&self, device_id: String) -> Result<bool, ConsoleApiError> {
        for id in self.connections.lock().await.keys() {
            if *id == device_id {
                return Ok(true);
            }
        }
        Ok(false)
    }

    pub async fn broadcast_rtc_ice_config_changed(&self, revision: u64, changed_at: i64) -> usize {
        let connections: Vec<_> = self.connections.lock().await.values().cloned().collect();
        let mut delivered = 0;
        for connection in connections {
            if connection
                .lock()
                .await
                .send_rtc_ice_config_changed(revision, changed_at)
                .await
            {
                delivered += 1;
            }
        }
        delivered
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn stale_connection_cannot_remove_newer_connection() {
        let old = Arc::new(Mutex::new(1_u8));
        let new = Arc::new(Mutex::new(2_u8));
        let mut connections = HashMap::new();

        connections.insert("device-1".to_string(), old.clone());
        connections.insert("device-1".to_string(), new.clone());

        assert!(!remove_if_current(&mut connections, "device-1", &old));
        assert!(Arc::ptr_eq(connections.get("device-1").unwrap(), &new));

        assert!(remove_if_current(&mut connections, "device-1", &new));
        assert!(!connections.contains_key("device-1"));
    }
}
