use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct Authorization {
    pub auth_id: String,
    pub auth_name: String,
    pub machine_code: String,
    pub description: String,
    pub max_streams: i32,
    pub appkey: String,
    pub app_secret: String,
    pub username: String,
    pub password: String,
    pub created_timestamp_ms: i64,
    pub end_timestamp_ms: i64,
    pub last_modify_timestamp: i64,
    pub days: i32,
    pub verify_server: String,
    #[serde(default)]
    pub deploy_str: String,
    #[serde(default)]
    pub role: i32,
    #[serde(default)]
    pub used_time_ms: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct AuthorizationVo {
    #[serde(flatten)]
    pub authorization: Authorization,
    pub total: u64,
}

impl Authorization {
    pub fn as_vo(&self, total: u64) -> AuthorizationVo {
        AuthorizationVo {
            authorization: self.clone(),
            total,
        }
    }
}
