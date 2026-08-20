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
}
