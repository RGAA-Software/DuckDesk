//! Strict shared wire contract between Console and a node Service.
//! This crate contains no database, process or transport implementation.

use chrono::{DateTime, Utc};
use px_release_catalog::ReleaseSpec;
use serde::{Deserialize, Serialize};
use uuid::Uuid;
use zeroize::Zeroizing;

fn serialize_secret<S>(value: &Zeroizing<String>, serializer: S) -> Result<S::Ok, S::Error>
where
    S: serde::Serializer,
{
    serializer.serialize_str(value.as_str())
}

fn deserialize_secret<'de, D>(deserializer: D) -> Result<Zeroizing<String>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    String::deserialize(deserializer).map(Zeroizing::new)
}

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

/// Decrypted only for an authenticated node holding this exact live RDP Start
/// lease. Deliberately not Debug or Clone so credentials cannot enter generic
/// command diagnostics or retry snapshots.
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RdpWorkspaceCredential {
    pub workspace_id: Uuid,
    pub account_name: String,
    pub credential_revision: u32,
    pub expected_sid: Option<String>,
    #[serde(
        serialize_with = "serialize_secret",
        deserialize_with = "deserialize_secret"
    )]
    pub password: Zeroizing<String>,
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

/// An approved release offered to one authenticated node. The release catalog is policy
/// metadata only: receiving this value never authorizes installation before the node's
/// independent package-signature and anti-rollback checks succeed.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeUpdateOffer {
    pub release_id: Uuid,
    pub policy_revision: i64,
    pub artifact: ReleaseSpec,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "result", rename_all = "snake_case", deny_unknown_fields)]
