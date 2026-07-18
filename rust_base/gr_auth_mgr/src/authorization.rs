use serde::{Deserialize, Serialize};

pub const PRODUCT_CMS: &str = "cms";
pub const PRODUCT_GOPICO: &str = "gopico";

pub fn default_product_cms() -> String {
    PRODUCT_CMS.to_string()
}

#[derive(Debug, Clone, Serialize, Deserialize)]
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
    /// Product this authorization applies to: "cms" | "gopico".
    #[serde(default = "default_product_cms")]
    pub product: String,
    /// Soft-revoke flag (DB only, not part of signed license payload).
    #[serde(default)]
    pub revoked: bool,
    #[serde(default)]
    pub revoked_at_ms: i64,
    /// Latest status report sent by the licensed client (DB only, never signed).
    #[serde(default)]
    pub client_version: String,
    #[serde(default)]
    pub client_status: String,
    #[serde(default)]
    pub client_os: String,
    #[serde(default)]
    pub client_device_count: i32,
    #[serde(default)]
    pub client_reported_at_ms: i64,
}

impl Default for Authorization {
    fn default() -> Self {
        Self {
            auth_id: String::new(),
            auth_name: String::new(),
            machine_code: String::new(),
            description: String::new(),
            max_streams: 0,
            appkey: String::new(),
            app_secret: String::new(),
            username: String::new(),
            password: String::new(),
            created_timestamp_ms: 0,
            end_timestamp_ms: 0,
            last_modify_timestamp: 0,
            days: 0,
            verify_server: String::new(),
            deploy_str: String::new(),
            role: 0,
            used_time_ms: 0,
            product: default_product_cms(),
            revoked: false,
            revoked_at_ms: 0,
            client_version: String::new(),
            client_status: String::new(),
            client_os: String::new(),
            client_device_count: 0,
            client_reported_at_ms: 0,
        }
    }
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
