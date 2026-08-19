use crate::cms_context::CmsContext;
use crate::cms_relay::relay_message::{
    KEY_CLIENT_W3C_HOST, KEY_DEVICE_ID, KEY_DEVICE_NAME, KEY_LAST_UPDATE_TIMESTAMP, KEY_STREAM_ID,
};
use crate::{gRelayRedisConn, gRelayRoomMgr};
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use chrono::Utc;
use futures_util::stream::SplitSink;
use futures_util::SinkExt;
use protocol::px_relay;
use protocol::px_relay::RelayMessage;
use redis::AsyncCommands;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

pub const RELAY_IGNORE_MSG_INDEX: i64 = -1;

pub struct RelayConn {
    pub context: Arc<Mutex<CmsContext>>,
    pub sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
    pub device_id: String,
    pub last_update_timestamp: i64,
    pub heartbeat_index: i64,
    // client's www ip address
    pub client_w3c_host: String,
    pub client_net_info: Vec<px_relay::RelayDeviceNetInfo>,
    pub last_relay_msg_index: i64,
    pub device_name: String,
    pub stream_id: String,
}

impl RelayConn {
    pub async fn new(
        context: Arc<Mutex<CmsContext>>,
        sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
        device_id: String,
        client_w3c_host: String,
        device_name: String,
        stream_id: String,
    ) -> Arc<Mutex<RelayConn>> {
        Arc::new(Mutex::new(RelayConn {
            context,
            sender,
            device_id,
            last_update_timestamp: px_base::get_current_timestamp(),
            heartbeat_index: 0,
            client_w3c_host,
            client_net_info: vec![],
            last_relay_msg_index: 0,
            device_name,
            stream_id,
        }))
    }

    pub async fn append_upload_data_size(&mut self, size: i64) {
        // to redis; key: upload_{device_id}_{stream_id}_year_month
        let now = Utc::now();
        let key_month = format!("upload_{}:{}", self.device_id, now.format("%Y_%m"));
        if let Err(e) = gRelayRedisConn
            .lock()
            .await
            .clone_conn()
            .incr::<String, i64, ()>(key_month.clone(), size)
            .await
        {
            tracing::error!("update upload data for: {} failed: {}", key_month, e);
        }

        // to redis; key: received_{device_id}_{stream_id}_year_month_day
        let key_day = format!("upload_{}:{}", self.device_id, now.format("%Y_%m_%d"));
        if let Err(e) = gRelayRedisConn
            .lock()
            .await
            .clone_conn()
            .incr::<String, i64, ()>(key_day.clone(), size)
            .await
        {
            tracing::error!("update upload data for: {} failed: {}", key_day, e);
        }
    }

    pub async fn append_down_data_size(&mut self, size: i64) {
        // to redis; key: sent_{device_id}_{stream_id}_year_month
        let now = Utc::now();
        let key_month = format!("down_{}:{}", self.device_id, now.format("%Y_%m"));
        if let Err(e) = gRelayRedisConn
            .lock()
            .await
            .clone_conn()
            .incr::<String, i64, ()>(key_month.clone(), size)
            .await
        {
            tracing::error!("update down data for: {} failed: {}", key_month, e);
        }

        // to redis; key: sent_{device_id}_{stream_id}_year_month_day
        let key_day = format!("down_{}:{}", self.device_id, now.format("%Y_%m_%d"));
        if let Err(e) = gRelayRedisConn
            .lock()
            .await
            .clone_conn()
            .incr::<String, i64, ()>(key_day.clone(), size)
            .await
        {
            tracing::error!("update down data for: {} failed: {}", key_day, e);
        }
    }

