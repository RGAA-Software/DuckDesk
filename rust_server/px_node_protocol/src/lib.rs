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
        gpu_reservation: Option<GpuReservation>,
        relay: Option<RelayEndpoint>,
    },
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Stop,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RelayEndpoint {
    pub host: String,
    pub port: u16,
    pub app_key: String,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct GpuReservation {
    pub stable_key: String,
    pub inventory_revision: i64,
    pub memory_bytes: i64,
    pub compute_per_mille: i16,
    pub encoder_per_mille: i16,
    pub memory_reserve_bytes: i64,
    pub compute_limit_per_mille: i16,
    pub encoder_limit_per_mille: i16,
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
    pub rdp_domain: Option<String>,
    pub rdp_proxy_certificate_sha256: Option<String>,
    pub telemetry: NodeTelemetry,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TelemetryBackfillSample {
    pub sample_id: Uuid,
    pub telemetry: NodeTelemetry,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TelemetryProbeState {
    Ready,
    Partial,
    Unavailable,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeGpuTelemetry {
    pub stable_key: String,
    pub name: String,
    pub runtime_binding_ready: bool,
    pub dedicated_memory_bytes: Option<u64>,
    pub used_memory_bytes: Option<u64>,
    pub utilization_per_mille: Option<u16>,
    pub encoder_utilization_per_mille: Option<u16>,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeTelemetry {
    pub sampled_at: DateTime<Utc>,
    pub probe_state: TelemetryProbeState,
    pub logical_processors: Option<u16>,
    pub cpu_utilization_per_mille: Option<u16>,
    pub memory_total_bytes: Option<u64>,
    pub memory_available_bytes: Option<u64>,
    pub disk_total_bytes: Option<u64>,
    pub disk_free_bytes: Option<u64>,
    pub gpu_inventory_revision: Option<u64>,
    pub gpus: Vec<NodeGpuTelemetry>,
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

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum FrontendTarget {
    Desktop {
        device_id: Uuid,
    },
    CloudApplication {
        application_id: Uuid,
        instance_id: Uuid,
    },
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FrontendGrant {
    pub session_id: Uuid,
    pub revision: i64,
    pub target: FrontendTarget,
    pub client_type: String,
    pub access_role: String,
    pub valid_for_ms: u32,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ExpectedFrontend {
    pub id: Uuid,
    pub revision: i64,
    pub state: String,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct FrontendRetirement {
    pub session_id: Uuid,
    pub challenge_id: Uuid,
    pub reject_through_revision: i64,
    pub deadline: DateTime<Utc>,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelKind {
    Control,
    Media,
    Audio,
    File,
    Rdp,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct OpenChannel {
    pub source_id: Uuid,
    pub session_id: Uuid,
    pub kind: ChannelKind,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelFailure {
    TransportLost,
    PolicyRevoked,
    IoError,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ChannelClose {
    PeerClosed,
    UserStopped,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum ChannelOutcome {
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Progress,
    Closed {
        reason: ChannelClose,
    },
    Failed {
        reason: ChannelFailure,
    },
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ChannelProgress {
    pub sequence: u64,
    pub sent_bytes: u64,
    pub received_bytes: u64,
    pub elapsed_ms: u64,
    pub outcome: ChannelOutcome,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TransferDirection {
    ToNode,
    FromNode,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BeginFileTransfer {
    pub transfer_request_id: Uuid,
    pub session_id: Uuid,
    pub direction: TransferDirection,
    pub file_name: String,
    pub total_bytes: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub expected_sha256: Option<[u8; 32]>,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum TransferFailure {
    TransportLost,
    HashMismatch,
    PolicyRevoked,
    IoError,
    SourceChanged,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum TransferOutcome {
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Progress,
    Completed {
        received_sha256: [u8; 32],
    },
    Failed {
        reason: TransferFailure,
    },
    #[serde(deserialize_with = "strict_empty::deserialize")]
    Cancelled,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct TransferProgress {
    pub sequence: u64,
    pub transferred_bytes: u64,
    pub outcome: TransferOutcome,
}

#[derive(Clone, Copy, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RecordingCodec {
    H264,
    H265,
    Av1,
    Unknown,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RecordingReport {
    pub source_id: Uuid,
    pub source_sha256: [u8; 32],
    pub session_id: Option<Uuid>,
    pub file_name: String,
    pub size_bytes: u64,
    pub modified_unix_ms: i64,
    pub codec: RecordingCodec,
    pub sequence: u64,
    pub present: bool,
}

/// A short-lived, one-use upload capability returned only on an authenticated node-control
/// connection. The node resolves `source_id` inside its private recording inventory and sends
/// bytes to `upload_path` on the same Console origin; it never supplies a filesystem path.
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RecordingCacheUpload {
    pub attempt_id: Uuid,
    pub recording_id: Uuid,
    pub source_id: Uuid,
    pub size_bytes: u64,
    pub source_sha256: [u8; 32],
    pub upload_path: String,
    pub upload_token: String,
    pub valid_for_ms: u32,
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
    ReportTelemetryBackfill {
        request_id: u64,
        samples: Vec<TelemetryBackfillSample>,
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
    ListFrontends {
        request_id: u64,
    },
    AdmitFrontend {
        request_id: u64,
        session_id: Uuid,
        revision: i64,
        frontend_token: String,
    },
    BeginFrontendRetirement {
        request_id: u64,
        session_id: Uuid,
    },
    FinishFrontendRetirement {
        request_id: u64,
        session_id: Uuid,
        challenge_id: Uuid,
    },
    OpenChannel {
        request_id: u64,
        channel: OpenChannel,
    },
    ReportChannel {
        request_id: u64,
        channel_id: Uuid,
        progress: ChannelProgress,
    },
    BeginFileTransfer {
        request_id: u64,
        transfer: BeginFileTransfer,
    },
    ReportFileTransfer {
        request_id: u64,
        transfer_id: Uuid,
        progress: TransferProgress,
    },
    ReportRecording {
        request_id: u64,
        recording: RecordingReport,
    },
    PollRecordingCache {
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
            | Self::ReportTelemetryBackfill { request_id, .. }
            | Self::BeginReconciliation { request_id }
            | Self::Reconcile { request_id, .. }
            | Self::PollCommand { request_id }
            | Self::AcknowledgeCommand { request_id, .. }
            | Self::ReportDeployment { request_id, .. }
            | Self::ListDeployments { request_id, .. }
            | Self::ListFrontends { request_id }
            | Self::AdmitFrontend { request_id, .. }
            | Self::BeginFrontendRetirement { request_id, .. }
            | Self::FinishFrontendRetirement { request_id, .. }
            | Self::OpenChannel { request_id, .. }
            | Self::ReportChannel { request_id, .. }
            | Self::BeginFileTransfer { request_id, .. }
            | Self::ReportFileTransfer { request_id, .. }
            | Self::ReportRecording { request_id, .. }
            | Self::PollRecordingCache { request_id, .. } => *request_id,
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
        device_id: Uuid,
        generation: i64,
        control_epoch: i64,
        relay: Option<RelayEndpoint>,
    },
    Reported {
        request_id: u64,
        state: String,
        endpoint_revision: i64,
    },
    TelemetryBackfilled {
        request_id: u64,
        sample_ids: Vec<Uuid>,
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
    Frontends {
        request_id: u64,
        frontends: Vec<ExpectedFrontend>,
    },
    FrontendAdmitted {
        request_id: u64,
        grant: FrontendGrant,
    },
    FrontendRetirementStarted {
        request_id: u64,
        retirement: FrontendRetirement,
    },
    FrontendRetired {
        request_id: u64,
        session_id: Uuid,
        revision: i64,
    },
    ChannelOpened {
        request_id: u64,
        channel_id: Uuid,
        state: String,
        sequence: i64,
        revision: i64,
    },
    ChannelReported {
        request_id: u64,
        channel_id: Uuid,
        state: String,
        sequence: i64,
        revision: i64,
    },
    FileTransferStarted {
        request_id: u64,
        transfer_id: Uuid,
        state: String,
        sequence: i64,
        revision: i64,
    },
    FileTransferReported {
        request_id: u64,
        transfer_id: Uuid,
        state: String,
        sequence: i64,
        revision: i64,
    },
    RecordingReported {
        request_id: u64,
        recording_id: Uuid,
        reported_present: bool,
        source_sequence: i64,
        revision: i64,
    },
    RecordingCacheUploads {
        request_id: u64,
        uploads: Vec<RecordingCacheUpload>,
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
            | Self::TelemetryBackfilled { request_id, .. }
            | Self::ReconciliationStarted { request_id, .. }
            | Self::Reconciled { request_id }
            | Self::Command { request_id, .. }
            | Self::CommandAcknowledged { request_id, .. }
            | Self::DeploymentReported { request_id }
            | Self::Deployments { request_id, .. }
            | Self::Frontends { request_id, .. }
            | Self::FrontendAdmitted { request_id, .. }
            | Self::FrontendRetirementStarted { request_id, .. }
            | Self::FrontendRetired { request_id, .. }
            | Self::ChannelOpened { request_id, .. }
            | Self::ChannelReported { request_id, .. }
            | Self::FileTransferStarted { request_id, .. }
            | Self::FileTransferReported { request_id, .. }
            | Self::RecordingReported { request_id, .. }
            | Self::RecordingCacheUploads { request_id, .. } => Some(*request_id),
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
            r#"{"type":"admit_frontend","request_id":1,"session_id":"00000000-0000-0000-0000-000000000000","revision":1,"frontend_token":"secret","extra":1}"#,
            r#"{"type":"report_channel","request_id":1,"channel_id":"00000000-0000-0000-0000-000000000000","progress":{"sequence":1,"sent_bytes":0,"received_bytes":0,"elapsed_ms":0,"outcome":{"kind":"progress","extra":1}}}"#,
            r#"{"type":"report_file_transfer","request_id":1,"transfer_id":"00000000-0000-0000-0000-000000000000","progress":{"sequence":1,"transferred_bytes":0,"outcome":{"kind":"cancelled","extra":1}}}"#,
        ] {
            assert!(serde_json::from_str::<NodeRequest>(invalid).is_err());
        }
        let response = serde_json::to_string(&NodeResponse::Authenticated {
            request_id: 1,
            node_id: Uuid::nil(),
            device_id: Uuid::nil(),
            generation: 2,
            control_epoch: 3,
            relay: None,
        })
        .unwrap();
        assert!(!response.contains("token"));
        assert!(!response.contains("secret"));

        let admitted = serde_json::to_string(&NodeResponse::FrontendAdmitted {
            request_id: 2,
            grant: FrontendGrant {
                session_id: Uuid::nil(),
                revision: 1,
                target: FrontendTarget::CloudApplication {
                    application_id: Uuid::nil(),
                    instance_id: Uuid::nil(),
                },
                client_type: "android".into(),
                access_role: "controller".into(),
                valid_for_ms: 30_000,
            },
        })
        .unwrap();
        assert!(!admitted.contains("token"));
        assert!(!admitted.contains("secret"));
    }
}
