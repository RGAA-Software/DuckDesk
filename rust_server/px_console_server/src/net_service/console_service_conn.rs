use crate::console_context::ConsoleContext;
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use futures_util::stream::SplitSink;
use futures_util::SinkExt;
use prost::Message as ProstMessage;
use protocol::console_service::{
    ConsoleServiceCreateWallSession, ConsoleServiceHeartBeat, ConsoleServiceHello,
    ConsoleServiceMessage, ConsoleServiceMessageType, ConsoleServiceValidateRdpSessionResult,
    RtcIceConfigChanged,
};
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::sync::Arc;
use tokio::sync::Mutex;

pub type ConsoleServiceConnPtr = Arc<Mutex<ConsoleServiceConn>>;

pub(super) const CLOUD_NODE_CAPABILITIES: &[&str] = &[
    "browser_remote",
    "cloud_app_catalog",
    "cloud_app_host",
    "desktop_client",
    "desktop_host",
    "file_transfer",
    "game_hook",
    "joystick",
    "rdp_client",
    "rdp_host",
    "system_information",
    "virtual_display",
    "webview_host",
];
pub(super) const REMOTE_CAPABILITIES: &[&str] = &[
    "browser_remote",
    "desktop_client",
    "desktop_host",
    "file_transfer",
    "joystick",
    "rdp_client",
    "rdp_host",
    "system_information",
    "virtual_display",
];

fn validate_product_identity(hello: &ConsoleServiceHello) -> Result<(), &'static str> {
    if hello.company != "Pixels"
        || hello.product_version_code == 0
        || hello.product_version.split('.').count() != 3
    {
        return Err("invalid Pixels product identity");
    }
    let expected = match (hello.product.as_str(), hello.edition.as_str()) {
        ("cloud_node", "CLOUD_NODE") => CLOUD_NODE_CAPABILITIES,
        ("remote", "REMOTE") => REMOTE_CAPABILITIES,
        _ => return Err("service reported an unsupported product edition"),
    };
    let actual: HashSet<&str> = hello.capabilities.iter().map(String::as_str).collect();
    let required: HashSet<&str> = expected.iter().copied().collect();
    if actual.len() != hello.capabilities.len() || actual != required {
        return Err("service capability set does not match its edition");
    }
    Ok(())
}

