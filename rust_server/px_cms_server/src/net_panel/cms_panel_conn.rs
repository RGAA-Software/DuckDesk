use crate::cms_context::CmsContext;
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use futures_util::stream::SplitSink;
use futures_util::SinkExt;
use prost::Message as ProstMessage;
use protocol::cms_panel::{CmsPanelHeartBeat, CmsPanelHello, CmsPanelMessage, CmsPanelMessageType};
use px_base::sys_info::SysInfo;
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
    // local NIC IPv4 list reported in CmsPanelHello (design doc 5.2)
    pub panel_lan_ips: Vec<String>,
    pub panel_http_port: i32,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct CmsPanelConnVo {
    pub device_id: String,
    pub device_name: String,
    pub user_id: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub device_ip_addr: String,
    pub sys_info: SysInfo,
    #[serde(default)]
    pub panel_lan_ips: Vec<String>,
    #[serde(default)]
    pub panel_http_port: i32,
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
            panel_lan_ips: Default::default(),
            panel_http_port: 0,
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
            user_id: self.user_id.to_string(),
            hello_timestamp: self.hello_timestamp,
            last_update_timestamp: self.last_update_timestamp,
            device_ip_addr: self.device_ip_addr.to_string(),
            sys_info,
            panel_lan_ips: self.panel_lan_ips.clone(),
            panel_http_port: self.panel_http_port,
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
            // panel reports its local NIC ips at handshake (design doc 5.2);
            // keep them on the conn and persist into the device table
            self.panel_lan_ips = sub.panel_lan_ips.clone();
            self.panel_http_port = sub.panel_http_port;
            {
                let device_id = device_id.clone();
                let ips = sub.panel_lan_ips;
                let port = sub.panel_http_port as i64;
                tokio::spawn(async move {
                    if let Err(e) = crate::gDeviceManager
                        .update_device_field(device_id.clone(), "panel_lan_ips".to_string(), ips)
                        .await
                    {
                        tracing::warn!("persist panel_lan_ips for {} failed: {:?}", device_id, e);
                    }
                    if port > 0 {
                        if let Err(e) = crate::gDeviceManager
                            .update_device_field(
                                device_id.clone(),
                                "panel_http_port".to_string(),
                                port,
                            )
                            .await
                        {
                            tracing::warn!(
                                "persist panel_http_port for {} failed: {:?}",
                                device_id,
                                e
                            );
                        }
                    }
                });
            }
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
        } else if m.msg_type == CmsPanelMessageType::KRecordListResp {
            // record tunnel family (design doc 6.2)
            if let Some(resp) = m.record_list_resp {
                if !crate::gRecordTunnel.complete_list(resp) {
                    tracing::warn!("record list resp with unknown req_id, dropped");
                }
            }
        } else if m.msg_type == CmsPanelMessageType::KRecordFetchDone {
            if let Some(done) = m.record_fetch_done {
                if done.ok {
                    tracing::info!("record fetch done: {}/{}", done.device_id, done.filename);
                } else {
                    tracing::error!(
                        "record fetch failed: {}/{}: {}",
                        done.device_id,
                        done.filename,
                        done.error
                    );
                    let _ = crate::gRenderRecordManager
                        .mark_error(&done.device_id, &done.filename, &done.error)
                        .await;
                }
                crate::gRecordTunnel.remove_inflight(&done.device_id, &done.filename);
            }
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
            ..Default::default()
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
