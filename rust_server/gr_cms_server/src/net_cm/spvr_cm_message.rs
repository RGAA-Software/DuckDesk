use gr_base::sys_info::SysInfo;
use serde::{Deserialize, Serialize};

const CM_HEARTBEAT: &str = "heartbeat";

#[derive(Debug, Deserialize)]
#[serde(tag = "msg_type")]
pub enum CmMessage {
    #[serde(rename = "ping")]
    Ping,

    #[serde(rename = "heartbeat")]
    Heartbeat { index: u32 },

    #[serde(rename = "stream_hardware_info")]
    StreamHardwareInfo { device_id: String },

    #[serde(rename = "stream_running_stat")]
    StreamRunningStat { device_id: String },

    #[serde(other)]
    Unknown,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct StreamHardwareInfoResp {
    pub msg_type: String,
    pub device_id: String,
    pub sys_info_array: Vec<SysInfo>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct StreamHardwarePieceResp {
    pub msg_type: String,
    pub device_id: String,
    pub sys_info: SysInfo,
}
