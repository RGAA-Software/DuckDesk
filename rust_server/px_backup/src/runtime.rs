use crate::{
    BackupCancellation, BackupError, BackupPlan, BackupRepository, BackupRunner,
    BackupScheduleConfig, BackupTask, BackupTaskOutcome, BackupTaskStore, LogicalBackupTool,
    PinnedPgTools, RepositoryError, RetentionPolicy, SchedulerError,
};
use serde::{Deserialize, Serialize};
use std::{
    fs::{self, File, OpenOptions},
    io::Write,
    path::{Path, PathBuf},
    time::Duration,
};
use uuid::Uuid;

pub const BACKUP_DAEMON_CONFIG_SCHEMA_VERSION: u32 = 2;
pub const BACKUP_DAEMON_STATUS_SCHEMA_VERSION: u32 = 2;
const MAX_STATUS_BYTES: usize = 64 * 1024;
const MAX_METRICS_BYTES: usize = 16 * 1024;

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum BackupDaemonError {
    #[error("invalid backup daemon configuration")]
    InvalidConfig,
    #[error("private backup daemon configuration is unavailable")]
    ConfigUnavailable,
    #[error("backup daemon repository is unavailable")]
    Repository,
    #[error("backup daemon scheduler is unavailable")]
    Scheduler,
    #[error("backup daemon status is unavailable")]
    Status,
    #[error("pinned PostgreSQL tools are unavailable")]
    Tool,
}

impl From<RepositoryError> for BackupDaemonError {
    fn from(_: RepositoryError) -> Self {
        Self::Repository
    }
}

