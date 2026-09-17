use crate::{
    BackupMember, BackupMemberState, BackupRepository, BackupService, RecoverySetKind,
    RecoverySetManifest, RecoverySetStatus, RepositoryError, RetentionClass,
    MANIFEST_SCHEMA_VERSION,
};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeSet,
    fs::OpenOptions,
    io::Read,
    net::IpAddr,
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use uuid::Uuid;

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum BackupError {
    #[error("invalid backup plan")]
    InvalidPlan,
    #[error("coordinated write-barrier proof is required")]
    BarrierRequired,
    #[error("backup repository rejected the operation")]
    Repository,
    #[error("pinned PostgreSQL tool is unavailable or changed")]
    ToolIdentity,
    #[error("database credential file is unavailable")]
    Credential,
    #[error("database archive command failed")]
    ArchiveFailed,
    #[error("database archive verification failed")]
    VerificationFailed,
    #[error("database archive command timed out")]
    ToolTimeout,
    #[error("database archive operation cancelled")]
    Cancelled,
    #[error("system clock is unavailable")]
    Clock,
}

impl From<RepositoryError> for BackupError {
    fn from(_: RepositoryError) -> Self {
        Self::Repository
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DatabaseTarget {
    pub service: BackupService,
    pub host: String,
    pub port: u16,
    pub database: String,
    pub username: String,
    pub password_file: PathBuf,
    pub schema_version: u32,
}

impl DatabaseTarget {
    fn validate(&self) -> Result<(), BackupError> {
        if self.port == 0
            || self.schema_version == 0
            || !valid_host(&self.host)
            || !valid_identifier(&self.database)
            || !valid_identifier(&self.username)
            || !self.password_file.is_absolute()
        {
            return Err(BackupError::InvalidPlan);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "state", rename_all = "snake_case", deny_unknown_fields)]
pub enum BackupTarget {
    Required {
        database: DatabaseTarget,
    },
    NotApplicable {
        service: BackupService,
        reason: String,
    },
}

impl BackupTarget {
    fn service(&self) -> BackupService {
        match self {
            Self::Required { database } => database.service,
            Self::NotApplicable { service, .. } => *service,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BackupPlan {
    pub deployment_id: Uuid,
    pub kind: RecoverySetKind,
    pub retention: BTreeSet<RetentionClass>,
    pub previous_recovery_set_id: Option<Uuid>,
    pub targets: Vec<BackupTarget>,
}

impl BackupPlan {
    fn validate(&self) -> Result<(), BackupError> {
        if self.deployment_id.is_nil()
            || self.retention.is_empty()
            || self.kind != RecoverySetKind::Independent
        {
            return if self.kind == RecoverySetKind::Independent {
                Err(BackupError::InvalidPlan)
            } else {
                Err(BackupError::BarrierRequired)
            };
        }
        let services = self
            .targets
            .iter()
            .map(BackupTarget::service)
            .collect::<BTreeSet<_>>();
        if self.targets.len() != 3
            || services
                != BTreeSet::from([
                    BackupService::Console,
                    BackupService::Auth,
                    BackupService::Desk,
                ])
        {
            return Err(BackupError::InvalidPlan);
        }
        for target in &self.targets {
            match target {
                BackupTarget::Required { database } => database.validate()?,
                BackupTarget::NotApplicable { service, reason } => {
                    if *service == BackupService::Console
                        || reason.trim().is_empty()
                        || reason.len() > 256
                    {
                        return Err(BackupError::InvalidPlan);
                    }
                }
            }
        }
        Ok(())
    }
}

pub trait LogicalBackupTool {
    fn dump(&self, target: &DatabaseTarget, destination: &Path) -> Result<(), BackupError>;
    fn verify(&self, archive: &Path) -> Result<(), BackupError>;
}

#[derive(Debug, Clone, Default)]
pub struct BackupCancellation {
    cancelled: Arc<AtomicBool>,
}

impl BackupCancellation {
    pub fn cancel(&self) {
        self.cancelled.store(true, Ordering::Release);
    }

    pub fn is_cancelled(&self) -> bool {
        self.cancelled.load(Ordering::Acquire)
    }
}

pub struct BackupRunner<T> {
    tool: T,
}

impl<T: LogicalBackupTool> BackupRunner<T> {
    pub fn new(tool: T) -> Self {
        Self { tool }
    }

    pub fn run(
        &self,
        repository: &BackupRepository,
        plan: &BackupPlan,
    ) -> Result<RecoverySetManifest, BackupError> {
        plan.validate()?;
        if repository.deployment_id() != plan.deployment_id {
            return Err(BackupError::InvalidPlan);
        }
        let recovery_set_id = Uuid::new_v4();
        let created_at = now_unix()?;
        let staged = repository.begin_set(recovery_set_id)?;
        let mut members = Vec::with_capacity(3);
        for target in &plan.targets {
            match target {
                BackupTarget::Required { database } => {
                    let started_at = now_unix()?;
                    let archive = staged.prepare_archive(database.service)?;
                    self.tool.dump(database, &archive)?;
                    self.tool.verify(&archive)?;
                    let completed_at = now_unix()?;
                    members.push(BackupMember {
                        service: database.service,
                        member: BackupMemberState::Required {
                            database: database.database.clone(),
                            schema_version: database.schema_version,
                            archive_file: archive
                                .file_name()
                                .and_then(|name| name.to_str())
                                .ok_or(BackupError::Repository)?
                                .to_string(),
                            archive_sha256: hash_file(&archive, BackupError::VerificationFailed)?,
                            started_at_unix: started_at,
                            completed_at_unix: completed_at,
                        },
                    });
                }
                BackupTarget::NotApplicable { service, reason } => {
                    members.push(BackupMember {
                        service: *service,
                        member: BackupMemberState::NotApplicable {
                            reason: reason.clone(),
                        },
                    });
                }
            }
        }
        let manifest = RecoverySetManifest {
            schema_version: MANIFEST_SCHEMA_VERSION,
            recovery_set_id,
            deployment_id: plan.deployment_id,
            kind: plan.kind,
            status: RecoverySetStatus::Verified,
            created_at_unix: created_at,
            completed_at_unix: Some(now_unix()?),
            locked: false,
            restoring: false,
            retention: plan.retention.clone(),
            previous_recovery_set_id: plan.previous_recovery_set_id,
            members,
            failure_code: None,
        };
        staged.publish(&manifest)?;
        Ok(manifest)
    }
}

#[derive(Debug, Clone)]
pub struct PinnedPgTools {
    pg_dump: PathBuf,
    pg_dump_sha256: String,
    pg_restore: PathBuf,
    pg_restore_sha256: String,
    command_timeout: Duration,
    cancellation: BackupCancellation,
}

impl PinnedPgTools {
    pub fn new(
        pg_dump: PathBuf,
        pg_dump_sha256: String,
        pg_restore: PathBuf,
        pg_restore_sha256: String,
        command_timeout: Duration,
        cancellation: BackupCancellation,
    ) -> Result<Self, BackupError> {
        if !valid_tool_path(&pg_dump, "pg_dump")
            || !valid_tool_path(&pg_restore, "pg_restore")
            || !valid_sha256(&pg_dump_sha256)
            || !valid_sha256(&pg_restore_sha256)
            || command_timeout < Duration::from_secs(1)
            || command_timeout > Duration::from_secs(24 * 60 * 60)
        {
            return Err(BackupError::ToolIdentity);
        }
        let tools = Self {
            pg_dump,
            pg_dump_sha256,
            pg_restore,
            pg_restore_sha256,
            command_timeout,
            cancellation,
        };
        tools.verify_tools()?;
        Ok(tools)
    }

    fn verify_tools(&self) -> Result<(), BackupError> {
        if hash_file(&self.pg_dump, BackupError::ToolIdentity)? != self.pg_dump_sha256
            || hash_file(&self.pg_restore, BackupError::ToolIdentity)? != self.pg_restore_sha256
        {
            return Err(BackupError::ToolIdentity);
        }
        Ok(())
    }
}

impl LogicalBackupTool for PinnedPgTools {
    fn dump(&self, target: &DatabaseTarget, destination: &Path) -> Result<(), BackupError> {
        target.validate()?;
        self.verify_tools()?;
        drop(
            px_private_files::private::read_private(&target.password_file)
                .map_err(|_| BackupError::Credential)?,
        );
        let mut command = Command::new(&self.pg_dump);
        command
            .args(["--format=custom", "--no-password"])
            .arg("--file")
            .arg(destination)
            .arg("--host")
            .arg(&target.host)
            .arg("--port")
            .arg(target.port.to_string())
            .arg("--username")
            .arg(&target.username)
            .arg("--dbname")
            .arg(&target.database)
            .env_remove("PGPASSWORD")
            .env_remove("DATABASE_URL")
            .env("PGPASSFILE", &target.password_file)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null());
        run_command(
            &mut command,
            self.command_timeout,
            &self.cancellation,
            BackupError::ArchiveFailed,
        )
    }

    fn verify(&self, archive: &Path) -> Result<(), BackupError> {
        self.verify_tools()?;
        let mut command = Command::new(&self.pg_restore);
        command
            .arg("--list")
            .arg(archive)
            .env_remove("PGPASSWORD")
            .env_remove("DATABASE_URL")
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null());
        run_command(
            &mut command,
            self.command_timeout,
            &self.cancellation,
            BackupError::VerificationFailed,
        )
    }
}

fn run_command(
    command: &mut Command,
    timeout: Duration,
    cancellation: &BackupCancellation,
    failure: BackupError,
) -> Result<(), BackupError> {
    if cancellation.is_cancelled() {
        return Err(BackupError::Cancelled);
    }
    let mut child = command.spawn().map_err(|_| failure.clone())?;
    let started = Instant::now();
    loop {
        if cancellation.is_cancelled() {
            stop_child(&mut child);
            return Err(BackupError::Cancelled);
        }
        if started.elapsed() >= timeout {
            stop_child(&mut child);
            return Err(BackupError::ToolTimeout);
        }
        match child.try_wait() {
            Ok(Some(status)) if status.success() => return Ok(()),
            Ok(Some(_)) => return Err(failure),
            Ok(None) => thread::sleep(Duration::from_millis(100)),
            Err(_) => {
                stop_child(&mut child);
                return Err(failure);
            }
        }
    }
}

fn stop_child(child: &mut Child) {
    let _ = child.kill();
    let _ = child.wait();
}

fn now_unix() -> Result<u64, BackupError> {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .map_err(|_| BackupError::Clock)
}

fn hash_file(path: &Path, failure: BackupError) -> Result<String, BackupError> {
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW);
    }
    let mut file = options.open(path).map_err(|_| failure.clone())?;
    if !file.metadata().map_err(|_| failure.clone())?.is_file() {
        return Err(failure);
    }
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = file.read(&mut buffer).map_err(|_| failure.clone())?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

fn valid_host(value: &str) -> bool {
    value.parse::<IpAddr>().is_ok()
        || (!value.is_empty()
            && value.len() <= 253
            && value
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-'))
            && !value.starts_with('-')
            && !value.ends_with('-'))
}

fn valid_identifier(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 63
        && value
            .bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
}

fn valid_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn valid_tool_path(path: &Path, expected_stem: &str) -> bool {
    path.is_absolute()
        && path
            .file_stem()
            .and_then(|value| value.to_str())
            .is_some_and(|value| value.eq_ignore_ascii_case(expected_stem))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        fs,
        sync::atomic::{AtomicBool, Ordering},
    };

    struct Fixture {
        _base: tempfile::TempDir,
        root: PathBuf,
        deployment_id: Uuid,
    }

    impl Fixture {
        fn new() -> Self {
            let base = tempfile::Builder::new()
                .prefix("pixels-backup-runner-")
                .tempdir()
                .unwrap();
            make_private(base.path());
            let root = base.path().join("sets");
            fs::create_dir(&root).unwrap();
            make_private(&root);
            Self {
                _base: base,
                root,
                deployment_id: Uuid::new_v4(),
            }
        }
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
            let identity = Command::new("whoami")
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            let grant = format!(
                "{}:(OI)(CI)F",
                String::from_utf8(identity.stdout).unwrap().trim()
            );
            let result = Command::new("icacls")
                .arg(path)
                .args(["/inheritance:r", "/grant:r", &grant, "*S-1-5-18:(OI)(CI)F"])
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(result.status.success());
        }
    }

    fn target(fixture: &Fixture, service: BackupService) -> BackupTarget {
        BackupTarget::Required {
            database: DatabaseTarget {
                service,
                host: "127.0.0.1".into(),
                port: 5432,
                database: format!("pixels_{service:?}").to_ascii_lowercase(),
                username: "pixels_backup".into(),
                password_file: fixture.root.join("credentials.pgpass"),
                schema_version: 1,
            },
        }
    }

    fn plan(fixture: &Fixture) -> BackupPlan {
        BackupPlan {
            deployment_id: fixture.deployment_id,
            kind: RecoverySetKind::Independent,
            retention: BTreeSet::from([RetentionClass::Hourly]),
            previous_recovery_set_id: None,
            targets: vec![
                target(fixture, BackupService::Console),
                target(fixture, BackupService::Auth),
                BackupTarget::NotApplicable {
                    service: BackupService::Desk,
                    reason: "component not installed".into(),
                },
            ],
        }
    }

    struct FakeTool {
        fail_verify: AtomicBool,
    }

    impl LogicalBackupTool for FakeTool {
        fn dump(&self, target: &DatabaseTarget, destination: &Path) -> Result<(), BackupError> {
            fs::write(destination, format!("archive:{:?}", target.service))
                .map_err(|_| BackupError::ArchiveFailed)
        }

        fn verify(&self, _archive: &Path) -> Result<(), BackupError> {
            if self.fail_verify.load(Ordering::Relaxed) {
                Err(BackupError::VerificationFailed)
            } else {
                Ok(())
            }
        }
    }

    #[test]
    fn runner_publishes_only_verified_exact_members_and_preserves_explicit_na() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        let runner = BackupRunner::new(FakeTool {
            fail_verify: AtomicBool::new(false),
        });
        let manifest = runner.run(&repository, &plan(&fixture)).unwrap();
        assert_eq!(manifest.kind, RecoverySetKind::Independent);
        assert_eq!(manifest.status, RecoverySetStatus::Verified);
        assert!(matches!(
            manifest.members[2].member,
            BackupMemberState::NotApplicable { .. }
        ));
        assert_eq!(repository.manifests().unwrap(), vec![manifest]);
    }

