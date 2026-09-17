//! Strict shared wire contract between Console and a node Service.
//! This crate contains no database, process or transport implementation.

use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

pub const MAX_CONNECTIONS: usize = 128;
pub const MAX_MESSAGE_BYTES: usize = 64 * 1024;

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum VideoCodec {
    H264,
    H265,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct VideoSpec {
    pub codec: VideoCodec,
    pub bitrate_kbps: u32,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum ApplicationLaunch {
    GameHook {
        executable_relative: String,
        arguments: String,
        video: VideoSpec,
    },
    Webview {
        entry_url: String,
        video: VideoSpec,
    },
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Rdp,
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum NodeCommandAction {
    Start {
        port: u16,
        launch: ApplicationLaunch,
        install_root: Option<String>,
        gpu_key: Option<String>,
    },
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Stop,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
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

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeReport {
    pub sequence: u64,
    pub product_version_code: u32,
    pub public_host: String,
    pub desktop_port: u16,
    pub application_port_start: u16,
    pub application_port_end: u16,
    pub game_hook: bool,
    pub webview: bool,
    pub rdp: bool,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ObservedRuntimePhase {
    Starting,
    Running,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ObservedRuntime {
    pub instance_id: Uuid,
    pub launch_id: Uuid,
    pub port: u16,
    pub phase: ObservedRuntimePhase,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RuntimeInventory {
    pub challenge_id: Uuid,
    pub runtimes: Vec<ObservedRuntime>,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(tag = "result", rename_all = "snake_case", deny_unknown_fields)]
pub enum CommandOutcome {
    Running {
        port: u16,
    },
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Absent,
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Unknown,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CommandReceipt {
    pub command_id: Uuid,
    pub lease_id: Uuid,
    pub instance_id: Uuid,
    pub launch_id: Uuid,
    pub instance_revision: i64,
    pub outcome: CommandOutcome,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PreparationFailure {
    MissingFiles,
    UnsupportedMode,
    BindingUnverified,
    InvalidConfiguration,
    DependencyUnavailable,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(tag = "state", rename_all = "snake_case", deny_unknown_fields)]
pub enum PreparationState {
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Pending,
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Ready,
    Failed {
        reason: PreparationFailure,
    },
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeploymentObservation {
    pub deployment_revision: i64,
    pub application_revision: i64,
    pub endpoint_revision: i64,
    pub sequence: u64,
    pub status: PreparationState,
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum DeploymentPreparation {
    GameHook {
        install_root: String,
        executable_relative: String,
        gpu_key: Option<String>,
    },
    Webview {
        gpu_key: Option<String>,
    },
    Rdp {
        gpu_key: Option<String>,
    },
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeploymentAssignment {
    pub id: Uuid,
    pub application_id: Uuid,
    pub deployment_revision: i64,
    pub application_revision: i64,
    pub disabled: bool,
    pub preparation: DeploymentPreparation,
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
pub enum NodeRequest {
    Authenticate {
        request_id: u64,
        node_token: String,
    },
    Report {
        request_id: u64,
        report: NodeReport,
    },
    BeginReconciliation {
        request_id: u64,
    },
    Reconcile {
        request_id: u64,
        inventory: RuntimeInventory,
    },
    PollCommand {
        request_id: u64,
    },
    AcknowledgeCommand {
        request_id: u64,
        receipt: CommandReceipt,
    },
    ReportDeployment {
        request_id: u64,
        deployment_id: Uuid,
        observation: DeploymentObservation,
    },
    ListDeployments {
        request_id: u64,
        after: Option<Uuid>,
        limit: u16,
    },
}

impl NodeRequest {
    pub fn request_id(&self) -> u64 {
        match self {
            Self::Authenticate { request_id, .. }
            | Self::Report { request_id, .. }
            | Self::BeginReconciliation { request_id }
            | Self::Reconcile { request_id, .. }
            | Self::PollCommand { request_id }
            | Self::AcknowledgeCommand { request_id, .. }
            | Self::ReportDeployment { request_id, .. }
            | Self::ListDeployments { request_id, .. } => *request_id,
        }
    }
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ExpectedLaunch {
    pub instance_id: Uuid,
    pub launch_id: Uuid,
    pub reject_through_revision: i64,
    pub port: u16,
    pub desired_state: String,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReconciliationChallenge {
    pub id: Uuid,
    pub node_generation: i64,
    pub control_epoch: i64,
    pub deadline: DateTime<Utc>,
    pub launches: Vec<ExpectedLaunch>,
}

#[derive(Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case", deny_unknown_fields)]
pub enum NodeResponse {
    Authenticated {
        request_id: u64,
        node_id: Uuid,
        generation: i64,
        control_epoch: i64,
    },
    Reported {
        request_id: u64,
        state: String,
        endpoint_revision: i64,
    },
    ReconciliationStarted {
        request_id: u64,
        challenge: ReconciliationChallenge,
    },
    Reconciled {
        request_id: u64,
    },
    Command {
        request_id: u64,
        command: Option<Box<NodeCommand>>,
    },
    CommandAcknowledged {
        request_id: u64,
        state: String,
        revision: i64,
    },
    DeploymentReported {
        request_id: u64,
    },
    Deployments {
        request_id: u64,
        deployments: Vec<DeploymentAssignment>,
    },
    Error {
        request_id: Option<u64>,
        code: String,
    },
}

impl NodeResponse {
    pub fn request_id(&self) -> Option<u64> {
        match self {
            Self::Authenticated { request_id, .. }
            | Self::Reported { request_id, .. }
            | Self::ReconciliationStarted { request_id, .. }
            | Self::Reconciled { request_id }
            | Self::Command { request_id, .. }
            | Self::CommandAcknowledged { request_id, .. }
            | Self::DeploymentReported { request_id }
            | Self::Deployments { request_id, .. } => Some(*request_id),
            Self::Error { request_id, .. } => *request_id,
        }
    }
}

mod strict_empty {
    use serde::{de::MapAccess, Deserialize, Deserializer};

    pub fn deserialize<'de, D>(deserializer: D) -> Result<(), D::Error>
    where
        D: Deserializer<'de>,
    {
        #[derive(Deserialize)]
        #[serde(deny_unknown_fields)]
        struct Empty {}

        struct Visitor;
        impl<'de> serde::de::Visitor<'de> for Visitor {
            type Value = ();

            fn expecting(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                formatter.write_str("no variant fields")
            }

            fn visit_map<A>(self, map: A) -> Result<Self::Value, A::Error>
            where
                A: MapAccess<'de>,
            {
                Empty::deserialize(serde::de::value::MapAccessDeserializer::new(map))?;
                Ok(())
            }
        }
        deserializer.deserialize_map(Visitor)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn node_wire_is_tagged_strict_and_keeps_credentials_out_of_responses() {
        let request: NodeRequest =
            serde_json::from_str(r#"{"type":"authenticate","request_id":1,"node_token":"secret"}"#)
                .unwrap();
        assert_eq!(request.request_id(), 1);
        for invalid in [
            r#"{"type":"authenticate","request_id":1,"node_token":"secret","extra":1}"#,
            r#"{"type":"poll_command","request_id":1,"extra":1}"#,
            r#"{"request_id":1}"#,
            r#"{"type":"unknown","request_id":1}"#,
            r#"{"type":"reconcile","request_id":1,"inventory":{"challenge_id":"00000000-0000-0000-0000-000000000000","runtimes":[{"instance_id":"00000000-0000-0000-0000-000000000000","launch_id":"00000000-0000-0000-0000-000000000000","port":1,"phase":"running","extra":1}]}}"#,
            r#"{"type":"acknowledge_command","request_id":1,"receipt":{"command_id":"00000000-0000-0000-0000-000000000000","lease_id":"00000000-0000-0000-0000-000000000000","instance_id":"00000000-0000-0000-0000-000000000000","launch_id":"00000000-0000-0000-0000-000000000000","instance_revision":1,"outcome":{"result":"absent","extra":1}}}"#,
        ] {
            assert!(serde_json::from_str::<NodeRequest>(invalid).is_err());
        }
        let response = serde_json::to_string(&NodeResponse::Authenticated {
            request_id: 1,
            node_id: Uuid::nil(),
            generation: 2,
            control_epoch: 3,
        })
        .unwrap();
        assert!(!response.contains("token"));
        assert!(!response.contains("secret"));
    }
}
