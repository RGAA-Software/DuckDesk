use crate::device::cms_device::CmsDevice;
use crate::user::cms_user::CmsUserView;
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
    /// HTTP-safe user projection. Never serialize the persistence model here:
    /// it contains the Argon2 password verifier.
    pub user: CmsUserView,
    pub device: CmsDevice,
}

#[derive(Serialize, Debug, Deserialize, Clone, Default, PartialEq, Eq)]
pub struct CmsUserDeviceSummary {
    pub device_id: String,
    pub name: String,
    pub online: bool,
    pub capabilities: Vec<String>,
    pub last_seen_at: i64,
}

impl From<CmsDevice> for CmsUserDeviceSummary {
    fn from(device: CmsDevice) -> Self {
        Self {
            device_id: device.device_id,
            name: device.device_name,
            online: device.active,
            capabilities: vec!["view".to_string(), "input".to_string()],
            last_seen_at: device.last_update_timestamp,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn adapter_cannot_serialize_a_user_password_verifier() {
        let adapter = CmsUserDeviceAdapter {
            user: CmsUserView {
                uid: "u1".to_string(),
                username: "alice".to_string(),
                ..Default::default()
            },
            ..Default::default()
        };
        let json = serde_json::to_string(&adapter).unwrap();
        assert!(!json.contains("password_hash"));
    }

    #[test]
    fn device_summary_never_serializes_connection_secrets() {
        let summary = CmsUserDeviceSummary::from(CmsDevice {
            device_id: "dev-1".to_string(),
            device_name: "desk".to_string(),
            desktop_link: "link://secret".to_string(),
            random_pwd_md5: "random-secret".to_string(),
            safety_pwd_md5: "safety-secret".to_string(),
            ..Default::default()
        });
        let json = serde_json::to_string(&summary).unwrap();
        assert!(!json.contains("desktop_link"));
        assert!(!json.contains("random-secret"));
        assert!(!json.contains("safety-secret"));
    }
}
