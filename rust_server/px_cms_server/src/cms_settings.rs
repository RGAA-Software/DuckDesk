use crate::config::cms_server_config::CmsServerConfig;
use crate::{gAuthManager, gCmsSettings};
use px_base::ip_util::get_clean_ipv4_addresses;
use serde::Deserialize;

pub const DEFAULT_AUTH_SERVER_URL: &str = "https://127.0.0.1:30400";
pub const DEFAULT_AUTH_PULL_INTERVAL_SECS: u64 = 3600;

fn default_auth_server_url() -> String {
    DEFAULT_AUTH_SERVER_URL.to_string()
}

fn default_auth_pull_interval_secs() -> u64 {
    DEFAULT_AUTH_PULL_INTERVAL_SECS
}

fn default_force_authorize() -> bool {
    true
}

fn default_ssl_enable() -> bool {
    true
}

fn default_media_server_url() -> String {
    "http://127.0.0.1:12888".to_string()
}

fn default_live_app() -> String {
    "live".to_string()
}

fn default_live_app_id() -> String {
    "cargame_debug".to_string()
}

fn default_auto_start_media_server() -> bool {
    true
}

fn default_publish_rtmp_url() -> String {
    "rtmp://127.0.0.1:1935/live/{live_stream_id}".to_string()
}

/// ZLMediaKit integration settings. `api_secret` is deliberately server-only:
/// the CMS issues short-lived playback tickets instead of returning this value
/// or a direct media-server URL to the browser.
#[derive(Debug, Deserialize, Clone)]
#[serde(default)]
pub struct CmsLiveSettings {
    pub media_server_url: String,
    pub api_secret: String,
    pub app: String,
    pub default_app_id: String,
    /// Address used by render instances to publish the passive main stream.
    /// `{live_stream_id}` is replaced by render before publishing.
    #[serde(default = "default_publish_rtmp_url")]
    pub publish_rtmp_url: String,
    /// Start the fixed px_media.exe sidecar when media_server_url is local.
    #[serde(default = "default_auto_start_media_server")]
    pub auto_start_media_server: bool,
}

impl Default for CmsLiveSettings {
    fn default() -> Self {
        Self {
            media_server_url: default_media_server_url(),
            api_secret: String::new(),
            app: default_live_app(),
            default_app_id: default_live_app_id(),
            publish_rtmp_url: default_publish_rtmp_url(),
            auto_start_media_server: default_auto_start_media_server(),
        }
    }
}

