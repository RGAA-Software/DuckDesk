use crate::console_context::ConsoleContext;
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use futures_util::stream::SplitSink;
use futures_util::SinkExt;
use prost::Message as ProstMessage;
use protocol::console_service::{
    ConsoleConnectionGrant, ConsoleServiceCreateWallSession, ConsoleServiceHeartBeat, ConsoleServiceHello,
    ConsoleServiceMessage, ConsoleServiceMessageType, ConsoleServiceRedeemConnectionTicketResult,
};
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
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    pub render_alive: bool,
    pub auth_info_json: String,
    pub instances_json: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ConsoleServiceConnVo {
    pub device_id: String,
    pub version: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    pub render_alive: bool,
    pub auth_info_json: String,
    #[serde(default)]
    pub instances_json: String,
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
            hello_timestamp: 0,
            last_update_timestamp: 0,
            hb_index: 0,
            render_alive: false,
            auth_info_json: "".to_string(),
            instances_json: "".to_string(),
        }
    }

    pub fn as_info(&self) -> ConsoleServiceConnVo {
        ConsoleServiceConnVo {
            device_id: self.device_id.to_string(),
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
        let m = ConsoleServiceMessage::decode(data);
        if let Err(e) = m {
            tracing::error!("parse error: {:?}", e);
            return false;
        }
        let m = m.unwrap();
        if m.msg_type == ConsoleServiceMessageType::KConsoleServiceHello {
            let Some(sub) = m.hello else {
                tracing::warn!("service hello message without hello body!");
                return true;
            };
            self.hello_timestamp = px_base::get_current_timestamp();
            self.last_update_timestamp = self.hello_timestamp;
            let device_id = sub.device_id;
            self.version = sub.version;
            self.send_hello(device_id).await;
        } else if m.msg_type == ConsoleServiceMessageType::KConsoleServiceHeartBeat {
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
        } else if m.msg_type == ConsoleServiceMessageType::KConsoleServiceRedeemConnectionTicket {
            let Some(request) = m.redeem_connection_ticket else {
                tracing::warn!("ticket redemption message without request body");
                return true;
            };
            let request_id = request.request_id.clone();
            let instance_id =
                (!request.instance_id.is_empty()).then_some(request.instance_id.as_str());
            let result = crate::connection_ticket::manager::ConnectionTicketManager::redeem(
                &request.ticket,
                &self.device_id,
                &request.client_nonce,
                instance_id,
                &request_id,
            )
            .await;
            let response = match result {
                Ok(grant) => ConsoleServiceRedeemConnectionTicketResult {
                    request_id,
                    ok: true,
                    code: "OK".to_string(),
                    grant: Some(ConsoleConnectionGrant {
                        kind: grant.kind,
                        device_id: grant.device_id,
                        app_id: grant.app_id.unwrap_or_default(),
                        instance_id: grant.instance_id.unwrap_or_default(),
                        subject_type: grant.subject_type,
                        subject_id: grant.subject_id,
                        permissions: grant.permissions,
                        expires_at: grant.expires_at,
                    }),
                },
                Err(error) => {
                    tracing::warn!(request_id = %request_id, "connection ticket redemption rejected");
                    ConsoleServiceRedeemConnectionTicketResult {
                        request_id,
                        ok: false,
                        code: match error {
                            crate::console_api_error::ConsoleApiError::TicketExpiredOrUsed => {
                                "TICKET_EXPIRED_OR_USED"
                            }
                            crate::console_api_error::ConsoleApiError::InvalidParams => "INVALID_ARGUMENT",
                            _ => "TICKET_REJECTED",
                        }
                        .to_string(),
                        grant: None,
                    }
                }
            };
            self.send_redeem_result(response).await;
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

    pub async fn send_create_wall_session(&mut self, request: ConsoleServiceCreateWallSession) -> bool {
        let mut sv_msg = ConsoleServiceMessage::default();
        sv_msg.set_msg_type(ConsoleServiceMessageType::KConsoleServiceCreateWallSession);
        sv_msg.device_id = self.device_id.clone();
        sv_msg.create_wall_session = Some(request);
        self.send_bin_message_bytes(Bytes::from(sv_msg.encode_to_vec()))
            .await
    }

    async fn send_redeem_result(
        &mut self,
        response: ConsoleServiceRedeemConnectionTicketResult,
    ) -> bool {
        let mut message = ConsoleServiceMessage::default();
        message.set_msg_type(ConsoleServiceMessageType::KConsoleServiceRedeemConnectionTicketResult);
        message.device_id = self.device_id.clone();
        message.redeem_connection_ticket_result = Some(response);
        self.send_bin_message_bytes(Bytes::from(message.encode_to_vec()))
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
