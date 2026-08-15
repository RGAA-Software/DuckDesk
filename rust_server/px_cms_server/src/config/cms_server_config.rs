use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct CmsServerConfig {
    pub srv_name: String,
    pub srv_w3c_ip: String,
    pub srv_cms_port: u16,
    pub srv_udp_broadcast_port: u16,
    pub srv_relay_port: u16,
    pub srv_appkey: String,
}
