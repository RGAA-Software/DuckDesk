use crate::cms_context::CmsContext;
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use futures_util::stream::SplitSink;
use futures_util::SinkExt;
use px_base::sys_info::SysInfo;
use prost::Message as ProstMessage;
use protocol::cms_panel::{
    CmsPanelHeartBeat, CmsPanelHello, CmsPanelMessage, CmsPanelMessageType,
};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::Mutex;

pub type CmsPanelConnPtr = Arc<Mutex<CmsPanelConn>>;

#[derive(Clone)]
pub struct CmsPanelConn {
    pub context: Arc<Mutex<CmsContext>>,
    pub sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
    pub device_id: String,
    pub device_name: String,
    pub appkey: String,
    pub user_id: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    pub device_ip_addr: String,
    pub sys_info_array: Vec<SysInfo>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct CmsPanelConnVo {
    pub device_id: String,
    pub device_name: String,
    pub appkey: String,
    pub user_id: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub device_ip_addr: String,
    pub sys_info: SysInfo,
}

impl CmsPanelConn {
    //
    pub async fn new(
        context: Arc<Mutex<CmsContext>>,
        sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
        device_id: String,
        appkey: String,
        user_id: String,
    ) -> CmsPanelConn {
        Self {
            context,
            sender,
            device_id,
            device_name: "".to_string(),
            appkey,
            user_id,
            hello_timestamp: 0,
            last_update_timestamp: 0,
            hb_index: 0,
            device_ip_addr: "".to_string(),
            sys_info_array: Default::default(),
        }
    }

    pub fn as_info(&self) -> CmsPanelConnVo {
        let mut sys_info = SysInfo::default();
        if !self.sys_info_array.is_empty() {
            sys_info = self.sys_info_array.last().unwrap().clone();
        }
        CmsPanelConnVo {
            device_id: self.device_id.to_string(),
            device_name: self.device_name.to_string(),
            appkey: self.appkey.to_string(),
            user_id: self.user_id.to_string(),
            hello_timestamp: self.hello_timestamp,
            last_update_timestamp: self.last_update_timestamp,
            device_ip_addr: self.device_ip_addr.to_string(),
            sys_info,
        }
    }

    pub async fn process_message(&mut self, _who: String, data: Bytes) -> bool {
        let m = CmsPanelMessage::decode(data);
        if let Err(e) = m {
            tracing::error!("parse error: {:?}", e);
            return false;
        }
        let m = m.unwrap();
        if m.msg_type == CmsPanelMessageType::KCmsPanelHello {
            let sub = m.hello.unwrap();
            self.hello_timestamp = px_base::get_current_timestamp();
            self.last_update_timestamp = self.hello_timestamp;
            let device_id = sub.device_id;
            self.user_id = sub.user_id;
            self.device_name = sub.device_name;
            self.send_hello(device_id, self.user_id.clone()).await;
        } else if m.msg_type == CmsPanelMessageType::KCmsPanelHeartBeat {
            let sub = m.heartbeat.unwrap();
            self.last_update_timestamp = px_base::get_current_timestamp();
            let hb_index = sub.hb_index;
            self.hb_index = hb_index;
            self.user_id = sub.user_id.clone();
            self.device_name = sub.device_name;
            self.device_ip_addr = sub.device_ip_addr;
            //tracing::info!("==> sys info raw: {}", sub.sys_info_raw);
            if !sub.sys_info_raw.is_empty() {
                let sys_info = serde_json::from_str::<SysInfo>(sub.sys_info_raw.clone().as_str());
                if let Ok(sys_info) = sys_info {
                    self.sys_info_array.push(sys_info);
                    if self.sys_info_array.len() > 180 {
                        self.sys_info_array.remove(0);
                    }
                } else {
                    tracing::error!(
                        "parse sys info raw to SysInfo failed! {}",
                        sys_info.err().unwrap()
                    );
                }
            } else {
                tracing::warn!("==> sys info raw is empty!");
            }
            if !self.sys_info_array.is_empty() {
                //tracing::info!("panel heartbeat msg, self.sys_info, readable ts: {} cpu 0 using: {:?}, cpu 1 using: {}",
                //    self.sys_info_array[0].timestamp_readable, self.sys_info_array[0].cpu.cpus[0].using, self.sys_info_array[0].cpu.cpus[1].using);
            }
            self.send_heartbeat(hb_index, self.device_id.clone(), self.user_id.clone())
                .await;
        }

        true
    }

    async fn send_hello(&mut self, device_id: String, user_id: String) {
        let mut pl_msg = CmsPanelMessage::default();
        pl_msg.set_msg_type(CmsPanelMessageType::KCmsPanelHello);
        pl_msg.hello = Some(CmsPanelHello {
            device_id,
            user_id,
            device_name: self.device_name.clone(),
        });
        let buffer = pl_msg.encode_to_vec();
        self.send_bin_message_vec(buffer).await;
    }

    async fn send_heartbeat(&mut self, hb_index: i64, device_id: String, user_id: String) {
        let mut pl_msg = CmsPanelMessage::default();
        pl_msg.set_msg_type(CmsPanelMessageType::KCmsPanelHeartBeat);
        pl_msg.heartbeat = Some(CmsPanelHeartBeat {
            hb_index,
            device_id,
            desktop_link: "".to_string(),
            desktop_link_raw: "".to_string(),
            sys_info_raw: "".to_string(),
            user_id,
            device_ip_addr: "".to_string(),
            device_name: "".to_string(),
        });
        self.send_bin_message_vec(pl_msg.encode_to_vec()).await;
    }

    pub async fn send_bin_message_vec(&mut self, data: Vec<u8>) {
        self.send_bin_message_bytes(Bytes::from(data)).await;
    }

    pub async fn send_bin_message_bytes(&mut self, om: Bytes) -> bool {
        // send message
        let _size = om.len();
        let r = self.sender.lock().await.send(Message::Binary(om)).await;
        if let Err(r) = r {
            tracing::error!("error sending relay message: {r}");
            return false;
        }
        true
    }
}
