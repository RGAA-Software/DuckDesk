use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct UserGroup {
    pub gid: String,
    pub name: String,
    pub name_normalized: String,
    pub remark: String,
    pub deleted: bool,
    pub created_at: i64,
    pub updated_at: i64,
    pub version: i64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct UserGroupMember {
    pub uid: String,
    pub gid: String,
    pub created_at: i64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct GroupDeviceGrant {
    pub gid: String,
    pub device_id: String,
    pub created_at: i64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct GroupAppGrant {
    pub gid: String,
    pub app_id: String,
    pub created_at: i64,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize, PartialEq, Eq)]
pub struct GroupRef {
    pub gid: String,
    pub name: String,
}

#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct GroupView {
    pub gid: String,
    pub name: String,
    pub remark: String,
    pub member_count: u64,
    pub device_count: u64,
    pub app_count: u64,
    pub created_at: i64,
    pub updated_at: i64,
    pub version: i64,
}
