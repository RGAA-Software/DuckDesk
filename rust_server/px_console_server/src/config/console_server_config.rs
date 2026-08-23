use serde::{Deserialize, Serialize};

fn default_true() -> bool {
    true
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct ConsoleServerConfig {
    pub srv_name: String,
    pub srv_w3c_ip: String,
    #[serde(alias = "srv_cms_port")]
    pub srv_console_port: u16,
    pub srv_udp_broadcast_port: u16,
    pub srv_relay_port: u16,
    pub srv_appkey: String,
    // whether the Console serves HTTPS (true) or plain HTTP; defaults to true
    // so access info produced by older versions is treated as HTTPS.
    #[serde(default = "default_true")]
    pub srv_ssl_enable: bool,
}

/// Wire-only mirror emitted for panels that predate the Console rename.
#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct LegacyCmsServerConfig {
    pub srv_name: String,
    pub srv_w3c_ip: String,
    pub srv_cms_port: u16,
    pub srv_udp_broadcast_port: u16,
    pub srv_relay_port: u16,
    pub srv_appkey: String,
    #[serde(default = "default_true")]
    pub srv_ssl_enable: bool,
}

impl From<&ConsoleServerConfig> for LegacyCmsServerConfig {
    fn from(config: &ConsoleServerConfig) -> Self {
        Self {
            srv_name: config.srv_name.clone(),
            srv_w3c_ip: config.srv_w3c_ip.clone(),
            srv_cms_port: config.srv_console_port,
            srv_udp_broadcast_port: config.srv_udp_broadcast_port,
            srv_relay_port: config.srv_relay_port,
            srv_appkey: config.srv_appkey.clone(),
            srv_ssl_enable: config.srv_ssl_enable,
        }
    }
}