pub enum UpdateActivationOutcome {
    Installed,
    Failed { error_code: String },
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
    FetchRdpWorkspace {
        request_id: u64,
        command_id: Uuid,
        lease_id: Uuid,
    },
    ConfirmRdpWorkspace {
        request_id: u64,
        command_id: Uuid,
        lease_id: Uuid,
        workspace_id: Uuid,
        windows_sid: String,
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
    CheckUpdate {
        request_id: u64,
        current_build_number: i64,
    },
    BeginUpdateActivation {
        request_id: u64,
        release_id: Uuid,
        policy_revision: i64,
        prepared_sha256: String,
    },
    FinishUpdateActivation {
        request_id: u64,
        task_id: Uuid,
        lease_id: Uuid,
        outcome: UpdateActivationOutcome,
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
            | Self::FetchRdpWorkspace { request_id, .. }
            | Self::ConfirmRdpWorkspace { request_id, .. }
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
            | Self::PollRecordingCache { request_id, .. }
            | Self::CheckUpdate { request_id, .. }
            | Self::BeginUpdateActivation { request_id, .. }
            | Self::FinishUpdateActivation { request_id, .. } => *request_id,
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
    RdpWorkspace {
        request_id: u64,
        workspace: RdpWorkspaceCredential,
    },
    RdpWorkspaceConfirmed {
        request_id: u64,
        workspace_id: Uuid,
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
    UpdateChecked {
        request_id: u64,
        offer: Option<NodeUpdateOffer>,
    },
    UpdateActivationGranted {
        request_id: u64,
        task_id: Uuid,
        lease_id: Uuid,
        lease_until: DateTime<Utc>,
    },
    UpdateActivationFinished {
        request_id: u64,
        state: String,
        revision: i64,
        error_code: Option<String>,
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
            | Self::RdpWorkspace { request_id, .. }
            | Self::RdpWorkspaceConfirmed { request_id, .. }
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
            | Self::RecordingCacheUploads { request_id, .. }
            | Self::UpdateChecked { request_id, .. }
            | Self::UpdateActivationGranted { request_id, .. }
            | Self::UpdateActivationFinished { request_id, .. } => Some(*request_id),
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
    use px_release_catalog::{
        Architecture, Channel, Distribution, OperatingSystem, Product, ReleaseQuery,
    };

    #[test]
    fn node_wire_is_tagged_strict_and_keeps_credentials_out_of_ordinary_responses() {
        let request: NodeRequest =
            serde_json::from_str(r#"{"type":"authenticate","request_id":1,"node_token":"secret"}"#)
                .unwrap();
        assert_eq!(request.request_id(), 1);
        for invalid in [
            r#"{"type":"authenticate","request_id":1,"node_token":"secret","extra":1}"#,
            r#"{"type":"poll_command","request_id":1,"extra":1}"#,
            r#"{"type":"fetch_rdp_workspace","request_id":1,"command_id":"00000000-0000-0000-0000-000000000000","lease_id":"00000000-0000-0000-0000-000000000000","extra":1}"#,
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

    #[test]
    fn rdp_workspace_secret_exists_only_in_the_explicit_lease_response() {
        let workspace_id = Uuid::from_u128(1);
        let encoded = serde_json::to_string(&NodeResponse::RdpWorkspace {
            request_id: 7,
            workspace: RdpWorkspaceCredential {
                workspace_id,
                account_name: "pxrdp_0123456789abcd".into(),
                credential_revision: 3,
                expected_sid: Some("S-1-5-21-1-2-3-1001".into()),
                password: Zeroizing::new("a-secure-workspace-password".into()),
            },
        })
        .unwrap();
        let decoded: NodeResponse = serde_json::from_str(&encoded).unwrap();
        let NodeResponse::RdpWorkspace {
            request_id,
            workspace,
        } = decoded
        else {
            panic!("expected RDP workspace response");
        };
        assert_eq!(request_id, 7);
        assert_eq!(workspace.workspace_id, workspace_id);
        assert_eq!(workspace.password.as_str(), "a-secure-workspace-password");
    }

    #[test]
    fn update_offer_is_explicit_strict_and_carries_no_install_authorization() {
        let offer = NodeUpdateOffer {
            release_id: Uuid::from_u128(5),
            policy_revision: 2,
            artifact: ReleaseSpec {
                target: ReleaseQuery {
                    product: Product::CloudNode,
                    distribution: Distribution::Official,
                    channel: Channel::Stable,
                    os: OperatingSystem::Windows,
                    architecture: Architecture::X86_64,
                },
                build_number: 30368,
                version: "3.3.68".into(),
                metadata_base_url: "https://downloads.example.test/metadata/".into(),
                targets_base_url: "https://downloads.example.test/targets/".into(),
                target_name: "cloud-node.exe".into(),
                sha256: "a".repeat(64),
                platform_signer_sha256: Some("b".repeat(64)),
                size_bytes: 1024,
            },
        };
        let encoded = serde_json::to_value(NodeResponse::UpdateChecked {
            request_id: 9,
            offer: Some(offer.clone()),
        })
        .unwrap();
        assert_eq!(encoded["type"], "update_checked");
        assert!(encoded.get("install_authorized").is_none());
        let decoded: NodeResponse = serde_json::from_value(encoded.clone()).unwrap();
        assert_eq!(decoded.request_id(), Some(9));
        let mut unexpected = encoded;
        unexpected["offer"]["signature_verified"] = serde_json::json!(true);
        assert!(serde_json::from_value::<NodeResponse>(unexpected).is_err());
        assert_eq!(offer.artifact.build_number, 30368);
    }

    #[test]
    fn update_activation_is_a_separate_strict_lease_without_install_commands() {
        let release_id = Uuid::from_u128(5);
        let task_id = Uuid::from_u128(6);
        let lease_id = Uuid::from_u128(7);
        let request = NodeRequest::BeginUpdateActivation {
            request_id: 10,
            release_id,
            policy_revision: 2,
            prepared_sha256: "a".repeat(64),
        };
        let encoded = serde_json::to_value(request).unwrap();
        assert_eq!(encoded["type"], "begin_update_activation");
        assert!(encoded.get("installer_path").is_none());
        assert!(encoded.get("command").is_none());
        let decoded: NodeRequest = serde_json::from_value(encoded.clone()).unwrap();
        assert_eq!(decoded.request_id(), 10);
        let mut unexpected = encoded;
        unexpected["force"] = serde_json::json!(true);
        assert!(serde_json::from_value::<NodeRequest>(unexpected).is_err());

        let response = NodeResponse::UpdateActivationGranted {
            request_id: 10,
            task_id,
            lease_id,
            lease_until: Utc::now() + chrono::TimeDelta::minutes(10),
        };
        let encoded = serde_json::to_value(response).unwrap();
        assert_eq!(encoded["type"], "update_activation_granted");
        assert!(encoded.get("installer_path").is_none());
        assert!(encoded.get("shell_command").is_none());
        assert_eq!(
            serde_json::from_value::<NodeResponse>(encoded)
                .unwrap()
                .request_id(),
            Some(10)
        );

        let finish = serde_json::json!({
            "type":"finish_update_activation",
            "request_id":11,
            "task_id":task_id,
            "lease_id":lease_id,
            "outcome":{"result":"failed","error_code":"installer_failed"}
        });
        assert_eq!(
            serde_json::from_value::<NodeRequest>(finish.clone())
                .unwrap()
                .request_id(),
            11
        );
        let mut unexpected = finish;
        unexpected["outcome"]["retry"] = serde_json::json!(true);
        assert!(serde_json::from_value::<NodeRequest>(unexpected).is_err());
    }
}