impl From<SchedulerError> for BackupDaemonError {
    fn from(_: SchedulerError) -> Self {
        Self::Scheduler
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BackupDaemonConfig {
    pub schema_version: u32,
    pub deployment_id: Uuid,
    pub repository_root: PathBuf,
    pub offsite_repository_root: Option<PathBuf>,
    pub scheduler_root: PathBuf,
    pub status_root: PathBuf,
    pub pg_dump_path: PathBuf,
    pub pg_dump_sha256: String,
    pub pg_restore_path: PathBuf,
    pub pg_restore_sha256: String,
    pub command_timeout_seconds: u64,
    pub poll_interval_seconds: u64,
    pub schedule: BackupScheduleConfig,
    pub retention: RetentionPolicy,
    pub offsite_retention: Option<RetentionPolicy>,
    pub plan: BackupPlan,
}

impl BackupDaemonConfig {
    pub fn load_private(path: &Path) -> Result<Self, BackupDaemonError> {
        if !path.is_absolute() {
            return Err(BackupDaemonError::InvalidConfig);
        }
        let bytes = px_private_files::private::read_private(path)
            .map_err(|_| BackupDaemonError::ConfigUnavailable)?;
        let config =
            serde_json::from_slice::<Self>(&bytes).map_err(|_| BackupDaemonError::InvalidConfig)?;
        config.validate()?;
        Ok(config)
    }

    pub fn validate(&self) -> Result<(), BackupDaemonError> {
        if self.schema_version != BACKUP_DAEMON_CONFIG_SCHEMA_VERSION
            || self.deployment_id.is_nil()
            || self.schedule.deployment_id != self.deployment_id
            || self.plan.deployment_id != self.deployment_id
            || !(1..=300).contains(&self.poll_interval_seconds)
            || !(1..=24 * 60 * 60).contains(&self.command_timeout_seconds)
            || !valid_retention(self.retention)
            || self
                .offsite_retention
                .is_some_and(|retention| !valid_retention(retention))
            || self.offsite_repository_root.is_some() != self.offsite_retention.is_some()
        {
            return Err(BackupDaemonError::InvalidConfig);
        }
        self.schedule
            .validate()
            .map_err(|_| BackupDaemonError::InvalidConfig)?;
        self.plan
            .validate()
            .map_err(|_| BackupDaemonError::InvalidConfig)?;
        let paths = [
            &self.repository_root,
            &self.scheduler_root,
            &self.status_root,
            &self.pg_dump_path,
            &self.pg_restore_path,
        ];
        if paths.iter().any(|path| !path.is_absolute())
            || self.repository_root == self.scheduler_root
            || self.repository_root == self.status_root
            || self.scheduler_root == self.status_root
        {
            return Err(BackupDaemonError::InvalidConfig);
        }
        if self.offsite_repository_root.as_ref().is_some_and(|root| {
            !root.is_absolute()
                || root == &self.repository_root
                || root == &self.scheduler_root
                || root == &self.status_root
        }) {
            return Err(BackupDaemonError::InvalidConfig);
        }
        Ok(())
    }
}

fn valid_retention(retention: RetentionPolicy) -> bool {
    retention.hourly > 0
        && retention.daily > 0
        && retention.weekly > 0
        && retention.monthly > 0
        && retention.pre_upgrade > 0
        && retention.manual_days > 0
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum BackupRuntimeAlert {
    ConsecutiveFailures,
    BackupOverdue,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BackupDaemonStatus {
    pub schema_version: u32,
    pub deployment_id: Uuid,
    pub service_started_at_unix: u64,
    pub updated_at_unix: u64,
    pub scheduler_revision: u64,
    pub active_task: Option<BackupTask>,
    pub last_success_at_unix: Option<u64>,
    pub last_recovery_set_id: Option<Uuid>,
    pub last_local_recovery_set_id: Option<Uuid>,
    pub offsite_configured: bool,
    pub offsite_repository_healthy: bool,
    pub last_offsite_recovery_set_id: Option<Uuid>,
    pub last_failure_code: Option<String>,
    pub consecutive_failures: u32,
    pub overdue: bool,
    pub alerts: Vec<BackupRuntimeAlert>,
}

pub struct BackupDaemon<T> {
    config: BackupDaemonConfig,
    repository: BackupRepository,
    offsite_repository: Option<BackupRepository>,
    scheduler: BackupTaskStore,
    runner: BackupRunner<T>,
    cancellation: BackupCancellation,
    service_started_at_unix: u64,
}

impl BackupDaemon<PinnedPgTools> {
    pub fn open(config: BackupDaemonConfig, now_unix: u64) -> Result<Self, BackupDaemonError> {
        config.validate()?;
        let cancellation = BackupCancellation::default();
        let tools = PinnedPgTools::new(
            config.pg_dump_path.clone(),
            config.pg_dump_sha256.clone(),
            config.pg_restore_path.clone(),
            config.pg_restore_sha256.clone(),
            Duration::from_secs(config.command_timeout_seconds),
            cancellation.clone(),
        )
        .map_err(|_| BackupDaemonError::Tool)?;
        Self::open_with_tool(config, tools, cancellation, now_unix)
    }
}

impl<T: LogicalBackupTool> BackupDaemon<T> {
    pub fn open_with_tool(
        config: BackupDaemonConfig,
        tool: T,
        cancellation: BackupCancellation,
        now_unix: u64,
    ) -> Result<Self, BackupDaemonError> {
        config.validate()?;
        if now_unix == 0 {
            return Err(BackupDaemonError::InvalidConfig);
        }
        px_private_files::private::verify_private_directory(&config.status_root)
            .map_err(|_| BackupDaemonError::Status)?;
        verify_status_entries(&config.status_root)?;
        let repository = BackupRepository::open(&config.repository_root, config.deployment_id)?;
        repository.discard_incomplete_sets()?;
        let offsite_repository = config
            .offsite_repository_root
            .as_ref()
            .map(|root| BackupRepository::open(root, config.deployment_id))
            .transpose()?;
        if let Some(offsite_repository) = &offsite_repository {
            offsite_repository.discard_incomplete_sets()?;
        }
        let mut scheduler = BackupTaskStore::open(&config.scheduler_root, config.schedule)?;
        scheduler.reconcile_after_restart(now_unix)?;
        let daemon = Self {
            config,
            repository,
            offsite_repository,
            scheduler,
            runner: BackupRunner::new(tool),
            cancellation,
            service_started_at_unix: now_unix,
        };
        daemon.publish_status(now_unix)?;
        Ok(daemon)
    }

    pub fn cancellation(&self) -> BackupCancellation {
        self.cancellation.clone()
    }

    pub fn poll_interval(&self) -> Duration {
        Duration::from_secs(self.config.poll_interval_seconds)
    }

    pub fn run_due(&mut self, now_unix: u64) -> Result<bool, BackupDaemonError> {
        let Some(task) = self.scheduler.poll(now_unix)? else {
            self.publish_status(now_unix)?;
            return Ok(false);
        };
        self.publish_status(now_unix)?;
        let (outcome, discard_local_incomplete, discard_offsite_incomplete) = match self
            .runner
            .run(&self.repository, &self.config.plan)
        {
            Ok(manifest) => {
                let mut failure_code = None;
                if let (Some(offsite_repository), Some(offsite_retention)) =
                    (&self.offsite_repository, self.config.offsite_retention)
                {
                    match self
                        .repository
                        .replicate_verified_to(offsite_repository, manifest.recovery_set_id)
                    {
                        Ok(_) => {
                            if offsite_repository
                                .prune_after_verified(
                                    manifest.recovery_set_id,
                                    offsite_retention,
                                    now_unix,
                                )
                                .is_err()
                            {
                                failure_code = Some("OFFSITE_RETENTION_FAILURE");
                            }
                        }
                        Err(_) => failure_code = Some("OFFSITE_REPLICATION_FAILURE"),
                    }
                }
                if self
                    .repository
                    .prune_after_verified(manifest.recovery_set_id, self.config.retention, now_unix)
                    .is_err()
                {
                    failure_code = Some("LOCAL_RETENTION_FAILURE");
                }
                (
                    failure_code.map_or(
                        BackupTaskOutcome::Succeeded {
                            recovery_set_id: manifest.recovery_set_id,
                        },
                        |code| BackupTaskOutcome::Failed {
                            code: code.to_string(),
                        },
                    ),
                    false,
                    failure_code == Some("OFFSITE_REPLICATION_FAILURE"),
                )
            }
            Err(error) => (
                BackupTaskOutcome::Failed {
                    code: backup_failure_code(error).to_string(),
                },
                true,
                false,
            ),
        };
        self.scheduler.complete(task.task_id, now_unix, outcome)?;
        if discard_local_incomplete {
            self.repository.discard_incomplete_sets()?;
        }
        if discard_offsite_incomplete {
            if let Some(offsite_repository) = &self.offsite_repository {
                offsite_repository.discard_incomplete_sets()?;
            }
        }
        self.publish_status(now_unix)?;
        Ok(true)
    }

    pub fn status(&self, now_unix: u64) -> Result<BackupDaemonStatus, BackupDaemonError> {
        let snapshot = self.scheduler.snapshot();
        let mut last_success_at_unix = None;
        let mut last_recovery_set_id = None;
        let mut last_failure_code = None;
        let mut consecutive_failures = 0_u32;
        for task in snapshot.recent.iter().rev() {
            match &task.outcome {
                Some(BackupTaskOutcome::Succeeded { recovery_set_id }) => {
                    last_success_at_unix = task.completed_at_unix;
                    last_recovery_set_id = Some(*recovery_set_id);
                    break;
                }
                Some(BackupTaskOutcome::Failed { code }) => {
                    if last_failure_code.is_none() {
                        last_failure_code = Some(code.clone());
                    }
                    consecutive_failures = consecutive_failures.saturating_add(1);
                }
                Some(BackupTaskOutcome::Interrupted) => {
                    if last_failure_code.is_none() {
                        last_failure_code = Some("INTERRUPTED".to_string());
                    }
                    consecutive_failures = consecutive_failures.saturating_add(1);
                }
                None => {}
            }
        }
        let overdue_boundary = last_success_at_unix
            .unwrap_or(self.config.schedule.anchor_unix)
            .saturating_add(self.config.schedule.period_seconds.saturating_mul(2));
        let overdue = now_unix > overdue_boundary;
        let mut alerts = Vec::new();
        if consecutive_failures >= 2 {
            alerts.push(BackupRuntimeAlert::ConsecutiveFailures);
        }
        if overdue {
            alerts.push(BackupRuntimeAlert::BackupOverdue);
        }
        let last_local_recovery_set_id = newest_verified_recovery_set_id(&self.repository)?;
        let (offsite_repository_healthy, last_offsite_recovery_set_id) =
            if let Some(offsite_repository) = &self.offsite_repository {
                match newest_verified_recovery_set_id(offsite_repository) {
                    Ok(recovery_set_id) => (true, recovery_set_id),
                    Err(_) => (false, None),
                }
            } else {
                (false, None)
            };
        Ok(BackupDaemonStatus {
            schema_version: BACKUP_DAEMON_STATUS_SCHEMA_VERSION,
            deployment_id: self.config.deployment_id,
            service_started_at_unix: self.service_started_at_unix,
            updated_at_unix: now_unix,
            scheduler_revision: snapshot.revision,
            active_task: snapshot.active,
            last_success_at_unix,
            last_recovery_set_id,
            last_local_recovery_set_id,
            offsite_configured: self.offsite_repository.is_some(),
            offsite_repository_healthy,
            last_offsite_recovery_set_id,
            last_failure_code,
            consecutive_failures,
            overdue,
            alerts,
        })
    }

    fn publish_status(&self, now_unix: u64) -> Result<(), BackupDaemonError> {
        let status = self.status(now_unix)?;
        persist_status(&self.config.status_root, &status)?;
        persist_metrics(
            &self.config.status_root,
            &status,
            self.config.poll_interval_seconds,
        )
    }
}

fn newest_verified_recovery_set_id(
    repository: &BackupRepository,
) -> Result<Option<Uuid>, BackupDaemonError> {
    Ok(repository
        .manifests()?
        .into_iter()
        .filter(|manifest| manifest.status.is_verified())
        .max_by_key(|manifest| {
            (
                manifest.completed_at_unix,
                manifest.created_at_unix,
                manifest.recovery_set_id,
            )
        })
        .map(|manifest| manifest.recovery_set_id))
}

fn backup_failure_code(error: BackupError) -> &'static str {
    match error {
        BackupError::InvalidPlan => "INVALID_PLAN",
        BackupError::BarrierRequired => "BARRIER_REQUIRED",
        BackupError::BarrierInvalid => "BARRIER_INVALID",
        BackupError::BarrierInactive => "BARRIER_INACTIVE",
        BackupError::BarrierChanged => "BARRIER_CHANGED",
        BackupError::Repository => "REPOSITORY_FAILURE",
        BackupError::ToolIdentity => "TOOL_IDENTITY_FAILURE",
        BackupError::Credential => "CREDENTIAL_FAILURE",
        BackupError::ArchiveFailed => "ARCHIVE_FAILURE",
        BackupError::VerificationFailed => "VERIFICATION_FAILURE",
        BackupError::ToolTimeout => "TOOL_TIMEOUT",
        BackupError::Cancelled => "CANCELLED",
        BackupError::Clock => "CLOCK_FAILURE",
    }
}

fn verify_status_entries(root: &Path) -> Result<(), BackupDaemonError> {
    for directory_entry in fs::read_dir(root).map_err(|_| BackupDaemonError::Status)? {
        let directory_entry = directory_entry.map_err(|_| BackupDaemonError::Status)?;
        let file_name = directory_entry
            .file_name()
            .to_str()
            .ok_or(BackupDaemonError::Status)?
            .to_string();
        if !matches!(
            file_name.as_str(),
            "status.json"
                | "status.previous"
                | "status.next"
                | "metrics.prom"
                | "metrics.previous"
                | "metrics.next"
        ) {
            return Err(BackupDaemonError::Status);
        }
        let file_type = directory_entry
            .file_type()
            .map_err(|_| BackupDaemonError::Status)?;
        if !file_type.is_file() || file_type.is_symlink() {
            return Err(BackupDaemonError::Status);
        }
    }
    Ok(())
}

fn persist_status(root: &Path, status: &BackupDaemonStatus) -> Result<(), BackupDaemonError> {
    let bytes = serde_json::to_vec_pretty(status).map_err(|_| BackupDaemonError::Status)?;
    persist_atomic_file(
        root,
        "status.json",
        "status.previous",
        "status.next",
        &bytes,
        MAX_STATUS_BYTES,
    )
}

fn persist_metrics(
    root: &Path,
    status: &BackupDaemonStatus,
    poll_interval_seconds: u64,
) -> Result<(), BackupDaemonError> {
    let consecutive_failure_alert = u8::from(
        status
            .alerts
            .contains(&BackupRuntimeAlert::ConsecutiveFailures),
    );
    let overdue_alert = u8::from(status.alerts.contains(&BackupRuntimeAlert::BackupOverdue));
    let metrics = format!(
        concat!(
            "# HELP pixels_backup_status_timestamp_seconds Unix timestamp of the latest backup daemon status publication.\n",
            "# TYPE pixels_backup_status_timestamp_seconds gauge\n",
            "pixels_backup_status_timestamp_seconds{{deployment_id=\"{}\"}} {}\n",
            "# HELP pixels_backup_poll_interval_seconds Configured backup daemon status polling interval.\n",
            "# TYPE pixels_backup_poll_interval_seconds gauge\n",
            "pixels_backup_poll_interval_seconds{{deployment_id=\"{}\"}} {}\n",
            "# HELP pixels_backup_last_success_timestamp_seconds Unix timestamp of the latest verified backup task.\n",
            "# TYPE pixels_backup_last_success_timestamp_seconds gauge\n",
            "pixels_backup_last_success_timestamp_seconds{{deployment_id=\"{}\"}} {}\n",
            "# HELP pixels_backup_consecutive_failures Number of consecutive failed or interrupted backup tasks.\n",
            "# TYPE pixels_backup_consecutive_failures gauge\n",
            "pixels_backup_consecutive_failures{{deployment_id=\"{}\"}} {}\n",
            "# HELP pixels_backup_alert_consecutive_failures Whether the consecutive-failure alert condition is active.\n",
            "# TYPE pixels_backup_alert_consecutive_failures gauge\n",
            "pixels_backup_alert_consecutive_failures{{deployment_id=\"{}\"}} {}\n",
            "# HELP pixels_backup_alert_overdue Whether the verified-backup overdue alert condition is active.\n",
            "# TYPE pixels_backup_alert_overdue gauge\n",
            "pixels_backup_alert_overdue{{deployment_id=\"{}\"}} {}\n",
            "# HELP pixels_backup_offsite_configured Whether an offsite repository is required by this deployment.\n",
            "# TYPE pixels_backup_offsite_configured gauge\n",
            "pixels_backup_offsite_configured{{deployment_id=\"{}\"}} {}\n",
            "# HELP pixels_backup_offsite_repository_healthy Whether the configured offsite repository can be verified.\n",
            "# TYPE pixels_backup_offsite_repository_healthy gauge\n",
            "pixels_backup_offsite_repository_healthy{{deployment_id=\"{}\"}} {}\n"
        ),
        status.deployment_id,
        status.updated_at_unix,
        status.deployment_id,
        poll_interval_seconds,
        status.deployment_id,
        status.last_success_at_unix.unwrap_or(0),
        status.deployment_id,
        status.consecutive_failures,
        status.deployment_id,
        consecutive_failure_alert,
        status.deployment_id,
        overdue_alert,
        status.deployment_id,
        u8::from(status.offsite_configured),
        status.deployment_id,
        u8::from(status.offsite_repository_healthy),
    );
    persist_atomic_file(
        root,
        "metrics.prom",
        "metrics.previous",
        "metrics.next",
        metrics.as_bytes(),
        MAX_METRICS_BYTES,
    )
}

fn persist_atomic_file(
    root: &Path,
    current_name: &str,
    previous_name: &str,
    next_name: &str,
    bytes: &[u8],
    maximum_bytes: usize,
) -> Result<(), BackupDaemonError> {
    let current_path = root.join(current_name);
    let previous_path = root.join(previous_name);
    let next_path = root.join(next_name);
    if previous_path.exists() || next_path.exists() {
        return Err(BackupDaemonError::Status);
    }
    if bytes.is_empty() || bytes.len() > maximum_bytes {
        return Err(BackupDaemonError::Status);
    }
    let mut next_file = private_new_file(&next_path)?;
    next_file
        .write_all(bytes)
        .map_err(|_| BackupDaemonError::Status)?;
    next_file
        .sync_all()
        .map_err(|_| BackupDaemonError::Status)?;
    drop(next_file);
    if current_path.exists() {
        fs::rename(&current_path, &previous_path).map_err(|_| BackupDaemonError::Status)?;
    }
    fs::rename(&next_path, &current_path).map_err(|_| BackupDaemonError::Status)?;
    sync_directory(root)?;
    if previous_path.exists() {
        fs::remove_file(&previous_path).map_err(|_| BackupDaemonError::Status)?;
        sync_directory(root)?;
    }
    Ok(())
}

fn private_new_file(path: &Path) -> Result<File, BackupDaemonError> {
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
    }
    let file = options.open(path).map_err(|_| BackupDaemonError::Status)?;
    let metadata = file.metadata().map_err(|_| BackupDaemonError::Status)?;
    if !metadata.is_file() {
        return Err(BackupDaemonError::Status);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if metadata.permissions().mode() & 0o077 != 0 {
            return Err(BackupDaemonError::Status);
        }
    }
    Ok(file)
}

fn sync_directory(path: &Path) -> Result<(), BackupDaemonError> {
    #[cfg(windows)]
    {
        let _ = path;
        Ok(())
    }
    #[cfg(unix)]
    {
        File::open(path)
            .and_then(|directory| directory.sync_all())
            .map_err(|_| BackupDaemonError::Status)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{BackupService, BackupTarget, DatabaseTarget, RecoverySetKind, RetentionClass};
    use std::{collections::BTreeSet, io::Read};

    #[derive(Clone, Copy)]
    enum ToolBehavior {
        Succeed,
        FailArchive,
    }

    struct TestBackupTool {
        behavior: ToolBehavior,
    }

    impl LogicalBackupTool for TestBackupTool {
        fn dump(&self, database: &DatabaseTarget, destination: &Path) -> Result<(), BackupError> {
            if matches!(self.behavior, ToolBehavior::FailArchive) {
                return Err(BackupError::ArchiveFailed);
            }
            fs::write(destination, format!("backup:{}", database.database))
                .map_err(|_| BackupError::ArchiveFailed)
        }

        fn verify(&self, archive: &Path) -> Result<(), BackupError> {
            let mut archive_file =
                File::open(archive).map_err(|_| BackupError::VerificationFailed)?;
            let mut archive_contents = String::new();
            archive_file
                .read_to_string(&mut archive_contents)
                .map_err(|_| BackupError::VerificationFailed)?;
            if archive_contents.starts_with("backup:") {
                Ok(())
            } else {
                Err(BackupError::VerificationFailed)
            }
        }
    }

    struct RuntimeFixture {
        _temporary_directory: tempfile::TempDir,
        config: BackupDaemonConfig,
    }

    impl RuntimeFixture {
        fn new() -> Self {
            let temporary_directory = tempfile::Builder::new()
                .prefix("pixels-backup-runtime-")
                .tempdir()
                .unwrap();
            make_private(temporary_directory.path());
            let repository_root = create_private_child(temporary_directory.path(), "repository");
            let scheduler_root = create_private_child(temporary_directory.path(), "scheduler");
            let status_root = create_private_child(temporary_directory.path(), "status");
            let deployment_id = Uuid::new_v4();
            let password_file = temporary_directory.path().join("console.pgpass");
            px_private_files::private::create_private(
                &password_file,
                b"127.0.0.1:5432:pixels_console:pixels_backup:secret\n",
            )
            .unwrap();
            let fake_dump_path = temporary_directory.path().join("pg_dump");
            let fake_restore_path = temporary_directory.path().join("pg_restore");
            let config = BackupDaemonConfig {
                schema_version: BACKUP_DAEMON_CONFIG_SCHEMA_VERSION,
                deployment_id,
                repository_root,
                offsite_repository_root: None,
                scheduler_root,
                status_root,
                pg_dump_path: fake_dump_path,
                pg_dump_sha256: "1".repeat(64),
                pg_restore_path: fake_restore_path,
                pg_restore_sha256: "2".repeat(64),
                command_timeout_seconds: 60,
                poll_interval_seconds: 1,
                schedule: BackupScheduleConfig {
                    deployment_id,
                    anchor_unix: 1_000,
                    period_seconds: 60,
                },
                retention: RetentionPolicy::default(),
                offsite_retention: None,
                plan: BackupPlan {
                    deployment_id,
                    kind: RecoverySetKind::Independent,
                    write_barrier_proof_file: None,
                    retention: BTreeSet::from([RetentionClass::Hourly]),
                    previous_recovery_set_id: None,
                    targets: vec![
                        BackupTarget::Required {
                            database: DatabaseTarget {
                                service: BackupService::Console,
                                host: "127.0.0.1".to_string(),
                                port: 5432,
                                database: "pixels_console".to_string(),
                                username: "pixels_backup".to_string(),
                                password_file,
                                schema_version: 1,
                            },
                        },
                        BackupTarget::NotApplicable {
                            service: BackupService::Auth,
                            reason: "not installed in this private deployment".to_string(),
                        },
                        BackupTarget::NotApplicable {
                            service: BackupService::Desk,
                            reason: "not installed in this private deployment".to_string(),
                        },
                    ],
                },
            };
            Self {
                _temporary_directory: temporary_directory,
                config,
            }
        }
    }

    fn create_private_child(parent: &Path, name: &str) -> PathBuf {
        let child = parent.join(name);
        fs::create_dir(&child).unwrap();
        make_private(&child);
        child
    }

    fn make_private(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(path, fs::Permissions::from_mode(0o700)).unwrap();
        }
        #[cfg(windows)]
        {
            use std::{os::windows::process::CommandExt, process::Command};
            let process_identity = Command::new("whoami")
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(process_identity.status.success());
            let access_grant = format!(
                "{}:(OI)(CI)F",
                String::from_utf8(process_identity.stdout).unwrap().trim()
            );
            let access_result = Command::new("icacls")
                .arg(path)
                .args([
                    "/inheritance:r",
                    "/grant:r",
                    &access_grant,
                    "*S-1-5-18:(OI)(CI)F",
                ])
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(access_result.status.success());
        }
    }

    fn read_published_metrics(status_root: &Path) -> String {
        fs::read_to_string(status_root.join("metrics.prom")).unwrap()
    }

    #[test]
    fn successful_due_task_publishes_verified_set_and_status() {
        let fixture = RuntimeFixture::new();
        let cancellation = BackupCancellation::default();
        let mut daemon = BackupDaemon::open_with_tool(
            fixture.config.clone(),
            TestBackupTool {
                behavior: ToolBehavior::Succeed,
            },
            cancellation,
            1_000,
        )
        .unwrap();
        assert!(daemon.run_due(1_000).unwrap());
        assert!(!daemon.run_due(1_001).unwrap());
        let status = daemon.status(1_001).unwrap();
        assert_eq!(status.consecutive_failures, 0);
        assert_eq!(status.last_success_at_unix, Some(1_000));
        assert!(status.last_recovery_set_id.is_some());
        assert!(!status.overdue);
        assert!(status.alerts.is_empty());
        assert!(fixture.config.status_root.join("status.json").is_file());
        let published_metrics = read_published_metrics(&fixture.config.status_root);
        let deployment_label = format!("deployment_id=\"{}\"", fixture.config.deployment_id);
        assert!(published_metrics.contains(&deployment_label));
        assert!(published_metrics.contains("pixels_backup_last_success_timestamp_seconds"));
        assert!(published_metrics.contains("} 1000\n"));
        assert!(published_metrics.contains("pixels_backup_alert_consecutive_failures"));
        assert!(published_metrics.contains("pixels_backup_alert_overdue"));
        assert!(!published_metrics.contains("secret"));
        drop(daemon);
        let repository = BackupRepository::open(
            &fixture.config.repository_root,
            fixture.config.deployment_id,
        )
        .unwrap();
        assert_eq!(repository.manifests().unwrap().len(), 1);
    }

    #[test]
    fn repeated_failures_publish_independent_failure_and_overdue_alerts() {
        let fixture = RuntimeFixture::new();
        let mut daemon = BackupDaemon::open_with_tool(
            fixture.config.clone(),
            TestBackupTool {
                behavior: ToolBehavior::FailArchive,
            },
            BackupCancellation::default(),
            1_000,
        )
        .unwrap();
        assert!(daemon.run_due(1_000).unwrap());
        assert!(daemon.run_due(1_060).unwrap());
        let status = daemon.status(1_121).unwrap();
        assert_eq!(status.consecutive_failures, 2);
        assert!(status.overdue);
        assert_eq!(
            status.alerts,
            vec![
                BackupRuntimeAlert::ConsecutiveFailures,
                BackupRuntimeAlert::BackupOverdue
            ]
        );
        daemon.publish_status(1_121).unwrap();
        let published_metrics = read_published_metrics(&fixture.config.status_root);
        let deployment_label = format!("deployment_id=\"{}\"", fixture.config.deployment_id);
        assert!(published_metrics.contains(&format!(
            "pixels_backup_alert_consecutive_failures{{{deployment_label}}} 1"
        )));
        assert!(published_metrics.contains(&format!(
            "pixels_backup_alert_overdue{{{deployment_label}}} 1"
        )));
        assert!(published_metrics.contains("pixels_backup_consecutive_failures"));
        assert!(published_metrics.contains("} 2\n"));
        assert!(!published_metrics.contains("ARCHIVE_FAILURE"));
    }

    #[test]
    fn configured_offsite_repository_is_verified_before_task_success() {
        let mut fixture = RuntimeFixture::new();
        let offsite_root =
            create_private_child(fixture._temporary_directory.path(), "offsite-repository");
        fixture.config.offsite_repository_root = Some(offsite_root.clone());
        fixture.config.offsite_retention = Some(RetentionPolicy::default());
        let mut daemon = BackupDaemon::open_with_tool(
            fixture.config.clone(),
            TestBackupTool {
                behavior: ToolBehavior::Succeed,
            },
            BackupCancellation::default(),
            1_000,
        )
        .unwrap();
        assert!(daemon.run_due(1_000).unwrap());
        let status = daemon.status(1_001).unwrap();
        assert!(status.offsite_configured);
        assert!(status.offsite_repository_healthy);
        assert_eq!(
            status.last_offsite_recovery_set_id,
            status.last_recovery_set_id
        );
        assert_eq!(
            status.last_local_recovery_set_id,
            status.last_recovery_set_id
        );
        assert_eq!(status.last_failure_code, None);
        drop(daemon);
        let offsite = BackupRepository::open(&offsite_root, fixture.config.deployment_id).unwrap();
        let offsite_manifests = offsite.manifests().unwrap();
        assert_eq!(offsite_manifests.len(), 1);
        assert_eq!(
            offsite_manifests[0].status,
            crate::RecoverySetStatus::OffsiteVerified
        );
    }

    #[test]
    fn offsite_failure_preserves_local_verified_set_and_never_reports_success() {
        let mut fixture = RuntimeFixture::new();
        let offsite_root =
            create_private_child(fixture._temporary_directory.path(), "offsite-repository");
        fixture.config.offsite_repository_root = Some(offsite_root.clone());
        fixture.config.offsite_retention = Some(RetentionPolicy::default());
        let mut daemon = BackupDaemon::open_with_tool(
            fixture.config.clone(),
            TestBackupTool {
                behavior: ToolBehavior::Succeed,
            },
            BackupCancellation::default(),
            1_000,
        )
        .unwrap();
        fs::write(offsite_root.join("unregistered"), b"do not remove").unwrap();
        assert!(daemon.run_due(1_000).unwrap());
        let status = daemon.status(1_001).unwrap();
        assert_eq!(status.last_success_at_unix, None);
        assert!(!status.offsite_repository_healthy);
        assert_eq!(status.last_offsite_recovery_set_id, None);
        assert!(status.last_local_recovery_set_id.is_some());
        assert_eq!(
            status.last_failure_code.as_deref(),
            Some("OFFSITE_REPLICATION_FAILURE")
        );
        drop(daemon);
        let local = BackupRepository::open(
            &fixture.config.repository_root,
            fixture.config.deployment_id,
        )
        .unwrap();
        assert_eq!(local.manifests().unwrap().len(), 1);
        assert_eq!(
            fs::read(offsite_root.join("unregistered")).unwrap(),
            b"do not remove"
        );
    }

    #[test]
    fn restart_reconciles_active_task_and_retries_the_same_schedule() {
        let fixture = RuntimeFixture::new();
        let interrupted_task = {
            let mut scheduler =
                BackupTaskStore::open(&fixture.config.scheduler_root, fixture.config.schedule)
                    .unwrap();
            scheduler.poll(1_000).unwrap().unwrap()
        };
        let mut daemon = BackupDaemon::open_with_tool(
            fixture.config.clone(),
            TestBackupTool {
                behavior: ToolBehavior::Succeed,
            },
            BackupCancellation::default(),
            1_010,
        )
        .unwrap();
        assert!(daemon.run_due(1_010).unwrap());
        let status = daemon.status(1_010).unwrap();
        assert_eq!(status.last_success_at_unix, Some(1_010));
        drop(daemon);
        let scheduler =
            BackupTaskStore::open(&fixture.config.scheduler_root, fixture.config.schedule).unwrap();
        let snapshot = scheduler.snapshot();
        assert_eq!(snapshot.recent.len(), 2);
        assert_eq!(snapshot.recent[0].task_id, interrupted_task.task_id);
        assert_eq!(snapshot.recent[1].task_id, interrupted_task.task_id);
        assert_eq!(snapshot.recent[1].attempt, 2);
    }

    #[test]
    fn private_config_rejects_unknown_fields_and_mismatched_deployment() {
        let fixture = RuntimeFixture::new();
        let config_path = fixture
            ._temporary_directory
            .path()
            .join("backup-config.json");
        let mut config_value = serde_json::to_value(&fixture.config).unwrap();
        config_value
            .as_object_mut()
            .unwrap()
            .insert("arbitrary_command".to_string(), serde_json::json!("whoami"));
        let config_bytes = serde_json::to_vec(&config_value).unwrap();
        px_private_files::private::create_private(&config_path, &config_bytes).unwrap();
        assert_eq!(
            BackupDaemonConfig::load_private(&config_path),
            Err(BackupDaemonError::InvalidConfig)
        );
        let mut mismatched = fixture.config;
        mismatched.plan.deployment_id = Uuid::new_v4();
        assert_eq!(mismatched.validate(), Err(BackupDaemonError::InvalidConfig));
    }

    #[test]
    fn status_directory_rejects_unregistered_content() {
        let fixture = RuntimeFixture::new();
        fs::write(fixture.config.status_root.join("unregistered.txt"), b"keep").unwrap();
        let result = BackupDaemon::open_with_tool(
            fixture.config,
            TestBackupTool {
                behavior: ToolBehavior::Succeed,
            },
            BackupCancellation::default(),
            1_000,
        );
        assert!(matches!(result, Err(BackupDaemonError::Status)));
    }
}
