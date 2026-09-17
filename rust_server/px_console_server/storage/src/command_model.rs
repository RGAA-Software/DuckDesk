use crate::{ApplicationLaunch, StoreError, VideoCodec, VideoSpec};
use chrono::{DateTime, Utc};
use uuid::Uuid;

#[derive(Debug, Clone, serde::Serialize)]
#[serde(tag = "kind", rename_all = "snake_case")]
pub enum NodeCommandAction {
    Start {
        port: u16,
        launch: ApplicationLaunch,
        install_root: Option<String>,
        gpu_key: Option<String>,
    },
    /// Stop only this launch identity. No PID/path/port cleanup selector.
    Stop,
}
#[derive(Debug, Clone, serde::Serialize)]
pub struct NodeCommand {
    pub id: Uuid,
    pub instance_id: Uuid,
    pub launch_id: Uuid,
    pub application_id: Uuid,
    pub deployment_id: Uuid,
    pub application_revision: i64,
    pub deployment_revision: i64,
    pub instance_revision: i64,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub endpoint_revision: i64,
    pub lease_id: Uuid,
    pub lease_until: DateTime<Utc>,
    pub deadline: DateTime<Utc>,
    pub action: NodeCommandAction,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(tag = "result", rename_all = "snake_case", deny_unknown_fields)]
pub enum CommandOutcome {
    Running {
        port: u16,
    },
    /// Node proves this exact launch is absent (never ran, or exact stop completed).
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Absent,
    /// No proof of absence; never releases occupancy.
    #[serde(deserialize_with = "crate::strict_wire::empty")]
    Unknown,
}
impl CommandOutcome {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::Running { .. } => "running",
            Self::Absent => "absent",
            Self::Unknown => "unknown",
        }
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CommandReceipt {
    pub command_id: Uuid,
    pub lease_id: Uuid,
    pub instance_id: Uuid,
    pub launch_id: Uuid,
    pub instance_revision: i64,
    pub outcome: CommandOutcome,
}
#[derive(sqlx::FromRow)]
pub(crate) struct CommandRow {
    pub id: Uuid,
    pub instance_id: Uuid,
    pub node_id: Uuid,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub instance_revision: i64,
    pub kind: String,
    pub state: String,
    pub attempts: i32,
    pub lease_id: Option<Uuid>,
    pub lease_until: Option<DateTime<Utc>>,
    pub deadline: DateTime<Utc>,
    pub outcome: Option<String>,
    pub completed_lease_id: Option<Uuid>,
    pub expired: bool,
}
#[derive(sqlx::FromRow)]
pub(crate) struct LaunchRow {
    pub kind: String,
    pub install_root: Option<String>,
    pub executable_relative: Option<String>,
    pub arguments: Option<String>,
    pub entry_url: Option<String>,
    pub codec: Option<String>,
    pub bitrate_kbps: Option<i32>,
    pub gpu_key: Option<String>,
}
impl LaunchRow {
    pub(crate) fn action(self, port: i32) -> Result<NodeCommandAction, StoreError> {
        let invalid = StoreError::Database(px_pg::DatabaseError::Operation);
        let video = if self.kind == "rdp" {
            None
        } else {
            Some(VideoSpec {
                codec: match self.codec.as_deref() {
                    Some("h264") => VideoCodec::H264,
                    Some("h265") => VideoCodec::H265,
                    _ => return Err(invalid),
                },
                bitrate_kbps: self
                    .bitrate_kbps
                    .ok_or(invalid)?
                    .try_into()
                    .map_err(|_| invalid)?,
            })
        };
        let launch = match self.kind.as_str() {
            "game_hook" => ApplicationLaunch::GameHook {
                executable_relative: self.executable_relative.ok_or(invalid)?,
                arguments: self.arguments.ok_or(invalid)?,
                video: video.ok_or(invalid)?,
            },
            "webview" => ApplicationLaunch::Webview {
                entry_url: self.entry_url.ok_or(invalid)?,
                video: video.ok_or(invalid)?,
            },
            "rdp" => ApplicationLaunch::Rdp,
            _ => return Err(invalid),
        };
        launch.validate()?;
        if matches!(launch, ApplicationLaunch::GameHook { .. }) {
            crate::deployment_model::absolute_install_root(
                self.install_root.as_deref().ok_or(invalid)?,
            )?;
        } else if self.install_root.is_some() {
            return Err(invalid);
        }
        Ok(NodeCommandAction::Start {
            port: port.try_into().map_err(|_| invalid)?,
            launch,
            install_root: self.install_root,
            gpu_key: self.gpu_key,
        })
    }
}
