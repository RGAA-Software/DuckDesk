//! Recovery-set manifests and retention decisions shared by the Windows and Linux backup executors.
//! This crate does not execute database tools or accept arbitrary commands.

mod barrier;
mod executor;
mod manifest;
mod recovery_seal;
mod repository;
mod restore;
mod restore_executor;
mod restore_store;
mod retention;
mod runtime;
mod scheduler;
mod witness_store;

pub use barrier::{
    PinnedPgWriteBarrierCoordinator, WriteBarrierCoordinatorPlan, WriteBarrierDatabaseTarget,
    WriteBarrierError,
};
pub use executor::{
    BackupCancellation, BackupError, BackupPlan, BackupRunner, BackupTarget, DatabaseTarget,
    LogicalBackupTool, PinnedPgTools, WriteBarrierProof, WriteBarrierServiceAttestation,
    WRITE_BARRIER_PROOF_SCHEMA_VERSION,
};
pub use manifest::{
    BackupMember, BackupMemberState, BackupService, RecoveryEvidenceUnavailableReason,
    RecoverySecurityEvidence, RecoverySetKind, RecoverySetManifest, RecoverySetStatus,
    ServiceSecurityWatermark, MANIFEST_SCHEMA_VERSION,
};
pub use recovery_seal::{
    PinnedPgRecoverySealTool, RecoverySealError, RecoverySealPlan, RecoverySealReport,
    RecoverySealRunner, RecoverySealServiceReport, RecoverySealTargetResult, RecoverySealTool,
    RECOVERY_SEAL_REPORT_SCHEMA_VERSION,
};
pub use repository::{BackupRepository, RepositoryError, StagedRecoverySet};
pub use restore::{
    evaluate_restore_admission, restore_admission_evidence_sha256, ExternalRecoveryWitness,
    RestoreAdmissionBlocker, RestoreAdmissionDecision, RestoreAdmissionError,
    RestoreOperationalCheck, RECOVERY_WITNESS_SCHEMA_VERSION,
};
pub use restore_executor::{
    LogicalRestoreTool, PinnedPgRestoreProvisioner, PinnedPgRestoreTools, RestoreDatabaseTarget,
    RestoreExecutionError, RestoreExecutionPlan, RestoreExecutionReport,
    RestoreOperatorProvisionPlan, RestoreRunner, RestoredMember,
    RESTORE_EXECUTION_REPORT_SCHEMA_VERSION,
};
pub use restore_store::{
    RestoreAdmissionApproval, RestoreAdmissionRecord, RestoreAdmissionState, RestoreAdmissionStore,
    RestoreAdmissionStoreError, RESTORE_ADMISSION_RECORD_SCHEMA_VERSION,
};
pub use retention::{retained_set_ids, RetentionClass, RetentionPolicy};
pub use runtime::{
    BackupDaemon, BackupDaemonConfig, BackupDaemonError, BackupDaemonStatus, BackupRuntimeAlert,
    BACKUP_DAEMON_CONFIG_SCHEMA_VERSION, BACKUP_DAEMON_STATUS_SCHEMA_VERSION,
};
pub use scheduler::{
    BackupScheduleConfig, BackupTask, BackupTaskOutcome, BackupTaskSnapshot, BackupTaskStore,
    SchedulerError,
};
pub use witness_store::{RecordedRecoveryWitness, RecoveryWitnessStore, WitnessStoreError};