/// 鉴权是否放行（force_authorize=false 时所有 WS/HTTP 鉴权过滤直接通过）。
pub async fn is_auth_bypassed() -> bool {
    !gCmsSettings.lock().await.force_authorize
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

#[derive(Debug, Deserialize, Clone)]
#[serde(default)]
pub struct UserRateLimitSettings {
    pub login_per_ip_per_minute: usize,
    pub login_per_account_per_15_minutes: usize,
    pub guest_session_per_ip_per_hour: usize,
    pub start_per_subject_per_minute: usize,
}

impl Default for UserRateLimitSettings {
    fn default() -> Self {
        Self {
            login_per_ip_per_minute: 10,
            login_per_account_per_15_minutes: 20,
            guest_session_per_ip_per_hour: 30,
            start_per_subject_per_minute: 6,
        }
    }
}

#[derive(Debug, Deserialize, Clone)]
#[serde(default)]
pub struct UserQuotaSettings {
    pub guest_concurrent_instances: usize,
    pub user_concurrent_instances: usize,
    pub guest_daily_minutes: usize,
    pub public_app_global_concurrency: usize,
}

impl Default for UserQuotaSettings {
    fn default() -> Self {
        Self {
            guest_concurrent_instances: 1,
            user_concurrent_instances: 3,
            guest_daily_minutes: 60,
            public_app_global_concurrency: 20,
        }
    }
}

#[derive(Debug, Deserialize, Clone)]
#[serde(default)]
pub struct CmsUserSettings {
    pub panel_sliding_days: i64,
    pub panel_absolute_days: i64,
    pub web_sliding_hours: i64,
    pub web_absolute_days: i64,
    pub admin_sliding_hours: i64,
    pub admin_absolute_hours: i64,
    pub guest_absolute_hours: i64,
    pub ticket_expire_seconds: i64,
    pub rate_limit: UserRateLimitSettings,
    pub quota: UserQuotaSettings,
}

impl Default for CmsUserSettings {
    fn default() -> Self {
        Self {
            panel_sliding_days: 30,
            panel_absolute_days: 90,
            web_sliding_hours: 12,
            web_absolute_days: 30,
            admin_sliding_hours: 2,
            admin_absolute_hours: 8,
            guest_absolute_hours: 24,
            ticket_expire_seconds: 30,
            rate_limit: UserRateLimitSettings::default(),
            quota: UserQuotaSettings::default(),
        }
    }
}

#[derive(Debug, Deserialize, Clone, Default)]
pub struct CmsSettings {
    /// Deployment guard used by destructive maintenance commands.
    #[serde(default = "default_environment")]
    pub environment: String,
    pub server_name: String,
    pub server_w3c_ip: String,
    pub cms_port: u16,
    pub udp_broadcast_port: u16,
    pub relay_port: u16,
    pub mongodb_url: String,
    pub redis_url: String,
    pub ssl_cert: String,
    pub ssl_key: String,

    /// Server-only salt for IP/User-Agent correlation hashes. Production must
    /// provide an installation-specific value and must never expose it to Web.
    #[serde(default)]
    pub privacy_hash_salt: String,

    /// 授权服务器地址（px_auth_server）。
    #[serde(default = "default_auth_server_url")]
    pub auth_server_url: String,

    /// 授权拉取周期（秒）。
    #[serde(default = "default_auth_pull_interval_secs")]
    pub auth_pull_interval_secs: u64,

    /// 接入凭据（可选，与 auth server [app_credential] 段一致）。
    #[serde(default)]
    pub app_credential: AppCredentialSettings,

    /// true = 强制鉴权（WS token 过滤 + HTTP appkey 过滤）；
    /// false = 鉴权一律放行（本机/测试用）。缺省 true，部署测试环境时在
    /// px_cms.toml 显式写 force_authorize = false。
    #[serde(default = "default_force_authorize")]
    pub force_authorize: bool,

    /// true = 主服务走 HTTPS（rustls，需要 ssl_cert/ssl_key，缺证书拒绝启动）；
    /// false = 主服务绑纯 HTTP（局域网部署，避免页面内嵌 http:// 设备内容
    /// 被浏览器混合内容拦截）。缺省 true。
    #[serde(default = "default_ssl_enable")]
    pub ssl_enable: bool,

    /// ZLMediaKit discovery and CMS-proxied HLS playback configuration.
    #[serde(default)]
    pub live: CmsLiveSettings,

    /// End-user identity, throttling and quota policy.
    #[serde(default)]
    pub user: CmsUserSettings,

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

fn default_environment() -> String {
    "production".to_string()
}

impl CmsSettings {
    pub fn new() -> Self {
        CmsSettings::default()
    }

    /// Reject insecure production combinations before any listener or managed
    /// sidecar is started. Test deployments remain explicit through
    /// `environment = "test"`; production never silently downgrades auth/TLS.
    pub fn validate_for_server(&self) -> Result<(), String> {
        if !matches!(self.environment.as_str(), "test" | "production") {
            return Err("environment must be either 'test' or 'production'".to_string());
        }
        if self.environment == "production" {
            if !self.force_authorize {
                return Err("production requires force_authorize=true".to_string());
            }
            if !self.ssl_enable {
                return Err("production external login requires ssl_enable=true".to_string());
            }
            if self.ssl_cert.trim().is_empty() || self.ssl_key.trim().is_empty() {
                return Err("production TLS certificate and key must be configured".to_string());
            }
            if self.privacy_hash_salt.as_bytes().len() < 16 {
                return Err(
                    "production privacy_hash_salt must contain at least 16 bytes".to_string(),
                );
            }
        }
        if !(5..=300).contains(&self.user.ticket_expire_seconds) {
            return Err("user.ticket_expire_seconds must be between 5 and 300".to_string());
        }
        Ok(())
    }

    pub async fn load_settings() {
        let toml_content = std::fs::read_to_string("px_cms.toml").expect("can't read px_cms.toml");
        let mut ns: CmsSettings = toml::from_str(&toml_content).expect("parse toml failed");
        //tracing::info!("Load Settings:\n{:#?}", ns);
        tracing::info!("the w3c ip: {}", ns.server_w3c_ip);

        if ns.server_w3c_ip.is_empty() {
            tracing::warn!("server w3c_ip is empty, will read the machine info.");

            // 优先用"首选真实 IPv4"(按 公网 > 192.168.1.x > 192.168.0.x > 10.0.0.x
            // 排序),避免 VPN/虚拟网卡排在前面被选错;取不到再退回第一个干净 IP。
            let mut selected_ip = match px_base::ip_util::get_preferred_real_ipv4() {
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

        let mut settings = gCmsSettings.lock().await;
        // tracing::info!("Settings:\n{:#?}", ns);
        *settings = ns;
    }

    pub async fn get_server_config(&self) -> CmsServerConfig {
        let appkey = gAuthManager.lock().await.get_auth().await.appkey;
        CmsServerConfig {
            srv_name: self.server_name.clone(),
            srv_w3c_ip: self.server_w3c_ip.clone(),
            srv_cms_port: self.cms_port,
            srv_udp_broadcast_port: self.udp_broadcast_port,
            srv_relay_port: self.relay_port,
            srv_appkey: appkey,
            srv_ssl_enable: self.ssl_enable,
        }
    }

    pub fn dump(&self) {
        tracing::info!(
            environment = %self.environment,
            server_name = %self.server_name,
            server_w3c_ip = %self.server_w3c_ip,
            cms_port = self.cms_port,
            udp_broadcast_port = self.udp_broadcast_port,
            relay_port = self.relay_port,
            ssl_enable = self.ssl_enable,
            force_authorize = self.force_authorize,
            media_server_url = %self.live.media_server_url,
            "CMS settings loaded (secrets redacted)"
        );
    }
}

#[cfg(test)]
mod security_tests {
    use super::CmsSettings;

    fn base() -> CmsSettings {
        let mut settings = CmsSettings::default();
        settings.environment = "production".to_string();
        settings.force_authorize = true;
        settings.ssl_enable = true;
        settings.ssl_cert = "cert.pem".to_string();
        settings.ssl_key = "key.pem".to_string();
        settings.privacy_hash_salt = "installation-specific-salt".to_string();
        settings.user.ticket_expire_seconds = 30;
        settings
    }

    #[test]
    fn production_rejects_auth_or_tls_downgrade() {
        let mut settings = base();
        assert!(settings.validate_for_server().is_ok());
        settings.force_authorize = false;
        assert!(settings.validate_for_server().is_err());
        settings.force_authorize = true;
        settings.ssl_enable = false;
        assert!(settings.validate_for_server().is_err());
        settings.ssl_enable = true;
        settings.privacy_hash_salt.clear();
        assert!(settings.validate_for_server().is_err());
    }

    #[test]
    fn only_explicit_environment_names_and_bounded_ticket_ttl_are_accepted() {
        let mut settings = base();
        settings.environment = "prod".to_string();
        assert!(settings.validate_for_server().is_err());
        settings.environment = "test".to_string();
        settings.force_authorize = false;
        settings.ssl_enable = false;
        assert!(settings.validate_for_server().is_ok());
        settings.user.ticket_expire_seconds = 301;
        assert!(settings.validate_for_server().is_err());
    }
}
