use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct StatOpenUp {
    pub device_id: String,
    pub sys_info: String,
    pub created_ts: i64,
    pub updated_ts: i64,
}
