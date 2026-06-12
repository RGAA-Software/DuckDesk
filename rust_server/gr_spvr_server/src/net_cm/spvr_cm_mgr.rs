use std::sync::{Arc};
use egui::ahash::HashMap;
use tokio::sync::Mutex;
use crate::net_cm::spvr_cm_conn::SpvrCmConn;

pub struct SpvrCMManager {
    pub cm_conns: Mutex<HashMap<String, Arc<Mutex<SpvrCmConn>>>>,
}

impl SpvrCMManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            cm_conns: Mutex::new(HashMap::default()),
        })
    }

    pub async fn add_cm_conn(&self, id: String, cm_conn: Arc<Mutex<SpvrCmConn>>) {
        self.cm_conns
            .lock().await
            .insert(id, cm_conn);
    }

    pub async fn remove_cm_conn(&self, id: String) {
        self.cm_conns
            .lock().await
            .remove(id.as_str());
    }

    pub async fn notify_data(&self, data: String) {
        for conn in self.cm_conns.lock().await.values_mut() {
            conn.lock().await
                .send_message(data.clone()).await;
        }
    }
}