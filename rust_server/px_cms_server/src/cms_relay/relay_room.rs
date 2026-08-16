use crate::cms_relay::relay_conn::RelayConn;
use crate::cms_relay::relay_message::{
    KEY_CREATE_TIMESTAMP, KEY_DEVICE_ID, KEY_DEVICE_NAME, KEY_LAST_UPDATE_TIMESTAMP,
    KEY_REMOTE_DEVICE_ID, KEY_ROOM_ID, KEY_STREAM_ID,
};
use axum::body::Bytes;
use serde::Serialize;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub struct RelayRoom {
    pub device_id: String,
    pub remote_device_id: String,
    pub room_id: String,
    pub create_timestamp: i64,
    pub last_update_timestamp: i64,
    pub relay_conns: HashMap<String, Arc<Mutex<RelayConn>>>,
    pub device_name: String,
    pub stream_id: String,
}

#[derive(Clone, Serialize, Debug)]
pub struct RelayRoomAdapter {
    pub device_id: String,
    pub remote_device_id: String,
    pub room_id: String,
    pub create_timestamp: i64,
    pub last_update_timestamp: i64,
    pub device_name: String,
    pub stream_id: String,
}

impl Default for RelayRoom {
    fn default() -> Self {
        RelayRoom {
            device_id: "".to_string(),
            remote_device_id: "".to_string(),
            room_id: "".to_string(),
            create_timestamp: 0,
            last_update_timestamp: 0,
            relay_conns: HashMap::new(),
            device_name: "".to_string(),
            stream_id: "".to_string(),
        }
    }
}

impl RelayRoom {
    pub fn is_valid(&self) -> bool {
        !self.relay_conns.is_empty()
            && !self.remote_device_id.is_empty()
            && !self.room_id.is_empty()
    }

    pub fn as_str_map(&self) -> HashMap<String, String> {
        let mut hm = HashMap::new();
        hm.insert(KEY_DEVICE_ID.to_string(), self.device_id.clone());
        hm.insert(
            KEY_REMOTE_DEVICE_ID.to_string(),
            self.remote_device_id.clone(),
        );
        hm.insert(KEY_ROOM_ID.to_string(), self.room_id.clone());
        hm.insert(
            KEY_CREATE_TIMESTAMP.to_string(),
            self.create_timestamp.to_string(),
        );
        hm.insert(
            KEY_LAST_UPDATE_TIMESTAMP.to_string(),
            self.last_update_timestamp.to_string(),
        );
        hm.insert(KEY_DEVICE_NAME.to_string(), self.device_name.clone());
        hm.insert(KEY_STREAM_ID.to_string(), self.stream_id.clone());
        hm
    }

    pub async fn notify_except(&self, except_id: String, relay_msg_index: i64, m: Bytes) {
        let mut conns = Vec::new();
        for (key, value) in self.relay_conns.clone() {
            if key != except_id {
                conns.push(value.clone());
            }
        }

        for conn in conns {
            let m = m.clone();
            let device_id = conn.lock().await.device_id.clone();
            let r = conn
                .lock()
                .await
                .send_bin_message_with_index(relay_msg_index, m)
                .await;
            if !r {
                tracing::warn!("notify to this device failed: {}", device_id)
            }
        }
    }

    pub fn adapter(&self) -> RelayRoomAdapter {
        RelayRoomAdapter {
            device_id: self.device_id.clone(),
            remote_device_id: self.remote_device_id.clone(),
            room_id: self.room_id.clone(),
            create_timestamp: self.create_timestamp,
            last_update_timestamp: self.last_update_timestamp,
            device_name: self.device_name.clone(),
            stream_id: self.stream_id.clone(),
        }
    }
}
