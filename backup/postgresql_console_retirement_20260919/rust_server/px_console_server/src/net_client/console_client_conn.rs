use crate::console_context::ConsoleContext;
use crate::gConsoleClientConnMgr;
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use futures_util::stream::SplitSink;
use prost::Message as ProstMessage;
use protocol::console_client::{ConsoleClientMessage, ConsoleClientMessageType};
use px_base::md5_hex;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::Mutex;

pub type ConsoleClientConnPtr = Arc<Mutex<ConsoleClientConn>>;

#[derive(Clone)]
pub struct ConsoleClientConn {
    pub context: Arc<Mutex<ConsoleContext>>,
    pub sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
    // a random id for this connection
    pub conn_id: String,
    pub device_id: String,
    pub remote_device_id: String,
    pub remote_device_ip: String,
    pub appkey: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    // sdk connection still has messages ?
    // remote device is still sending frames ?
    pub connection_alive: bool,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct ConsoleClientConnVo {
    #[serde(default)]
    pub conn_id: String,

    #[serde(default)]
    pub device_id: String,

    #[serde(default)]
    pub remote_device_id: String,

    #[serde(default)]
    pub remote_device_ip: String,

    #[serde(default)]
    pub appkey: String,

    #[serde(default)]
    pub hello_timestamp: i64,

    #[serde(default)]
    pub readable_hello_ts: String,

    #[serde(default)]
    pub last_update_timestamp: i64,

    #[serde(default)]
    pub readable_update_ts: String,
}

impl ConsoleClientConn {
    //
    pub async fn new(
        context: Arc<Mutex<ConsoleContext>>,
        sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
        device_id: String,
        remote_device_id: String,
        remote_device_ip: String,
        appkey: String,
    ) -> Self {
        let seed = format!(
            "{}{}{}",
            device_id,
            remote_device_id,
            px_base::get_current_timestamp()
        );
        let conn_id = md5_hex(&seed);
        Self {
            context,
            sender,
            conn_id,
            device_id,
            remote_device_id,
            remote_device_ip,
            appkey,
            hello_timestamp: px_base::get_current_timestamp(),
            last_update_timestamp: px_base::get_current_timestamp(),
            hb_index: 0,
            connection_alive: false,
        }
    }

    pub fn as_vo(&self) -> ConsoleClientConnVo {
        ConsoleClientConnVo {
            conn_id: self.conn_id.clone(),
            device_id: self.device_id.clone(),
            remote_device_id: self.remote_device_id.clone(),
            remote_device_ip: self.remote_device_ip.clone(),
            appkey: self.appkey.clone(),
            hello_timestamp: self.hello_timestamp,
            readable_hello_ts: px_base::format_readable_timestamp(self.hello_timestamp),
            last_update_timestamp: self.last_update_timestamp,
            readable_update_ts: px_base::format_readable_timestamp(self.last_update_timestamp),
        }
    }

    pub async fn process_message(&mut self, _who: String, message_bytes: Bytes) -> bool {
        let decoded_message = ConsoleClientMessage::decode(message_bytes);
        if let Err(decode_error) = decoded_message {
            tracing::error!("console client parse error: {:?}", decode_error);
            return false;
        }
        let decoded_message = decoded_message.unwrap();
        let _device_id = decoded_message.device_id;
        let msg_type = decoded_message.msg_type;
        if msg_type == ConsoleClientMessageType::KConsoleClientHello {
            let _hello_message = decoded_message.hello.unwrap();
            // update time
            self.hello_timestamp = px_base::get_current_timestamp();
            self.connection_alive = true;

            // insert to db
            gConsoleClientConnMgr.insert_conn(self.as_vo()).await;
        } else if msg_type == ConsoleClientMessageType::KConsoleClientHeartBeat {
            let heartbeat_message = decoded_message.heartbeat.unwrap();
            self.hb_index = heartbeat_message.hb_index;
            self.last_update_timestamp = px_base::get_current_timestamp();
            self.connection_alive = heartbeat_message.connection_alive;

            // update database
            if self.hb_index % 5 == 0 {
                gConsoleClientConnMgr
                    .update_conn(self.conn_id.clone(), self.last_update_timestamp)
                    .await;
            }
        }
        true
    }
}