#[derive(Clone)]
pub struct ConsoleServiceConn {
    pub context: Arc<Mutex<ConsoleContext>>,
    pub sender: Option<Arc<Mutex<SplitSink<WebSocket, Message>>>>,
    pub device_id: String,
    pub appkey: String,
    pub version: String,
    pub company: String,
    pub product: String,
    pub edition: String,
    pub product_version: String,
    pub product_version_code: u32,
    pub capabilities: Vec<String>,
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
    pub company: String,
    pub product: String,
    pub edition: String,
    pub product_version: String,
    pub product_version_code: u32,
    pub capabilities: Vec<String>,
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
    pub fn supports_capability(&self, capability: &str) -> bool {
        self.capabilities.iter().any(|value| value == capability)
    }

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
            company: String::new(),
            product: String::new(),
            edition: String::new(),
            product_version: String::new(),
            product_version_code: 0,
            capabilities: Vec::new(),
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
            company: self.company.clone(),
            product: self.product.clone(),
            edition: self.edition.clone(),
            product_version: self.product_version.clone(),
            product_version_code: self.product_version_code,
            capabilities: self.capabilities.clone(),
            hello_timestamp: self.hello_timestamp,
            last_update_timestamp: self.last_update_timestamp,
            hb_index: self.hb_index,
            render_alive: self.render_alive,
            auth_info_json: self.auth_info_json.to_string(),
            instances_json: self.instances_json.to_string(),
            logical_sessions_json: self.logical_sessions_json.to_string(),
        }
    }

    pub async fn process_message(&mut self, _who: String, message_bytes: Bytes) -> bool {
        let decoded_message = ConsoleServiceMessage::decode(message_bytes);
        if let Err(decode_error) = decoded_message {
            tracing::error!("parse error: {:?}", decode_error);
            return false;
        }
        let decoded_message = decoded_message.unwrap();
        if !decoded_message.device_id.is_empty() && decoded_message.device_id != self.device_id {
            return false;
        }
        if decoded_message.msg_type == ConsoleServiceMessageType::KConsoleServiceHello {
            let Some(sub) = decoded_message.hello else {
                tracing::warn!("service hello message without hello body!");
                return true;
            };
            self.hello_timestamp = px_base::get_current_timestamp();
            if sub.device_id != self.device_id || sub.appkey != self.appkey {
                return false;
            }
            if let Err(error) = validate_product_identity(&sub) {
                tracing::warn!(device_id = %self.device_id, error, "rejecting invalid service product identity");
                return false;
            }
            self.last_update_timestamp = self.hello_timestamp;
            let device_id = sub.device_id;
            self.version = sub.version;
            self.company = sub.company;
            self.product = sub.product;
            self.edition = sub.edition;
            self.product_version = sub.product_version;
            self.product_version_code = sub.product_version_code;
            self.capabilities = sub.capabilities;
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
        } else if decoded_message.msg_type == ConsoleServiceMessageType::KConsoleServiceHeartBeat {
            let Some(sub) = decoded_message.heartbeat else {
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
        } else if decoded_message.msg_type
            == ConsoleServiceMessageType::KConsoleServiceStartAppInstanceResult
        {
            if let Some(sub) = decoded_message.start_app_instance_result {
                crate::app_schedule::gAppScheduleManager
                    .on_start_result(self.device_id.clone(), sub)
                    .await;
            }
        } else if decoded_message.msg_type
            == ConsoleServiceMessageType::KConsoleServiceStopAppInstanceResult
        {
            if let Some(sub) = decoded_message.stop_app_instance_result {
                crate::app_schedule::gAppScheduleManager
                    .on_stop_result(self.device_id.clone(), sub)
                    .await;
            }
        } else if decoded_message.msg_type
            == ConsoleServiceMessageType::KConsoleServiceCreateWallSessionResult
        {
            if let Some(sub) = decoded_message.create_wall_session_result {
                crate::wall::console_wall_handler::on_wall_session_result(sub).await;
            }
        } else if decoded_message.msg_type
            == ConsoleServiceMessageType::KConsoleServiceValidateRdpSession
        {
            let Some(request) = decoded_message.validate_rdp_session else {
                tracing::warn!("RDP session validation message without request body");
                return true;
            };
            let request_id = request.request_id.clone();
            let result = crate::rdp_session_authorization::validate(
                &self.device_id,
                &request.instance_id,
                &request.logical_session_id,
            )
            .await;
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
                    tracing::warn!(request_id = %request_id, error = ?error, "RDP runtime authorization rejected");
                    ConsoleServiceValidateRdpSessionResult {
                        request_id,
                        ok: false,
                        code: match error {
                            crate::console_api_error::ConsoleApiError::InvalidParams => {
                                "INVALID_ARGUMENT"
                            }
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
            company: self.company.clone(),
            product: self.product.clone(),
            edition: self.edition.clone(),
            product_version: self.product_version.clone(),
            product_version_code: self.product_version_code,
            capabilities: self.capabilities.clone(),
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

    async fn send_rdp_validation_result(
        &mut self,
        response: ConsoleServiceValidateRdpSessionResult,
    ) -> bool {
        let mut message = ConsoleServiceMessage::default();
        message.set_msg_type(ConsoleServiceMessageType::KConsoleServiceValidateRdpSessionResult);
        message.device_id = self.device_id.clone();
        message.validate_rdp_session_result = Some(response);
        self.send_bin_message_bytes(Bytes::from(message.encode_to_vec()))
            .await
    }

    pub async fn send_bin_message_vec(&mut self, message_bytes: Vec<u8>) {
        self.send_bin_message_bytes(Bytes::from(message_bytes))
            .await;
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
        let send_result = sender.lock().await.send(Message::Binary(om)).await;
        if let Err(send_error) = send_result {
            tracing::error!("error sending service message: {send_error}");
            return false;
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hello(product: &str, edition: &str, capabilities: &[&str]) -> ConsoleServiceHello {
        ConsoleServiceHello {
            company: "Pixels".into(),
            product: product.into(),
            edition: edition.into(),
            product_version: "3.3.67".into(),
            product_version_code: 30367,
            capabilities: capabilities
                .iter()
                .map(|value| (*value).to_string())
                .collect(),
            ..Default::default()
        }
    }

    #[test]
    fn product_identity_requires_exact_edition_and_capability_set() {
        assert!(validate_product_identity(&hello(
            "cloud_node",
            "CLOUD_NODE",
            CLOUD_NODE_CAPABILITIES
        ))
        .is_ok());
        assert!(validate_product_identity(&hello("remote", "REMOTE", REMOTE_CAPABILITIES)).is_ok());

        let mut missing = REMOTE_CAPABILITIES.to_vec();
        missing.retain(|capability| *capability != "rdp_host");
        assert!(validate_product_identity(&hello("remote", "REMOTE", &missing)).is_err());

        let mut extra = REMOTE_CAPABILITIES.to_vec();
        extra.push("cloud_app_host");
        assert!(validate_product_identity(&hello("remote", "REMOTE", &extra)).is_err());

        let mut wrong_company = hello("remote", "REMOTE", REMOTE_CAPABILITIES);
        wrong_company.company = "RGAA".into();
        assert!(validate_product_identity(&wrong_company).is_err());

        assert!(
            validate_product_identity(&hello("remote", "CLOUD_NODE", REMOTE_CAPABILITIES)).is_err()
        );
    }
}
