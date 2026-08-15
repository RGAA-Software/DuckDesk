use crate::spvr_context::SpvrContext;
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use futures_util::stream::SplitSink;
use futures_util::SinkExt;
use prost::Message as ProstMessage;
use protocol::spvr_service::{
    SpvrServiceHeartBeat, SpvrServiceHello, SpvrServiceMessage, SpvrServiceMessageType,
};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::Mutex;

pub type SpvrServiceConnPtr = Arc<Mutex<SpvrServiceConn>>;

#[derive(Clone)]
pub struct SpvrServiceConn {
    pub context: Arc<Mutex<SpvrContext>>,
    pub sender: Option<Arc<Mutex<SplitSink<WebSocket, Message>>>>,
    pub device_id: String,
    pub appkey: String,
    pub version: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    pub render_alive: bool,
    pub auth_info_json: String,
    pub instances_json: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct SpvrServiceConnVo {
    pub device_id: String,
    pub appkey: String,
    pub version: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    pub render_alive: bool,
    pub auth_info_json: String,
    #[serde(default)]
    pub instances_json: String,
}

impl SpvrServiceConn {
    //
    pub async fn new(
        context: Arc<Mutex<SpvrContext>>,
        sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
        device_id: String,
        appkey: String,
    ) -> SpvrServiceConn {
        Self {
            context,
            sender: Some(sender),
            device_id,
            appkey,
            version: "".to_string(),
            hello_timestamp: 0,
            last_update_timestamp: 0,
            hb_index: 0,
            render_alive: false,
            auth_info_json: "".to_string(),
            instances_json: "".to_string(),
        }
    }

    pub fn as_info(&self) -> SpvrServiceConnVo {
        SpvrServiceConnVo {
            device_id: self.device_id.to_string(),
            appkey: self.appkey.to_string(),
            version: self.version.to_string(),
            hello_timestamp: self.hello_timestamp,
            last_update_timestamp: self.last_update_timestamp,
            hb_index: self.hb_index,
            render_alive: self.render_alive,
            auth_info_json: self.auth_info_json.to_string(),
            instances_json: self.instances_json.to_string(),
        }
    }

    pub async fn process_message(&mut self, _who: String, data: Bytes) -> bool {
        let m = SpvrServiceMessage::decode(data);
        if let Err(e) = m {
            tracing::error!("parse error: {:?}", e);
            return false;
        }
        let m = m.unwrap();
        if m.msg_type == SpvrServiceMessageType::KSpvrServiceHello {
            let Some(sub) = m.hello else {
                tracing::warn!("service hello message without hello body!");
                return true;
            };
            self.hello_timestamp = px_base::get_current_timestamp();
            self.last_update_timestamp = self.hello_timestamp;
            let device_id = sub.device_id;
            self.version = sub.version;
            self.send_hello(device_id).await;
        } else if m.msg_type == SpvrServiceMessageType::KSpvrServiceHeartBeat {
            let Some(sub) = m.heartbeat else {
                tracing::warn!("service heartbeat message without heartbeat body!");
                return true;
            };
            self.last_update_timestamp = px_base::get_current_timestamp();
            let hb_index = sub.hb_index;
            self.hb_index = hb_index;
            self.render_alive = sub.render_alive;
            self.auth_info_json = sub.auth_info_json;
            self.instances_json = sub.instances_json;
            crate::app_schedule::gAppScheduleManager
                .reconcile_from_service_hb(self.device_id.clone(), &self.instances_json)
                .await;
            self.send_heartbeat(hb_index, self.device_id.clone()).await;
        } else if m.msg_type == SpvrServiceMessageType::KSpvrServiceStartAppInstanceResult {
            if let Some(sub) = m.start_app_instance_result {
                crate::app_schedule::gAppScheduleManager
                    .on_start_result(self.device_id.clone(), sub)
                    .await;
            }
        } else if m.msg_type == SpvrServiceMessageType::KSpvrServiceStopAppInstanceResult {
            if let Some(sub) = m.stop_app_instance_result {
                crate::app_schedule::gAppScheduleManager
                    .on_stop_result(self.device_id.clone(), sub)
                    .await;
            }
        }

        true
    }

    /// Proactively close the websocket (used when a fresher connection for the
    /// same device replaces this one). After this, sends fail fast.
    pub async fn close(&mut self) {
        if let Some(sender) = self.sender.take() {
            let _ = sender.lock().await.send(Message::Close(None)).await;
        }
    }

    async fn send_hello(&mut self, device_id: String) {
        let mut sv_msg = SpvrServiceMessage::default();
        sv_msg.set_msg_type(SpvrServiceMessageType::KSpvrServiceHello);
        sv_msg.device_id = device_id.clone();
        sv_msg.hello = Some(SpvrServiceHello {
            device_id,
            appkey: self.appkey.clone(),
            version: self.version.clone(),
        });
        let buffer = sv_msg.encode_to_vec();
        self.send_bin_message_vec(buffer).await;
    }

    async fn send_heartbeat(&mut self, hb_index: i64, device_id: String) {
        let mut sv_msg = SpvrServiceMessage::default();
        sv_msg.set_msg_type(SpvrServiceMessageType::KSpvrServiceHeartBeat);
        sv_msg.device_id = device_id.clone();
        sv_msg.heartbeat = Some(SpvrServiceHeartBeat {
            hb_index,
            device_id,
            render_alive: false,
            auth_info_json: "".to_string(),
            instances_json: "".to_string(),
        });
        self.send_bin_message_vec(sv_msg.encode_to_vec()).await;
    }

    pub async fn send_start_app_instance(
        &mut self,
        start: protocol::spvr_service::SpvrServiceStartAppInstance,
    ) -> bool {
        let mut sv_msg = SpvrServiceMessage::default();
        sv_msg.set_msg_type(SpvrServiceMessageType::KSpvrServiceStartAppInstance);
        sv_msg.device_id = self.device_id.clone();
        sv_msg.start_app_instance = Some(start);
        self.send_bin_message_bytes(Bytes::from(sv_msg.encode_to_vec()))
            .await
    }

    pub async fn send_stop_app_instance(
        &mut self,
        stop: protocol::spvr_service::SpvrServiceStopAppInstance,
    ) -> bool {
        let mut sv_msg = SpvrServiceMessage::default();
        sv_msg.set_msg_type(SpvrServiceMessageType::KSpvrServiceStopAppInstance);
        sv_msg.device_id = self.device_id.clone();
        sv_msg.stop_app_instance = Some(stop);
        self.send_bin_message_bytes(Bytes::from(sv_msg.encode_to_vec()))
            .await
    }

    pub async fn send_bin_message_vec(&mut self, data: Vec<u8>) {
        self.send_bin_message_bytes(Bytes::from(data)).await;
    }

    pub async fn send_bin_message_bytes(&mut self, om: Bytes) -> bool {
        // send message
        let Some(sender) = &self.sender else {
            return false;
        };
        let _size = om.len();
        let r = sender.lock().await.send(Message::Binary(om)).await;
        if let Err(r) = r {
            tracing::error!("error sending service message: {r}");
            return false;
        }
        true
    }
}
