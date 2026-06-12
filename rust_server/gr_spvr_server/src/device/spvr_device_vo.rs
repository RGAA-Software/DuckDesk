use serde::{Deserialize, Serialize};
use gr_base::format_duration_compact;
use gr_base::sys_info::SysInfo;
use crate::device::spvr_device::SpvrDevice;

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct SpvrDeviceVo {

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
    pub used_time: String,

    #[serde(default)]
    pub gen_random_pwd: String,

    // link://xxxxxx
    #[serde(default)]
    pub desktop_link: String,

    // origin json format of [desktop_link]
    #[serde(default)]
    pub desktop_link_raw: String,

    #[serde(default)]
    pub online: bool,

    #[serde(default)]
    pub device_ip_addr: String,

    #[serde(default)]
    pub active: bool,

    #[serde(default)]
    pub sys_info: SysInfo,
}

impl SpvrDeviceVo {
    pub fn from(device: &SpvrDevice) -> Self {
        Self {
            device_id: device.device_id.to_string(),
            device_name: device.device_name.to_string(),
            logged_in_user_id: device.logged_in_user_id.to_string(),
            seed: device.seed.to_string(),
            created_timestamp: device.created_timestamp,
            last_update_timestamp: device.last_update_timestamp,
            random_pwd_md5: device.random_pwd_md5.to_string(),
            safety_pwd_md5: device.safety_pwd_md5.to_string(),
            used_time: format_duration_compact(device.used_time),
            gen_random_pwd: device.gen_random_pwd.to_string(),
            desktop_link: device.desktop_link.to_string(),
            desktop_link_raw: device.desktop_link_raw.to_string(),
            online: false,
            device_ip_addr: device.get_ip_from_link(),
            active: device.active,
            sys_info: Default::default(),
        }
    }
}