use egui::ahash::HashMap;
use tokio::sync::Mutex;
use crate::net_panel::spvr_panel_conn::{SpvrPanelConn, SpvrPanelConnVo, SpvrPanelConnPtr};
use crate::spvr_api_error::SpvrApiError;

pub struct SpvrPanelConnManager {
    connections: Mutex<HashMap<String, SpvrPanelConnPtr>>,
}

impl SpvrPanelConnManager {
    pub fn new() -> Self {
        Self {
            connections: Mutex::new(Default::default()),
        }
    }

    pub async fn add_conn(&self, device_id: String, conn: SpvrPanelConnPtr) {
        self.connections
            .lock().await
            .insert(device_id, conn);
    }

    pub async fn remove_conn(&self, device_id: String) {
        self.connections
            .lock().await
            .remove(&device_id);
    }

    pub async fn get_conn(&self, device_id: String) -> Result<SpvrPanelConnPtr, SpvrApiError> {
        let conn = self.connections
            .lock().await
            .get(&device_id).cloned();
        if let Some(conn) = conn {
            Ok(conn)
        }
        else {
            Err(SpvrApiError::DeviceNotFound)
        }
    }

    pub async fn get_conn_info(&self, device_id: String) -> Result<SpvrPanelConnVo, SpvrApiError> {
        let conn = self.get_conn(device_id).await?;
        let conn = conn.lock().await.clone();
        Ok(conn.as_info())
    }

    pub async fn get_all_conn(&self) -> Result<Vec<SpvrPanelConn>, SpvrApiError> {
        let mut all_conn = Vec::new();
        for (id, conn) in self.connections.lock().await.iter() {
            all_conn.push(conn.lock().await.clone());
        }
        if all_conn.is_empty() {
            Err(SpvrApiError::ConnectionNotFound)
        }
        else {
            Ok(all_conn)
        }
    }

    pub async fn get_all_conn_info(&self) -> Result<Vec<SpvrPanelConnVo>, SpvrApiError> {
        let mut all_conn = Vec::new();
        for (id, conn) in self.connections.lock().await.iter() {
            all_conn.push(conn.lock().await.as_info());
        }
        if all_conn.is_empty() {
            Err(SpvrApiError::ConnectionNotFound)
        }
        else {
            Ok(all_conn)
        }
    }

    pub async fn get_all_conn_count(&self) -> usize {
        self.connections.lock().await.len()
    }

    pub async fn is_panel_online(&self, device_id: String) -> Result<bool, SpvrApiError> {
        for (id, conn) in self.connections.lock().await.iter() {
            if *id == device_id {
                return Ok(true)
            }
        }
        Ok(false)
    }

}