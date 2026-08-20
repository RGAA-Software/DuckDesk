#![allow(clippy::field_reassign_with_default, clippy::too_many_arguments)]

use crate::event::cms_event_keys::{
    EVENT_ANALYZE_LOG, EVENT_CPU, EVENT_DISK, EVENT_GPU, EVENT_MEMORY, EVENT_USER_ACTIVE,
    EVENT_USER_DELETE, EVENT_USER_LOGIN, EVENT_USER_LOGOUT, EVENT_USER_REGISTER, EVENT_USER_UPDATE,
    EVENT_USER_UPDATE_PASSWORD,
};
use mongodb::bson::oid::ObjectId;
use px_base::sys_info::SysInfo;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct CmsEvent {
    #[serde(default)]
    pub event_id: String,

    #[serde(default)]
    pub total: u64,

    #[serde(default)]
    pub timestamp: i64,

    #[serde(default)]
    pub readable_timestamp: String,

    #[serde(default)]
    pub event_type: String,

    #[serde(default)]
    pub device_id: String,

    #[serde(default)]
    pub device_ip: String,

    #[serde(default)]
    pub device_name: String,

    #[serde(default)]
    pub message: String,

    #[serde(default)]
    pub current_sys_info: Option<SysInfo>,

    #[serde(default)]
    pub user_id: String,

    #[serde(default)]
    pub user_name: String,

    #[serde(default)]
    pub user_update_values: HashMap<String, String>,

    #[serde(default)]
    pub log_path: String,

    #[serde(default)]
    pub cpu_usage: u32,

    #[serde(default)]
    pub mem_usage: u32,

    #[serde(default)]
    pub disk_usage: u32,
    #[serde(default)]
    pub disk_path: String,

    #[serde(default)]
    pub gpu_usage: u32,
    #[serde(default)]
    pub gpu_id: String,
    #[serde(default)]
    pub gpu_name: String,

    /// Structured security-audit fields. Legacy telemetry events leave these
    /// empty, so old event readers remain compatible.
    #[serde(default)]
    pub actor_type: String,
    #[serde(default)]
    pub actor_id: String,
    #[serde(default)]
    pub action: String,
    #[serde(default)]
    pub result: String,
    #[serde(default)]
    pub target_type: String,
    #[serde(default)]
    pub target_id: String,
    #[serde(default)]
    pub reason: String,
    #[serde(default)]
    pub metadata: HashMap<String, String>,
}

impl CmsEvent {
    #[allow(clippy::too_many_arguments)]
    pub fn new_audit(
        actor_type: &str,
        actor_id: &str,
        action: &str,
        result: &str,
        target_type: &str,
        target_id: &str,
        reason: &str,
        metadata: HashMap<String, String>,
    ) -> Self {
        Self {
            event_id: ObjectId::new().to_hex(),
            timestamp: px_base::get_current_timestamp(),
            readable_timestamp: px_base::get_current_readable_timestamp(),
            event_type: "security_audit".to_string(),
            actor_type: actor_type.to_string(),
            actor_id: actor_id.to_string(),
            action: action.to_string(),
            result: result.to_string(),
            target_type: target_type.to_string(),
            target_id: target_id.to_string(),
            reason: reason.to_string(),
            metadata,
            ..Default::default()
        }
    }

    pub fn new_register(uid: String, username: String) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_USER_REGISTER.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event
    }

    pub fn new_login(uid: String, username: String) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_USER_LOGIN.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event
    }

    pub fn new_logout(uid: String, username: String) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_USER_LOGOUT.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event
    }

    pub fn new_delete(uid: String, username: String) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_USER_DELETE.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event
    }

    pub fn new_active(uid: String, username: String) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_USER_ACTIVE.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event
    }

    pub fn new_update(uid: String, username: String, values: HashMap<String, String>) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_USER_UPDATE.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event.user_update_values = values;
        event
    }

    pub fn new_update_password(
        uid: String,
        username: String,
        values: HashMap<String, String>,
    ) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_USER_UPDATE_PASSWORD.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event.user_update_values = values;
        event
    }

    pub fn new_analyze_log(
        uid: String,
        username: String,
        device_id: String,
        log_path: String,
    ) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_ANALYZE_LOG.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event.device_id = device_id;
        event.log_path = log_path;
        event
    }

    // cpu
    pub fn new_cpu(
        device_id: String,
        device_ip: String,
        device_name: String,
        uid: String,
        username: String,
        cpu_usage: u32,
    ) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_CPU.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event.device_id = device_id;
        event.device_ip = device_ip;
        event.device_name = device_name;
        event.cpu_usage = cpu_usage;
        event
    }

    // memory
    pub fn new_memory(
        device_id: String,
        device_ip: String,
        device_name: String,
        uid: String,
        username: String,
        mem_usage: u32,
    ) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_MEMORY.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event.device_id = device_id;
        event.device_ip = device_ip;
        event.device_name = device_name;
        event.mem_usage = mem_usage;
        event
    }

    // disk
    pub fn new_disk(
        device_id: String,
        device_ip: String,
        device_name: String,
        uid: String,
        username: String,
        disk_usage: u32,
        disk_path: String,
    ) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_DISK.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event.device_id = device_id;
        event.device_ip = device_ip;
        event.device_name = device_name;
        event.disk_usage = disk_usage;
        event.disk_path = disk_path;
        event
    }

    // gpu
    pub fn new_gpu(
        device_id: String,
        device_ip: String,
        device_name: String,
        uid: String,
        username: String,
        gpu_usage: u32,
        gpu_id: String,
        gpu_name: String,
    ) -> Self {
        let mut event = CmsEvent::default();
        event.event_id = ObjectId::new().to_hex();
        event.event_type = EVENT_GPU.to_string();
        event.timestamp = px_base::get_current_timestamp();
        event.readable_timestamp = px_base::get_current_readable_timestamp();
        event.user_id = uid;
        event.user_name = username;
        event.device_id = device_id;
        event.device_ip = device_ip;
        event.device_name = device_name;
        event.gpu_usage = gpu_usage;
        event.gpu_id = gpu_id;
        event.gpu_name = gpu_name;
        event
    }
}
