use px_base::sys_info::SysInfo;
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize)]
#[allow(dead_code)]
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

#[derive(Debug, Deserialize, Serialize, PartialEq, Eq)]
pub struct DeviceOnlineStateChanged {
    pub msg_type: String,
    pub device_id: String,
    pub online: bool,
}

impl DeviceOnlineStateChanged {
    pub fn new(device_id: String, online: bool) -> Self {
        Self {
            msg_type: "device_online_state_changed".to_string(),
            device_id,
            online,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::DeviceOnlineStateChanged;

    #[test]
    fn device_online_event_has_stable_websocket_shape() {
        let event = DeviceOnlineStateChanged::new("device-1".to_string(), true);
        assert_eq!(
            serde_json::to_value(event).unwrap(),
            serde_json::json!({
                "msg_type": "device_online_state_changed",
                "device_id": "device-1",
                "online": true
            })
        );
    }
}
