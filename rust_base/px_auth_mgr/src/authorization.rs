use serde::{Deserialize, Serialize};

pub const PRODUCT_CMS: &str = "cms";
pub const PRODUCT_Pixels_CMS: &str = "Pixels_cms";
pub const PRODUCT_GOPICO: &str = "gopico";
pub const PRODUCT_CLIENTBOX: &str = "clientbox";
pub const PRODUCT_GOAGENT: &str = "goagent";

pub const MODE_TRIAL: &str = "trial";
pub const MODE_LICENSED: &str = "licensed";

pub fn default_product_cms() -> String {
    PRODUCT_CMS.to_string()
}

pub fn default_mode() -> String {
    MODE_LICENSED.to_string()
}

/// Products that support device self-registration & license pulling.
pub fn is_device_product(product: &str) -> bool {
    matches!(
        product,
        PRODUCT_GOPICO | PRODUCT_CLIENTBOX | PRODUCT_GOAGENT | PRODUCT_Pixels_CMS
    )
}

/// Default max devices for an auto-registered trial device.
pub fn default_trial_max_devices(product: &str) -> i32 {
    match product {
        PRODUCT_GOPICO => 4,
        _ => 1,
    }
}

/// Days used for trial/pseudo-permanent licenses.
pub const TRIAL_DAYS: i32 = 365000;

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
    /// Product this authorization applies to: "cms" | "Pixels_cms" | "gopico" | "clientbox" | "goagent".
    #[serde(default = "default_product_cms")]
    pub product: String,
    /// Authorization mode: "trial" | "licensed". Existing rows default to "licensed".
    #[serde(default = "default_mode")]
    pub mode: String,
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
            mode: default_mode(),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn device_products_include_Pixels_cms_but_not_legacy_cms() {
        assert!(is_device_product(PRODUCT_GOPICO));
        assert!(is_device_product(PRODUCT_CLIENTBOX));
        assert!(is_device_product(PRODUCT_GOAGENT));
        assert!(is_device_product(PRODUCT_Pixels_CMS));
        // Legacy manual-license product stays a non-device product.
        assert!(!is_device_product(PRODUCT_CMS));
        assert!(!is_device_product("unknown"));
    }

    #[test]
    fn Pixels_cms_trial_defaults_to_one_device() {
        assert_eq!(default_trial_max_devices(PRODUCT_Pixels_CMS), 1);
        assert_eq!(default_trial_max_devices(PRODUCT_GOPICO), 4);
    }
}
