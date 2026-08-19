use crate::cms_api_error::CmsApiError;
use crate::net_panel::cms_panel_conn::{CmsPanelConn, CmsPanelConnPtr, CmsPanelConnVo};
use egui::ahash::HashMap;
use tokio::sync::Mutex;

pub struct CmsPanelConnManager {
    connections: Mutex<HashMap<String, CmsPanelConnPtr>>,
}

impl CmsPanelConnManager {
    pub fn new() -> Self {
        Self {
            connections: Mutex::new(Default::default()),
        }
    }

    pub async fn add_conn(&self, device_id: String, conn: CmsPanelConnPtr) {
        self.connections.lock().await.insert(device_id, conn);
    }

    pub async fn remove_conn(&self, device_id: String) {
        self.connections.lock().await.remove(&device_id);
    }

    pub async fn get_conn(&self, device_id: String) -> Result<CmsPanelConnPtr, CmsApiError> {
        let conn = self.connections.lock().await.get(&device_id).cloned();
        if let Some(conn) = conn {
            Ok(conn)
        } else {
            Err(CmsApiError::DeviceNotFound)
        }
    }

    pub async fn get_conn_info(&self, device_id: String) -> Result<CmsPanelConnVo, CmsApiError> {
        let conn = self.get_conn(device_id).await?;
        let conn = conn.lock().await.clone();
        Ok(conn.as_info())
    }

    pub async fn get_all_conn(&self) -> Result<Vec<CmsPanelConn>, CmsApiError> {
        let mut all_conn = Vec::new();
        for conn in self.connections.lock().await.values() {
            all_conn.push(conn.lock().await.clone());
        }
        if all_conn.is_empty() {
            Err(CmsApiError::ConnectionNotFound)
        } else {
            Ok(all_conn)
        }
    }

    pub async fn get_all_conn_info(&self) -> Result<Vec<CmsPanelConnVo>, CmsApiError> {
        let mut all_conn = Vec::new();
        for conn in self.connections.lock().await.values() {
            all_conn.push(conn.lock().await.as_info());
        }
        if all_conn.is_empty() {
            Err(CmsApiError::ConnectionNotFound)
        } else {
            Ok(all_conn)
        }
    }

    pub async fn get_all_conn_count(&self) -> usize {
        self.connections.lock().await.len()
    }

    pub async fn is_panel_online(&self, device_id: String) -> Result<bool, CmsApiError> {
        for id in self.connections.lock().await.keys() {
            if *id == device_id {
                return Ok(true);
            }
        }
        Ok(false)
    }
}
