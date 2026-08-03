use crate::config::spvr_server_config::SpvrServerConfig;
use crate::{gAuthManager, gSpvrSettings};
use gr_base::ip_util::get_clean_ipv4_addresses;
use serde::Deserialize;

pub const DEFAULT_AUTH_SERVER_URL: &str = "https://127.0.0.1:30400";
pub const DEFAULT_AUTH_PULL_INTERVAL_SECS: u64 = 3600;

fn default_auth_server_url() -> String {
    DEFAULT_AUTH_SERVER_URL.to_string()
}

fn default_auth_pull_interval_secs() -> u64 {
    DEFAULT_AUTH_PULL_INTERVAL_SECS
}

/// 接入凭据（与 auth server 的 [app_credential] 段一致）。
/// appkey/app_secret 为空时 pull 请求不携带签名头（灰度期兼容
/// auth server 端 require_app_credential=false）。
#[derive(Debug, Deserialize, Clone, Default)]
pub struct AppCredentialSettings {
    #[serde(default)]
    pub appkey: String,
    #[serde(default)]
    pub app_secret: String,
}

#[derive(Debug, Deserialize, Clone, Default)]
pub struct SpvrSettings {
    pub server_name: String,
    pub server_w3c_ip: String,
    pub spvr_port: u16,
    pub udp_broadcast_port: u16,
    pub relay_port: u16,
    pub mongodb_url: String,
    pub redis_url: String,
    pub ssl_cert: String,
    pub ssl_key: String,

    /// 授权服务器地址（gr_auth_server）。
    #[serde(default = "default_auth_server_url")]
    pub auth_server_url: String,

    /// 授权拉取周期（秒）。
    #[serde(default = "default_auth_pull_interval_secs")]
    pub auth_pull_interval_secs: u64,

    /// 接入凭据（可选，与 auth server [app_credential] 段一致）。
    #[serde(default)]
    pub app_credential: AppCredentialSettings,

    // ./xx/xx.a
    #[serde(skip_deserializing, skip_serializing)]
    pub upload_path: String,

    // c:/xx/xx/xx.a
    #[serde(skip_deserializing, skip_serializing)]
    pub abs_upload_path: String,

    #[serde(skip_deserializing, skip_serializing)]
    pub upload_logs_path: String,

    #[serde(skip_deserializing, skip_serializing)]
    pub abs_upload_logs_path: String,
}

impl SpvrSettings {
    pub fn new() -> Self {
        SpvrSettings::default()
    }

    pub async fn load_settings() {
        let toml_content = std::fs::read_to_string("gr_cms_server_settings.toml")
            .expect("can't read gr_cms_server_settings.toml");
        let mut ns: SpvrSettings = toml::from_str(&toml_content).expect("parse toml failed");
        //tracing::info!("Load Settings:\n{:#?}", ns);
        tracing::info!("the w3c ip: {}", ns.server_w3c_ip);

        if ns.server_w3c_ip.is_empty() {
            tracing::warn!("server w3c_ip is empty, will read the machine info.");

            // 优先用"首选真实 IPv4"(按 公网 > 192.168.1.x > 192.168.0.x > 10.0.0.x
            // 排序),避免 VPN/虚拟网卡排在前面被选错;取不到再退回第一个干净 IP。
            let mut selected_ip = match gr_base::ip_util::get_preferred_real_ipv4() {
                Ok(Some(ip)) => ip.to_string(),
                _ => String::new(),
            };
            if selected_ip.is_empty() {
                let mut ip_array = Vec::new();
                let ips = get_clean_ipv4_addresses();
                if let Ok(ips) = ips {
                    for ip in ips {
                        tracing::info!("===> IP: {}", ip.to_string());
                        ip_array.push(ip.to_string());
                    }
                }
                if !ip_array.is_empty() {
                    selected_ip = ip_array[0].clone();
                }
            }
            if selected_ip.is_empty() {
                return;
            }
            ns.server_w3c_ip = selected_ip;
        }

        let mut settings = gSpvrSettings.lock().await;
        // tracing::info!("Settings:\n{:#?}", ns);
        *settings = ns;
    }

    pub async fn get_server_config(&self) -> SpvrServerConfig {
        let appkey = gAuthManager.lock().await.get_auth().await.appkey;
        SpvrServerConfig {
            srv_name: self.server_name.clone(),
            srv_w3c_ip: self.server_w3c_ip.clone(),
            srv_spvr_port: self.spvr_port,
            srv_udp_broadcast_port: self.udp_broadcast_port,
            srv_relay_port: self.relay_port,
            srv_appkey: appkey,
        }
    }

    pub fn dump(&self) {
        tracing::info!("{:#?}", self);
    }
}
