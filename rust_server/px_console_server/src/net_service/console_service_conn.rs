use crate::console_context::ConsoleContext;
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use futures_util::stream::SplitSink;
use futures_util::SinkExt;
use prost::Message as ProstMessage;
use protocol::console_service::{ConsoleServiceCreateWallSession, ConsoleServiceHeartBeat, ConsoleServiceHello, ConsoleServiceMessage,
                                ConsoleServiceMessageType, ConsoleServiceValidateRdpSessionResult, RtcIceConfigChanged};
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::Mutex;

pub type ConsoleServiceConnPtr = Arc<Mutex<ConsoleServiceConn>>;

#[derive(Clone)]
pub struct ConsoleServiceConn {
    pub context: Arc<Mutex<ConsoleContext>>,
    pub sender: Option<Arc<Mutex<SplitSink<WebSocket, Message>>>>,
    pub device_id: String,
    pub appkey: String,
    pub version: String,
    pub rdp_available: bool,
    pub rdp_domain: String,
    pub rdp_proxy_certificate_sha256: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    pub render_alive: bool,
    pub auth_info_json: String,
    pub instances_json: String,
    pub logical_sessions_json: String,
    pub node_endpoints: Option<protocol::console_service::NodeEndpoints>,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ConsoleServiceConnVo {
    pub node_endpoints: Option<protocol::console_service::NodeEndpoints>,
    #[serde(default)]
    pub rdp_available: bool,
    pub device_id: String,
    pub version: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    pub render_alive: bool,
    pub auth_info_json: String,
    #[serde(default)]
    pub instances_json: String,
    #[serde(default)]
    pub logical_sessions_json: String,
}

impl ConsoleServiceConn {
    //
    pub async fn new(
        context: Arc<Mutex<ConsoleContext>>,
        sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
        device_id: String,
        appkey: String,
    ) -> ConsoleServiceConn {
        Self {
            context,
            sender: Some(sender),
            device_id,
            appkey,
            version: "".to_string(),
            rdp_available: false,
            rdp_domain: String::new(),
            rdp_proxy_certificate_sha256: String::new(),
            hello_timestamp: 0,
            last_update_timestamp: 0,
            hb_index: 0,
            render_alive: false,
            auth_info_json: "".to_string(),
            instances_json: "".to_string(),
            logical_sessions_json: "[]".to_string(),
            node_endpoints: None,
        }
    }

    pub fn as_info(&self) -> ConsoleServiceConnVo {
        ConsoleServiceConnVo {
            node_endpoints: self.node_endpoints.clone(),
            rdp_available: self.rdp_available,
            device_id: self.device_id.to_string(),
            version: self.version.to_string(),
            hello_timestamp: self.hello_timestamp,
            last_update_timestamp: self.last_update_timestamp,
            hb_index: self.hb_index,
            render_alive: self.render_alive,
            auth_info_json: self.auth_info_json.to_string(),
            instances_json: self.instances_json.to_string(),
            logical_sessions_json: self.logical_sessions_json.to_string(),
        }
    }

