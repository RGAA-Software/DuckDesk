use crate::BackupDaemonStatus;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

pub const MAX_CONTROL_MESSAGE_BYTES: usize = 128 * 1024;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
pub enum BackupToConsole {
    Authenticate {
        deployment_id: Uuid,
        token: String,
    },
    Status {
        status: BackupDaemonStatus,
    },
    TriggerResult {
        task_id: Uuid,
        accepted: bool,
        code: Option<String>,
    },
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
pub enum ConsoleToBackup {
    Authenticated,
    Trigger { task_id: Uuid },
}
