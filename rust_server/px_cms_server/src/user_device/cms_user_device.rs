use crate::device::cms_device::CmsDevice;
use crate::user::cms_user::CmsUser;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct CmsUserDevice {
    pub uid: String,
    pub device_id: String,
    pub created_ts: i64,
    pub created_ts_readable: String,
}

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct CmsUserDeviceAdapter {
    pub uid: String,
    pub device_id: String,
    pub created_ts: i64,
    pub created_ts_readable: String,
    pub user: CmsUser,
    pub device: CmsDevice,
}
