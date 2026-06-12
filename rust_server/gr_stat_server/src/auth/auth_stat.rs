use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
pub struct StatAuth {
    pub auth_id: String,
    pub auth_name: String,
    pub auth_machine_code: String,
    pub sys_info: String,
    pub created_ts: i64,
    pub updated_ts: i64,
}