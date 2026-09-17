//! Recovery-set manifests and retention decisions shared by the Windows and Linux backup executors.
//! This crate does not execute database tools or accept arbitrary commands.

mod executor;
mod manifest;
mod repository;
mod retention;
mod runtime;
mod scheduler;

pub use executor::{
    BackupCancellation, BackupError, BackupPlan, BackupRunner, BackupTarget, DatabaseTarget,
    LogicalBackupTool, PinnedPgTools,
};
pub use manifest::{
    BackupMember, BackupMemberState, BackupService, RecoverySetKind, RecoverySetManifest,
    RecoverySetStatus, MANIFEST_SCHEMA_VERSION,
};
pub use repository::{BackupRepository, RepositoryError, StagedRecoverySet};
pub use retention::{retained_set_ids, RetentionClass, RetentionPolicy};
pub use runtime::{
    BackupDaemon, BackupDaemonConfig, BackupDaemonError, BackupDaemonStatus, BackupRuntimeAlert,
    BACKUP_DAEMON_CONFIG_SCHEMA_VERSION, BACKUP_DAEMON_STATUS_SCHEMA_VERSION,
};
pub use scheduler::{
    BackupScheduleConfig, BackupTask, BackupTaskOutcome, BackupTaskSnapshot, BackupTaskStore,
    SchedulerError,
};
