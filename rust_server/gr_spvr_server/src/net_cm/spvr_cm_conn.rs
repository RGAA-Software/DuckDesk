use std::sync::Arc;
use axum::body::Bytes;
use axum::extract::ws::{Message, Utf8Bytes, WebSocket};
use futures_util::SinkExt;
use futures_util::stream::SplitSink;
use serde::{Deserialize, Serialize};
use tokio::sync::Mutex;
use crate::gSpvrPanelConnMgr;
use crate::net_cm::spvr_cm_message::{CmMessage, StreamHardwareInfoResp, StreamHardwarePieceResp};
use crate::spvr_context::SpvrContext;

#[derive(Clone)]
pub struct SpvrCmConn {
    pub context: Arc<Mutex<SpvrContext>>,
    pub sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
    pub appkey: String,
    pub hello_timestamp: i64,
    pub last_update_timestamp: i64,
    pub hb_index: i64,
    pub hardware_streamer_device_id: String,
    pub running_stat_streamer_device_id: String,
}

impl SpvrCmConn {
    pub async fn new(context: Arc<Mutex<SpvrContext>>,
                     sender: Arc<Mutex<SplitSink<WebSocket, Message>>>,
                     appkey: String,
    ) -> SpvrCmConn {
        Self {
            context,
            sender,
            appkey,
            hello_timestamp: gr_base::get_current_timestamp(),
            last_update_timestamp: gr_base::get_current_timestamp(),
            hb_index: 0,
            hardware_streamer_device_id: "".to_string(),
            running_stat_streamer_device_id: "".to_string(),
        }
    }

    pub async fn start_hardware_info_streamer(me: Arc<Mutex<Self>>) {
        tokio::spawn(async move {
            loop {
                let fn_delay_1s = async {
                    tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
                };

                let target_device_id = me.lock().await.hardware_streamer_device_id.clone();
                if target_device_id.is_empty() {
                    fn_delay_1s.await;
                    continue;
                }

                let r = gSpvrPanelConnMgr
                    .get_conn(target_device_id.clone()).await;
                if let Err(err) = r {
                    //tracing::error!("error getting conn: {}", err);
                    fn_delay_1s.await;
                    continue;
                }
                else {
                    let conn = r.unwrap();
                    if !conn.lock().await.sys_info_array.is_empty() {
                        let sys_info = conn.lock().await
                            .sys_info_array
                            .last().unwrap()
                            .clone();
                        let resp = StreamHardwarePieceResp {
                            msg_type: "stream_hardware_piece_resp".to_string(),
                            device_id: target_device_id.clone(),
                            sys_info
                        };
                        if let Ok(str) = serde_json::to_string(&resp) {
                            if !me.lock().await.send_message(str).await {
                                tracing::warn!("exit hardware info back streamer...");
                                break;
                            }
                        }
                        else {
                            tracing::error!("error serializing {:#?} to json", resp);
                        }
                    }
                }

                fn_delay_1s.await;
            }
        });
    }

    pub async fn process_message(&mut self, _who: String, data: String) -> bool {
        match serde_json::from_str(data.as_str()).unwrap_or(CmMessage::Unknown) {
            CmMessage::Ping => {
                tracing::info!("received ping");
            }

            CmMessage::Heartbeat { index } => {
                //tracing::info!("received heartbeat: {}", index);
            }

            CmMessage::StreamHardwareInfo { device_id } => {
                tracing::info!("stream hardware info device_id: {}", device_id);
                let r = gSpvrPanelConnMgr
                    .get_conn(device_id.clone()).await;
                if let Err(err) = r {
                    tracing::error!("error getting conn: {}", err);
                    // back
                }
                else {
                    let conn = r.unwrap();
                    let sys_info_array = conn.lock().await.sys_info_array.clone();
                    let resp = serde_json::to_string(&StreamHardwareInfoResp {
                        msg_type: "stream_hardware_info_resp".to_string(),
                        device_id: device_id.clone(),
                        sys_info_array,
                    });
                    if let Err(e) = resp {
                        tracing::error!("error serializing stream info: {}", e);
                    }
                    else {
                        self.send_message(resp.unwrap()).await;
                    }

                    self.hardware_streamer_device_id = device_id;
                }
            }

            CmMessage::StreamRunningStat { device_id } => {

            }

            CmMessage::Unknown => {
                tracing::warn!("received unknown message: {}", data);
            }
        }
        true
    }

    pub async fn send_message(&self, msg: String) -> bool {
        if let Err(err) = self.sender.lock().await
            .send(Message::Text(Utf8Bytes::from(msg))).await {
            tracing::error!("send message to cm client failed: {}", err);
            return false;
        }
        true
    }
}