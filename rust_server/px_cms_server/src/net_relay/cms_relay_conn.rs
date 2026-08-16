use crate::config::cms_server_config::CmsServerConfig;
use crate::cms_context::CmsContext;
use crate::cms_defs::{KEY_GRPC_PORT, KEY_LOCAL_IP, KEY_SERVER_ID, KEY_SERVER_NAME, KEY_SERVER_TYPE, KEY_W3C_IP, KEY_WORKING_PORT};
use crate::cms_grpc_ws_client_trait::CmsGrpcWsClientTrait;
use crate::gCmsGrpcRelayClientMgr;
use axum::body::Bytes;
use axum::extract::ws::{Message, Utf8Bytes, WebSocket};
use px_base::StrMap;
use futures_util::stream::SplitSink;
use prost::Message as ProstMessage;
use protocol::cms_relay::{CmsRelayMessage, CmsRelayMessageType};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

// Relay Server Will Connect Here //

pub type CmsRelayConnPtr = Arc<Mutex<CmsRelayConn>>;

#[derive(Clone)]
pub struct CmsRelayConn {
    pub context: Arc<Mutex<CmsContext>>,
    pub sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
    pub srv_id: String,
    pub srv_name: String,
    pub srv_w3c_ip: String,
    pub srv_local_ip: String,
    pub srv_grpc_port: u16,
    pub srv_working_port: u16,
    pub srv_udp_broadcast_port: u16,
    pub srv_hb_index: i64,
    pub srv_appkey: String,
}

impl CmsRelayConn {
    pub async fn new(context: Arc<Mutex<CmsContext>>,
                     sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
                     server_id: String) -> CmsRelayConn {
        CmsRelayConn {
            context,
            sender,
            srv_id: server_id,
            srv_name: "".to_string(),
            srv_w3c_ip: "".to_string(),
            srv_local_ip: "".to_string(),
            srv_grpc_port: 0,
            srv_working_port: 0,
            srv_udp_broadcast_port: 0,
            srv_hb_index: 0,
            srv_appkey: "".to_string(),
        }
    }
    
    pub async fn process_bin_message(&mut self, who: String, data: Bytes) -> bool {
        let m = CmsRelayMessage::decode(data);
        if let Err(e) = m {
            tracing::error!("CmsRelayConn parse error: {:?}", e);
            return false;
        }

        let m = m.unwrap();
        let server_id = m.server_id;
        let msg_type = m.msg_type;

        if msg_type == CmsRelayMessageType::KHello {
            let m_hello = m.hello.unwrap();
            tracing::info!("hello message info: {:#?}", m_hello);
            
            self.srv_name = m_hello.srv_name.clone();
            self.srv_w3c_ip = m_hello.srv_w3c_ip.to_string();
            self.srv_local_ip = who.to_string();
            self.srv_grpc_port = m_hello.srv_grpc_port as u16;
            self.srv_working_port = m_hello.srv_working_port as u16;
            self.srv_appkey = m_hello.appkey.clone();
            tracing::info!("Cms conn, server name: {}, w3c ip: {}, grpc port: {}, working port: {}, appkey: {}",
                self.srv_name, self.srv_w3c_ip, self.srv_grpc_port, self.srv_working_port, self.srv_appkey);

            gCmsGrpcRelayClientMgr
                .lock().await
                .on_ws_hello(self.srv_local_ip.clone(), m_hello).await
        }
        else if msg_type == CmsRelayMessageType::KHeartBeat {
            let m_heartbeat = m.heartbeat.unwrap();
            self.srv_hb_index = m_heartbeat.hb_index;
            gCmsGrpcRelayClientMgr
                .lock().await
                .on_ws_heartbeat(m_heartbeat).await;
        }

        true
    }

    pub async fn process_text_message(&self, data: Utf8Bytes) -> bool {
        let value: serde_json::error::Result<serde_json::Value> = serde_json::from_str(data.as_str());
        if let Err(e) = value {
            tracing::error!("parse json error: {e}, json: {}", data.to_string());
            return false;
        }
        
        true
    }

    // connected server info; w3cip, ip, grpc port, etc
    // px_relay_server server
    // device server
    pub async fn get_server_info(&self) -> CmsServerConfig {
        CmsServerConfig {
            srv_id: self.srv_id.clone(),
            srv_name: self.srv_name.clone(),
            srv_type: "relay".to_string(),
            srv_w3c_ip: self.srv_w3c_ip.clone(),
            srv_cms_port: self.srv_working_port,
            srv_udp_broadcast_port: self.srv_udp_broadcast_port,
            srv_relay_port: 0,
            srv_appkey: self.srv_appkey.clone(),
        }
    }
}