    pub async fn on_hello(&mut self, m: RelayMessage) {
        self.last_update_timestamp = px_base::get_current_timestamp();
        self.client_net_info = m.hello.unwrap().net_info;
        tracing::info!(
            "received hello message: {}, net info: {:#?}",
            m.from_device_id,
            self.client_net_info
        );
        // 回复 kRelayHello:客户端 SDK 以此触发 hello 回调,panel 的中转指示灯
        // 依赖这条回执(relay alive 时间戳)。
        let reply = RelayMessage {
            from_device_id: self.device_id.clone(),
            r#type: px_relay::RelayMessageType::KRelayHello as i32,
            hello: Some(px_relay::RelayHello { net_info: vec![] }),
            ..Default::default()
        };
        use prost::Message as ProstMessage;
        self.send_bin_message(Bytes::from(reply.encode_to_vec()))
            .await;
    }

    pub async fn on_heartbeat(&mut self, m: RelayMessage) {
        self.last_update_timestamp = px_base::get_current_timestamp();
        if let Some(heartbeat) = m.heartbeat {
            self.heartbeat_index = heartbeat.index;
            self.client_net_info = heartbeat.net_info;
            gRelayRoomMgr
                .on_heartbeat_for_my_room(m.from_device_id)
                .await;
            // 回执心跳(kRelayHeartBeat,携带原 index):客户端用它更新
            // relay alive 时间戳。不回的话 panel 的中转/中转文件灯永远红。
            let reply = RelayMessage {
                from_device_id: self.device_id.clone(),
                r#type: px_relay::RelayMessageType::KRelayHeartBeat as i32,
                heartbeat: Some(px_relay::RelayHeartBeat {
                    index: heartbeat.index,
                    net_info: vec![],
                }),
                ..Default::default()
            };
            use prost::Message as ProstMessage;
            self.send_bin_message(Bytes::from(reply.encode_to_vec()))
                .await;
        }
    }

    pub async fn on_error(&self, _m: RelayMessage) {}

    pub async fn send_bin_message(&mut self, om: Bytes) -> bool {
        self.send_bin_message_with_index(RELAY_IGNORE_MSG_INDEX, om)
            .await
    }

    // send back to this connection itself.
    pub async fn send_bin_message_with_index(&mut self, relay_msg_index: i64, om: Bytes) -> bool {
        // alive or not
        if !self.is_alive() {
            tracing::warn!("this device is not alive : {}", self.device_id);
            return false;
        }

        // check message index
        if relay_msg_index != RELAY_IGNORE_MSG_INDEX {
            if self.last_relay_msg_index == 0 || relay_msg_index == 0 {
                self.last_relay_msg_index = relay_msg_index;
            } else {
                //tracing::info!("Relay index: {}", relay_msg_index);
                let diff = relay_msg_index - self.last_relay_msg_index;
                if diff != 1 {
                    tracing::error!(
                        "Relay msg index error, ==Send To==> device: {}, current: {}, last: {}",
                        self.device_id,
                        relay_msg_index,
                        self.last_relay_msg_index
                    );
                } else {
                    //tracing::info!("Relay message index diff : {}, current: {}, last: {}", diff, relay_msg_index, self.last_relay_msg_index);
                }
                self.last_relay_msg_index = relay_msg_index;
            }
        }

        // send message
        let size = om.len();
        let r = self.sender.lock().await.send(Message::Binary(om)).await;
        if let Err(r) = r {
            tracing::error!("error sending relay message: {r}");
            return false;
        }

        // append down data size
        self.append_down_data_size(size as i64).await;

        true
    }

    pub fn is_alive(&self) -> bool {
        px_base::get_current_timestamp() - self.last_update_timestamp < 60 * 1000
    }

    pub fn as_str_map(&self) -> HashMap<String, String> {
        let mut hm = HashMap::new();
        hm.insert(KEY_DEVICE_ID.to_string(), self.device_id.clone());
        hm.insert(
            KEY_LAST_UPDATE_TIMESTAMP.to_string(),
            self.last_update_timestamp.to_string(),
        );
        hm.insert(
            KEY_CLIENT_W3C_HOST.to_string(),
            self.client_w3c_host.clone(),
        );
        hm.insert(KEY_DEVICE_NAME.to_string(), self.device_name.clone());
        hm.insert(KEY_STREAM_ID.to_string(), self.stream_id.clone());
        hm
    }
}