    #[test]
    fn runner_failure_leaves_reconciliation_marker_and_never_publishes() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        let runner = BackupRunner::new(FakeTool {
            fail_verify: AtomicBool::new(true),
        });
        assert_eq!(
            runner.run(&repository, &plan(&fixture)),
            Err(BackupError::VerificationFailed)
        );
        assert_eq!(repository.manifests(), Err(RepositoryError::Corrupt));
        assert!(fs::read_dir(&fixture.root)
            .unwrap()
            .filter_map(Result::ok)
            .any(|entry| entry.file_name().to_string_lossy().starts_with(".partial-")));
    }

    #[test]
    fn plan_never_claims_a_barrier_or_accepts_duplicate_services_and_console_na() {
        let fixture = Fixture::new();
        let mut value = plan(&fixture);
        value.kind = RecoverySetKind::WriteBarrier;
        assert_eq!(value.validate(), Err(BackupError::BarrierRequired));
        value.kind = RecoverySetKind::Independent;
        value.targets[2] = target(&fixture, BackupService::Auth);
        assert_eq!(value.validate(), Err(BackupError::InvalidPlan));
        value.targets[0] = BackupTarget::NotApplicable {
            service: BackupService::Console,
            reason: "not installed".into(),
        };
        assert_eq!(value.validate(), Err(BackupError::InvalidPlan));
    }

    #[test]
    fn command_wait_is_bounded_and_cancellation_is_checked_before_spawn() {
        #[cfg(windows)]
        let mut sleeper = {
            let mut command = Command::new("powershell.exe");
            command.args(["-NoProfile", "-Command", "Start-Sleep -Seconds 5"]);
            command
        };
        #[cfg(unix)]
        let mut sleeper = {
            let mut command = Command::new("sleep");
            command.arg("5");
            command
        };
        let started = Instant::now();
        assert_eq!(
            run_command(
                &mut sleeper,
                Duration::from_millis(100),
                &BackupCancellation::default(),
                BackupError::ArchiveFailed,
            ),
            Err(BackupError::ToolTimeout)
        );
        assert!(started.elapsed() < Duration::from_secs(2));

        let cancellation = BackupCancellation::default();
        cancellation.cancel();
        let mut missing = Command::new("pixels-command-that-does-not-exist");
        assert_eq!(
            run_command(
                &mut missing,
                Duration::from_secs(1),
                &cancellation,
                BackupError::ArchiveFailed,
            ),
            Err(BackupError::Cancelled)
        );
    }
}
