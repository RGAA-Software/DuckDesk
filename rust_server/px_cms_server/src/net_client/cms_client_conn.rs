use crate::cms_context::CmsContext;
use crate::gCmsClientConnMgr;
use axum::body::Bytes;
use axum::extract::ws::{Message, WebSocket};
use futures_util::stream::SplitSink;
use prost::Message as ProstMessage;
use protocol::cms_client::{CmsClientMessage, CmsClientMessageType};
use px_base::md5_hex;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tokio::sync::Mutex;

pub type CmsClientConnPtr = Arc<Mutex<CmsClientConn>>;

#[derive(Clone)]
pub struct CmsClientConn {
    pub context: Arc<Mutex<CmsContext>>,
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
pub struct CmsClientConnVo {
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

impl CmsClientConn {
    //
    pub async fn new(
        context: Arc<Mutex<CmsContext>>,
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

    pub fn as_vo(&self) -> CmsClientConnVo {
        CmsClientConnVo {
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

    pub async fn process_message(&mut self, _who: String, data: Bytes) -> bool {
        let m = CmsClientMessage::decode(data);
        if let Err(e) = m {
            tracing::error!("cms client parse error: {:?}", e);
            return false;
        }
        let m = m.unwrap();
        let _device_id = m.device_id;
        let msg_type = m.msg_type;
        if msg_type == CmsClientMessageType::KCmsClientHello {
            let _m_hello = m.hello.unwrap();
            // update time
            self.hello_timestamp = px_base::get_current_timestamp();
            self.connection_alive = true;

            // insert to db
            gCmsClientConnMgr.insert_conn(self.as_vo()).await;
        } else if msg_type == CmsClientMessageType::KCmsClientHeartBeat {
            let m_heartbeat = m.heartbeat.unwrap();
            self.hb_index = m_heartbeat.hb_index;
            self.last_update_timestamp = px_base::get_current_timestamp();
            self.connection_alive = m_heartbeat.connection_alive;

            // update database
            if self.hb_index % 5 == 0 {
                gCmsClientConnMgr
                    .update_conn(self.conn_id.clone(), self.last_update_timestamp)
                    .await;
            }
        }
        true
    }
}
