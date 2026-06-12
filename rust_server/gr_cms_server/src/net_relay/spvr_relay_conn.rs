use crate::config::spvr_server_config::SpvrServerConfig;
use crate::spvr_context::SpvrContext;
use crate::spvr_defs::{KEY_GRPC_PORT, KEY_LOCAL_IP, KEY_SERVER_ID, KEY_SERVER_NAME, KEY_SERVER_TYPE, KEY_W3C_IP, KEY_WORKING_PORT};
use crate::spvr_grpc_ws_client_trait::SpvrGrpcWsClientTrait;
use crate::gSpvrGrpcRelayClientMgr;
use axum::body::Bytes;
use axum::extract::ws::{Message, Utf8Bytes, WebSocket};
use gr_base::StrMap;
use futures_util::stream::SplitSink;
use prost::Message as ProstMessage;
use protocol::spvr_relay::{SpvrRelayMessage, SpvrRelayMessageType};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::Mutex;

// Relay Server Will Connect Here //

pub type SpvrRelayConnPtr = Arc<Mutex<SpvrRelayConn>>;

#[derive(Clone)]
pub struct SpvrRelayConn {
    pub context: Arc<Mutex<SpvrContext>>,
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

impl SpvrRelayConn {
    pub async fn new(context: Arc<Mutex<SpvrContext>>,
                     sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
                     server_id: String) -> SpvrRelayConn {
        SpvrRelayConn {
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
        let m = SpvrRelayMessage::decode(data);
        if let Err(e) = m {
            tracing::error!("SpvrRelayConn parse error: {:?}", e);
            return false;
        }

        let m = m.unwrap();
        let server_id = m.server_id;
        let msg_type = m.msg_type;

        if msg_type == SpvrRelayMessageType::KHello {
            let m_hello = m.hello.unwrap();
            tracing::info!("hello message info: {:#?}", m_hello);
            
            self.srv_name = m_hello.srv_name.clone();
            self.srv_w3c_ip = m_hello.srv_w3c_ip.to_string();
            self.srv_local_ip = who.to_string();
            self.srv_grpc_port = m_hello.srv_grpc_port as u16;
            self.srv_working_port = m_hello.srv_working_port as u16;
            self.srv_appkey = m_hello.appkey.clone();
            tracing::info!("Spvr conn, server name: {}, w3c ip: {}, grpc port: {}, working port: {}, appkey: {}",
                self.srv_name, self.srv_w3c_ip, self.srv_grpc_port, self.srv_working_port, self.srv_appkey);

            gSpvrGrpcRelayClientMgr
                .lock().await
                .on_ws_hello(self.srv_local_ip.clone(), m_hello).await
        }
        else if msg_type == SpvrRelayMessageType::KHeartBeat {
            let m_heartbeat = m.heartbeat.unwrap();
            self.srv_hb_index = m_heartbeat.hb_index;
            gSpvrGrpcRelayClientMgr
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
    // gr_relay_server server
    // device server
    pub async fn get_server_info(&self) -> SpvrServerConfig {
        SpvrServerConfig {
            srv_id: self.srv_id.clone(),
            srv_name: self.srv_name.clone(),
            srv_type: "relay".to_string(),
            srv_w3c_ip: self.srv_w3c_ip.clone(),
            srv_spvr_port: self.srv_working_port,
            srv_udp_broadcast_port: self.srv_udp_broadcast_port,
            srv_relay_port: 0,
            srv_appkey: self.srv_appkey.clone(),
        }
    }
}