use crate::device::cms_desktop_link::DesktopLinkRaw;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct CmsDevice {
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
}

impl CmsDevice {
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
}