    pub async fn process_message(&mut self, _who: String, data: Bytes) -> bool {
        let m = ConsoleServiceMessage::decode(data);
        if let Err(e) = m {
            tracing::error!("parse error: {:?}", e);
            return false;
        }
        let m = m.unwrap();
        if !m.device_id.is_empty() && m.device_id != self.device_id {
            return false;
        }
        if m.msg_type == ConsoleServiceMessageType::KConsoleServiceHello {
            let Some(sub) = m.hello else {
                tracing::warn!("service hello message without hello body!");
                return true;
            };
            self.hello_timestamp = px_base::get_current_timestamp();
            if sub.device_id != self.device_id || sub.appkey != self.appkey {
                return false;
            }
            self.last_update_timestamp = self.hello_timestamp;
            let device_id = sub.device_id;
            self.version = sub.version;
            self.rdp_available = sub.rdp_available;
            self.rdp_domain = sub.rdp_domain;
            self.rdp_proxy_certificate_sha256 = sub.rdp_proxy_certificate_sha256;
            self.rdp_available &= !self.rdp_domain.is_empty()
                && self.rdp_domain.len() <= 15
                && self
                    .rdp_domain
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
                && self.rdp_proxy_certificate_sha256.len() == 64
                && self
                    .rdp_proxy_certificate_sha256
                    .bytes()
                    .all(|byte| byte.is_ascii_hexdigit());
            self.send_hello(device_id).await;
        } else if m.msg_type == ConsoleServiceMessageType::KConsoleServiceHeartBeat {
            let Some(sub) = m.heartbeat else {
                tracing::warn!("service heartbeat message without heartbeat body!");
                return true;
            };
            if sub.device_id != self.device_id {
                return false;
            }
            if sub
                .node_endpoints
                .as_ref()
                .is_some_and(|report| report.validate().is_err())
            {
                self.node_endpoints = None;
                tracing::warn!("rejecting invalid node endpoints for {}", self.device_id);
                return false;
            }
            self.node_endpoints = sub.node_endpoints;
            self.last_update_timestamp = px_base::get_current_timestamp();
            let hb_index = sub.hb_index;
            self.hb_index = hb_index;
            self.render_alive = sub.render_alive;
            self.auth_info_json = sub.auth_info_json;
            self.instances_json = sub.instances_json;
            self.logical_sessions_json = sub.logical_sessions_json;
            let database_ready = {
                let database = crate::gConsoleDatabase.lock().await;
                database.c_remote_session.is_some() && database.c_remote_session_event.is_some()
            };
            if database_ready {
                crate::gRemoteSessionManager
                    .reconcile_snapshot(
                        self.device_id.clone(),
                        self.logical_sessions_json.clone(),
                        self.last_update_timestamp,
                    )
                    .await;
            }
            crate::app_schedule::gAppScheduleManager
                .reconcile_from_service_hb(self.device_id.clone(), &self.instances_json)
                .await;
            self.send_heartbeat(hb_index, self.device_id.clone()).await;
        } else if m.msg_type == ConsoleServiceMessageType::KConsoleServiceStartAppInstanceResult {
            if let Some(sub) = m.start_app_instance_result {
                crate::app_schedule::gAppScheduleManager
                    .on_start_result(self.device_id.clone(), sub)
                    .await;
            }
        } else if m.msg_type == ConsoleServiceMessageType::KConsoleServiceStopAppInstanceResult {
            if let Some(sub) = m.stop_app_instance_result {
                crate::app_schedule::gAppScheduleManager
                    .on_stop_result(self.device_id.clone(), sub)
                    .await;
            }
        } else if m.msg_type == ConsoleServiceMessageType::KConsoleServiceCreateWallSessionResult {
            if let Some(sub) = m.create_wall_session_result {
                crate::wall::console_wall_handler::on_wall_session_result(sub).await;
            }
        } else if m.msg_type == ConsoleServiceMessageType::KConsoleServiceValidateRdpSession {
            let Some(request) = m.validate_rdp_session else {
                tracing::warn!("RDP session validation message without request body");
                return true;
            };
            let request_id = request.request_id.clone();
            let result = crate::rdp_session_authorization::validate(&self.device_id, &request.instance_id, &request.logical_session_id).await;
            let response = match result {
                Ok(authorization) => {
                    tracing::debug!("RDP runtime authorization confirmed");
                    ConsoleServiceValidateRdpSessionResult {
                        request_id,
                        ok: true,
                        code: "OK".to_string(),
                        device_id: authorization.device_id,
                        instance_id: authorization.instance_id,
                        subject_type: authorization.subject_type,
                        subject_id: authorization.subject_id,
                        logical_session_id: authorization.logical_session_id,
                    }
                }
                Err(error) => {
                    tracing::warn!(request_id = %request_id, "RDP runtime authorization rejected");
                    ConsoleServiceValidateRdpSessionResult {
                        request_id,
                        ok: false,
                        code: match error {
                            crate::console_api_error::ConsoleApiError::InvalidParams => "INVALID_ARGUMENT",
                            _ => "RDP_SESSION_REJECTED",
                        }
                        .to_string(),
                        ..Default::default()
                    }
                }
            };
            self.send_rdp_validation_result(response).await;
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
        let mut sv_msg = ConsoleServiceMessage::default();
        sv_msg.set_msg_type(ConsoleServiceMessageType::KConsoleServiceHello);
        sv_msg.device_id = device_id.clone();
        sv_msg.hello = Some(ConsoleServiceHello {
            device_id,
            appkey: self.appkey.clone(),
            version: self.version.clone(),
            rdp_available: self.rdp_available,
            rdp_domain: self.rdp_domain.clone(),
            rdp_proxy_certificate_sha256: self.rdp_proxy_certificate_sha256.clone(),
        });
        let buffer = sv_msg.encode_to_vec();
        self.send_bin_message_vec(buffer).await;
    }

    async fn send_heartbeat(&mut self, hb_index: i64, device_id: String) {
        let mut sv_msg = ConsoleServiceMessage::default();
        sv_msg.set_msg_type(ConsoleServiceMessageType::KConsoleServiceHeartBeat);
        sv_msg.device_id = device_id.clone();
        sv_msg.heartbeat = Some(ConsoleServiceHeartBeat {
            hb_index,
            device_id,
            render_alive: false,
            auth_info_json: "".to_string(),
            instances_json: "".to_string(),
            logical_sessions_json: "".to_string(),
            node_endpoints: None,
        });
        self.send_bin_message_vec(sv_msg.encode_to_vec()).await;
    }

    pub async fn send_start_app_instance(
        &mut self,
        start: protocol::console_service::ConsoleServiceStartAppInstance,
    ) -> bool {
        let mut sv_msg = ConsoleServiceMessage::default();
        sv_msg.set_msg_type(ConsoleServiceMessageType::KConsoleServiceStartAppInstance);
        sv_msg.device_id = self.device_id.clone();
        sv_msg.start_app_instance = Some(start);
        self.send_bin_message_bytes(Bytes::from(sv_msg.encode_to_vec()))
            .await
    }

    pub async fn send_stop_app_instance(
        &mut self,
        stop: protocol::console_service::ConsoleServiceStopAppInstance,
    ) -> bool {
        let mut sv_msg = ConsoleServiceMessage::default();
        sv_msg.set_msg_type(ConsoleServiceMessageType::KConsoleServiceStopAppInstance);
        sv_msg.device_id = self.device_id.clone();
        sv_msg.stop_app_instance = Some(stop);
        self.send_bin_message_bytes(Bytes::from(sv_msg.encode_to_vec()))
            .await
    }

    pub async fn send_create_wall_session(
        &mut self,
        request: ConsoleServiceCreateWallSession,
    ) -> bool {
        let mut sv_msg = ConsoleServiceMessage::default();
        sv_msg.set_msg_type(ConsoleServiceMessageType::KConsoleServiceCreateWallSession);
        sv_msg.device_id = self.device_id.clone();
        sv_msg.create_wall_session = Some(request);
        self.send_bin_message_bytes(Bytes::from(sv_msg.encode_to_vec()))
            .await
    }

    async fn send_rdp_validation_result(&mut self, response: ConsoleServiceValidateRdpSessionResult) -> bool {
        let mut message = ConsoleServiceMessage::default();
        message.set_msg_type(ConsoleServiceMessageType::KConsoleServiceValidateRdpSessionResult);
        message.device_id = self.device_id.clone();
        message.validate_rdp_session_result = Some(response);
        self.send_bin_message_bytes(Bytes::from(message.encode_to_vec()))
            .await
    }

    pub async fn send_bin_message_vec(&mut self, data: Vec<u8>) {
        self.send_bin_message_bytes(Bytes::from(data)).await;
    }

    pub async fn send_rtc_ice_config_changed(&mut self, revision: u64, changed_at: i64) -> bool {
        let mut message = ConsoleServiceMessage::default();
        message.set_msg_type(ConsoleServiceMessageType::KRtcIceConfigChanged);
        message.device_id = self.device_id.clone();
        message.rtc_ice_config_changed = Some(RtcIceConfigChanged {
            revision,
            changed_at,
        });
        self.send_bin_message_bytes(Bytes::from(message.encode_to_vec()))
            .await
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
