use serde::{Deserialize, Serialize};
use crate::device::spvr_device::SpvrDevice;
use crate::user::spvr_user::SpvrUser;

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct SpvrUserDevice {
    pub uid: String,
    pub device_id: String,
    pub created_ts: i64,
    pub created_ts_readable: String,
}

#[derive(Serialize, Debug, Deserialize, Clone, Default)]
pub struct SpvrUserDeviceAdapter {
    pub uid: String,
    pub device_id: String,
    pub created_ts: i64,
    pub created_ts_readable: String,
    pub user: SpvrUser,
    pub device: SpvrDevice,
}