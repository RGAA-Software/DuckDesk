use crate::device::console_desktop_link::DesktopLinkRaw;
use serde::{Deserialize, Serialize};
use std::net::IpAddr;

fn default_true() -> bool {
    true
}

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct ConsoleDevice {
    // device id
    #[serde(default)]
    pub device_id: String,

    // device name
    #[serde(default)]
    pub device_name: String,

    // bind to which user
    // logged-in user on the device
    #[serde(default)]
    pub logged_in_user_id: String,

    //
    #[serde(default)]
    pub seed: String,

    //
    #[serde(default)]
    pub created_timestamp: i64,

    //
    #[serde(default)]
    pub last_update_timestamp: i64,

    //
    #[serde(default)]
    pub random_pwd_md5: String,

    //
    #[serde(default)]
    pub safety_pwd_md5: String,

    // reset per month
    #[serde(default)]
    pub used_time: i64,

    #[serde(default)]
    pub gen_random_pwd: String,

    // link://xxxxxx
    #[serde(default)]
    pub desktop_link: String,

    // origin json format of [desktop_link]
    #[serde(default)]
    pub desktop_link_raw: String,

    #[serde(default)]
    pub active: bool,

    /// Desktop remote-control policy. Missing legacy fields deliberately
    /// preserve the product defaults instead of deserializing to false.
    #[serde(default = "default_true")]
    pub allow_observer: bool,
    #[serde(default = "default_true")]
    pub allow_takeover: bool,

    // local NIC IPv4 list reported by panel at handshake (design doc 5.2)
    #[serde(default)]
    pub panel_lan_ips: Vec<String>,

    // panel http server port (records api), 0 = unknown (use default 20369)
    #[serde(default)]
    pub panel_http_port: i64,
}

impl ConsoleDevice {
    pub fn get_ip_from_link(&self) -> String {
        match DesktopLinkRaw::from(self.desktop_link_raw.as_str()) {
            Ok(v) => {
                if v.ips.is_empty() {
                    "".to_string()
                } else {
                    v.ips[0].ip.to_string()
                }
            }
            Err(e) => {
                tracing::error!("parse desktop link failed: {}", e);
                "".to_string()
            }
        }
    }

    /// Reachable render HTTP endpoints advertised by the desktop panel.
    /// The safety credential intentionally stays in Console and is never exposed
    /// by the wall signaling API.
    pub fn get_render_endpoints(&self) -> Vec<(String, i32)> {
        match DesktopLinkRaw::from(self.desktop_link_raw.as_str()) {
            Ok(v) if v.rdpt > 0 && v.rdpt <= u16::MAX as i32 => v
                .ips
                .into_iter()
                // Accept literal IP addresses only. Besides catching corrupt
                // links this prevents a stored hostname from turning the Console
                // proxy into an unrestricted DNS/HTTP forwarder.
                .filter_map(|item| {
                    item.ip
                        .parse::<IpAddr>()
                        .ok()
                        .map(|ip| (ip.to_string(), v.rdpt))
                })
                .collect(),
            Ok(_) => Vec::new(),
            Err(e) => {
                tracing::warn!("parse render endpoint failed for {}: {}", self.device_id, e);
                Vec::new()
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn device_with_link(ips: &str, port: i32) -> ConsoleDevice {
        ConsoleDevice {
            device_id: "dev-1".to_string(),
            desktop_link_raw: format!(
                r#"{{"did":"dev-1","dn":"desk","iidx":0,"ips":{ips},"ppt":0,"rdpt":{port},"rlak":"","rlpt":0,"rlst":"","rpwd":""}}"#
            ),
            ..Default::default()
        }
    }

    #[test]
    fn render_endpoints_accept_only_literal_ips_and_valid_ports() {
        let device = device_with_link(
            r#"[{"ip":"192.168.1.9"},{"ip":"host.invalid"},{"ip":"::1"}]"#,
            32004,
        );
        assert_eq!(
            device.get_render_endpoints(),
            vec![
                ("192.168.1.9".to_string(), 32004),
                ("::1".to_string(), 32004)
            ]
        );
        assert!(device_with_link(r#"[{"ip":"127.0.0.1"}]"#, 70000)
            .get_render_endpoints()
            .is_empty());
    }
}
