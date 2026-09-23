//! Console-owned PostgreSQL repositories. No transport, global state or legacy backend.
mod activity;
mod activity_history;
mod activity_model;
mod application_events;
mod application_model;
mod applications;
mod bootstrap;
mod cache_collection;
mod cache_model;
mod cache_policy;
mod cache_reads;
mod cache_work;
mod command_model;
mod control;
mod database;
mod deployment_model;
mod deployments;
mod devices;
mod file_metadata;
mod file_transfers;
mod groups;
mod guest_events;
mod guests;
mod identity;
mod instance_commands;
mod instance_model;
mod instance_state;
mod instance_stop;
mod instances;
mod model;
mod node_lifecycle;
mod node_model;
mod nodes;
mod outbox;
mod reconciliation;
mod recording_cache;
mod recording_model;
mod recordings;
mod resource_policy;
mod resource_sessions;
mod saved_connection_model;
mod saved_connections;
mod session_frontends;
mod session_model;
mod session_policy;
mod strict_wire;
mod telemetry_alert_model;
mod telemetry_alerts;
mod transfer_model;
mod update_model;
mod updates;
mod workspace_model;
mod workspace_vault;
mod workspaces;

pub use activity::ActivityStore;
pub use activity_model::{
    ChannelClose, ChannelFailure, ChannelKind, ChannelOutcome, ChannelProgress, ChannelRecord,
    OpenChannel, VisitRecord,
};
pub use application_events::ApplicationEvent;
pub use application_model::{
    ApplicationAccess, ApplicationLaunch, ApplicationSpec, VideoCodec, VideoSpec,
};
pub use applications::{ApplicationCard, ApplicationDefinition, ApplicationStore};
pub use bootstrap::initialize_administrator;
pub use cache_model::{
    CacheAttempt, CacheCredential, CacheOptions, CacheProfile, CacheReadLease, CacheRuntime,
    CachedFile,
};
pub use command_model::{
    CommandOutcome, CommandReceipt, GpuReservation, NodeCommand, NodeCommandAction,
};
pub use control::{ControlStore, ManagedUser, Role};
pub use database::{ConsoleDatabase, PoolStatus};
pub use deployment_model::{
    DeploymentConfiguration, DeploymentObservation, DeploymentProfile, DeploymentTarget,
    GpuResourceProfile, NodeDeploymentAssignment, NodeDeploymentPreparation, PreparationFailure,
    PreparationState,
};
pub use deployments::DeploymentStore;
pub use devices::{DeviceAccess, DeviceIdentity, DevicePlatform, DeviceProfile, DeviceStore};
pub use file_transfers::FileTransferStore;
pub use groups::{GroupProfile, GroupStore};
pub use guest_events::GuestEvent;
pub use guests::{GuestBlockReason, GuestSession, GuestStore, ManagedGuest, OriginFingerprint};
pub use identity::IdentityStore;
pub use instance_model::{
    ApplicationInstance, PlacementCandidate, PlacementPreview, PlacementPreviewRequest,
    PlacementRejectionReason, ResourceCredential, ResourceOwner, StartApplication,
};
pub use instances::InstanceStore;
pub use model::{
    AuthenticatedSession, AvatarContent, ClientType, Credential, PasswordDigest,
    RuntimeEntitlement, StoreError, TokenDigest, UserAvatar, UserProfile, Username,
    MAX_AVATAR_BYTES,
};
pub use node_model::{
    ManagedNodeProfile, ManagedNodeTelemetrySample, NodeConfiguration, NodeConnection,
    NodeGpuHistoryProfile, NodeGpuProfile, NodeGpuTelemetry, NodeProduct, NodeProfile, NodeReport,
    NodeTelemetry, NodeTelemetryBackfillSample, NodeTelemetryProfile, NodeTelemetryTrend,
    NodeTelemetryTrendPoint, RuntimeEpoch, TelemetryHistoryCursor, TelemetryProbeState,
    TelemetryTrendRequest,
};
pub use nodes::NodeStore;
pub use outbox::{AuthorizationEvent, DeliveryFailure};
pub use reconciliation::{
    ExpectedLaunch, ObservedRuntime, ObservedRuntimePhase, ReconciliationChallenge,
    RuntimeInventory,
};
pub use recording_cache::RecordingCacheStore;
pub use recording_model::{RecordingCodec, RecordingProfile, RecordingReport};
pub use recordings::RecordingStore;
pub use resource_sessions::ResourceSessionStore;
pub use saved_connection_model::{
    AudioCapturePreference, CreateSavedConnection, SavedConnection, SavedConnectionSettings,
    SavedConnectionTarget,
};
pub use saved_connections::SavedConnectionStore;
pub use session_frontends::{ExpectedFrontend, FrontendGrant};
pub use session_model::{
    FrontendRetirement, OpenResourceSession, ResourceDescriptor, ResourceSession, SessionAccess,
    SessionTarget,
};
pub use telemetry_alert_model::{
    TelemetryAlertCursor, TelemetryAlertEvent, TelemetryAlertFilter, TelemetryAlertMetric,
    TelemetryAlertPolicy, TelemetryAlertPolicyProfile, TelemetryAlertSeverity, TelemetryAlertState,
};
pub use telemetry_alerts::TelemetryAlertStore;
pub use transfer_model::{
    BeginFileTransfer, FileTransferRecord, TransferDirection, TransferFailure, TransferOutcome,
    TransferProgress,
};
pub use update_model::{
    NodeUpdateActivation, NodeUpdateCompletion, NodeUpdateTrust, NodeUpdateTrustStatus,
    NodeUpdateTrustSummary, UpdateActivationOutcome, UpdateDecision, UpdateRelease,
    UpdateTrustObservation,
};
pub use updates::UpdateStore;
pub use workspace_model::{WorkspaceCommandLease, WorkspaceCredential, WorkspaceProfile};
pub use workspace_vault::{WorkspaceKey, WorkspaceVault};
pub use workspaces::WorkspaceStore;

pub static MIGRATIONS: sqlx::migrate::Migrator = sqlx::migrate!("../migrations");

async fn connect(
    config: &px_pg::DatabaseConfig,
    deployment: uuid::Uuid,
) -> Result<sqlx::PgPool, StoreError> {
    Ok(config
        .connect_runtime(px_pg::Service::Console, deployment, &MIGRATIONS)
        .await?)
}
