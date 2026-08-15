use crate::net_client::cms_controlling_stream::{CmsControllingStream, CmsControllingStreamPtr};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct CmsControllingStreamMgr {
    streams: Arc<Mutex<HashMap<String, CmsControllingStreamPtr>>>,
}

impl CmsControllingStreamMgr {
    pub fn new() -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(Self {
            streams: Arc::new(Default::default()),
        }))
    }

    pub async fn add_stream(&self, id: String, stream: CmsControllingStream) {
        self.streams
            .lock()
            .await
            .insert(id, Arc::new(Mutex::new(stream)));
    }

    pub async fn remove_stream(&self, id: String) {
        self.streams.lock().await.remove(&id);
    }

    pub async fn get_streams(&self) -> Vec<CmsControllingStream> {
        let mut streams = Vec::new();
        for (_, stream) in self.streams.lock().await.iter() {
            streams.push(stream.lock().await.clone());
        }
        streams
    }
}
