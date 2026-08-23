use crate::net_cm::console_cm_conn::ConsoleCmConn;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct ConsoleCMManager {
    pub cm_conns: Mutex<HashMap<String, Arc<Mutex<ConsoleCmConn>>>>,
}

impl ConsoleCMManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            cm_conns: Mutex::new(HashMap::default()),
        })
    }

    pub async fn add_cm_conn(&self, id: String, cm_conn: Arc<Mutex<ConsoleCmConn>>) {
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
