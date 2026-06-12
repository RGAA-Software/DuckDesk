use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct SpvrServerConfig {
    pub srv_name: String,
    pub srv_w3c_ip: String,
    pub srv_spvr_port: u16,
    pub srv_udp_broadcast_port: u16,
    pub srv_relay_port: u16,
    pub srv_appkey: String,
}