use serde::{Deserialize, Serialize};

fn default_true() -> bool {
    true
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct CmsServerConfig {
    pub srv_name: String,
    pub srv_w3c_ip: String,
    pub srv_cms_port: u16,
    pub srv_udp_broadcast_port: u16,
    pub srv_relay_port: u16,
    pub srv_appkey: String,
    // whether the CMS serves HTTPS (true) or plain HTTP; defaults to true
    // so access info produced by older versions is treated as HTTPS.
    #[serde(default = "default_true")]
    pub srv_ssl_enable: bool,
}
