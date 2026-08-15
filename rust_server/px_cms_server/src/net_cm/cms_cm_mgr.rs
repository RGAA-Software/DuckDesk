use crate::net_cm::cms_cm_conn::CmsCmConn;
use egui::ahash::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct CmsCMManager {
    pub cm_conns: Mutex<HashMap<String, Arc<Mutex<CmsCmConn>>>>,
}

impl CmsCMManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            cm_conns: Mutex::new(HashMap::default()),
        })
    }

    pub async fn add_cm_conn(&self, id: String, cm_conn: Arc<Mutex<CmsCmConn>>) {
        self.cm_conns.lock().await.insert(id, cm_conn);
    }

    pub async fn remove_cm_conn(&self, id: String) {
        self.cm_conns.lock().await.remove(id.as_str());
    }

    pub async fn notify_data(&self, data: String) {
        for conn in self.cm_conns.lock().await.values_mut() {
            conn.lock().await.send_message(data.clone()).await;
        }
    }
}
