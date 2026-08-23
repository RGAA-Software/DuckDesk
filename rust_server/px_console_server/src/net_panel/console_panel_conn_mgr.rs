use crate::console_api_error::ConsoleApiError;
use crate::net_panel::console_panel_conn::{ConsolePanelConn, ConsolePanelConnPtr, ConsolePanelConnVo};
use std::collections::HashMap;
use tokio::sync::Mutex;

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

    pub async fn remove_conn(&self, device_id: String) {
        self.connections.lock().await.remove(&device_id);
    }

    pub async fn get_conn(&self, device_id: String) -> Result<ConsolePanelConnPtr, ConsoleApiError> {
        let conn = self.connections.lock().await.get(&device_id).cloned();
        if let Some(conn) = conn {
            Ok(conn)
        } else {
            Err(ConsoleApiError::DeviceNotFound)
        }
    }

    pub async fn get_conn_info(&self, device_id: String) -> Result<ConsolePanelConnVo, ConsoleApiError> {
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
}
