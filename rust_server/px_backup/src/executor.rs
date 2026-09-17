use crate::{
    BackupMember, BackupMemberState, BackupRepository, BackupService,
    RecoveryEvidenceUnavailableReason, RecoverySecurityEvidence, RecoverySetKind,
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
    #[error("coordinated write-barrier proof is invalid")]
    BarrierInvalid,
    #[error("coordinated write-barrier lease is not active")]
    BarrierInactive,
    #[error("coordinated write-barrier proof changed during backup")]
    BarrierChanged,
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
    pub write_barrier_proof_file: Option<PathBuf>,
    pub retention: BTreeSet<RetentionClass>,
    pub previous_recovery_set_id: Option<Uuid>,
    pub targets: Vec<BackupTarget>,
}

impl BackupPlan {
    pub(crate) fn validate(&self) -> Result<(), BackupError> {
        if self.deployment_id.is_nil() || self.retention.is_empty() {
            return Err(BackupError::InvalidPlan);
        }
        match self.kind {
            RecoverySetKind::Independent if self.write_barrier_proof_file.is_none() => {}
            RecoverySetKind::WriteBarrier
                if self
                    .write_barrier_proof_file
                    .as_ref()
                    .is_some_and(|path| path.is_absolute()) => {}
            RecoverySetKind::WriteBarrier => return Err(BackupError::BarrierRequired),
            RecoverySetKind::Independent | RecoverySetKind::Physical => {
                return Err(BackupError::InvalidPlan);
            }
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

pub const WRITE_BARRIER_PROOF_SCHEMA_VERSION: u32 = 1;
const MAX_WRITE_BARRIER_PROOF_BYTES: usize = 4 * 1024;
const MAX_WRITE_BARRIER_SECONDS: u64 = 60 * 60;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WriteBarrierServiceAttestation {
    pub service: BackupService,
    pub drained_at_unix: u64,
    pub lease_expires_at_unix: u64,
    pub write_gate_token_sha256: String,
    pub security_sequence: u64,
    pub security_state_sha256: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WriteBarrierProof {
    pub schema_version: u32,
    pub deployment_id: Uuid,
    pub consistency_proof_id: Uuid,
    pub acquired_at_unix: u64,
    pub expires_at_unix: u64,
    pub external_key_ids: BTreeSet<String>,
    pub attestations: Vec<WriteBarrierServiceAttestation>,
}

impl WriteBarrierProof {
    fn load_private(path: &Path) -> Result<(Self, String), BackupError> {
        let proof_bytes = px_private_files::private::read_private(path)
            .map_err(|_| BackupError::BarrierInvalid)?;
        if proof_bytes.is_empty() || proof_bytes.len() > MAX_WRITE_BARRIER_PROOF_BYTES {
            return Err(BackupError::BarrierInvalid);
        }
        let proof = serde_json::from_slice::<Self>(&proof_bytes)
            .map_err(|_| BackupError::BarrierInvalid)?;
        let proof_sha256 = format!("{:x}", Sha256::digest(&proof_bytes));
        Ok((proof, proof_sha256))
    }

    fn validate_for(&self, plan: &BackupPlan, current_time: u64) -> Result<(), BackupError> {
        let required_services = plan
            .targets
            .iter()
            .filter_map(|target| match target {
                BackupTarget::Required { database } => Some(database.service),
                BackupTarget::NotApplicable { .. } => None,
            })
            .collect::<BTreeSet<_>>();
        let attested_services = self
            .attestations
            .iter()
            .map(|attestation| attestation.service)
            .collect::<BTreeSet<_>>();
        if self.schema_version != WRITE_BARRIER_PROOF_SCHEMA_VERSION
            || self.deployment_id != plan.deployment_id
            || self.consistency_proof_id.is_nil()
            || self.acquired_at_unix == 0
            || self.expires_at_unix <= self.acquired_at_unix
            || self.expires_at_unix - self.acquired_at_unix > MAX_WRITE_BARRIER_SECONDS
            || self.external_key_ids.len() > 128
            || self
                .external_key_ids
                .iter()
                .any(|key_id| !valid_sha256(key_id))
            || self.attestations.len() != required_services.len()
            || attested_services != required_services
            || self.attestations.iter().any(|attestation| {
                attestation.drained_at_unix < self.acquired_at_unix
                    || attestation.drained_at_unix >= self.expires_at_unix
                    || attestation.lease_expires_at_unix != self.expires_at_unix
                    || attestation.security_sequence == 0
                    || !valid_sha256(&attestation.write_gate_token_sha256)
                    || !valid_sha256(&attestation.security_state_sha256)
            })
        {
            return Err(BackupError::BarrierInvalid);
        }
        if current_time < self.acquired_at_unix || current_time >= self.expires_at_unix {
            return Err(BackupError::BarrierInactive);
        }
        Ok(())
    }

    fn security_evidence(&self) -> RecoverySecurityEvidence {
        RecoverySecurityEvidence::Captured {
            consistency_proof_id: self.consistency_proof_id,
            external_key_ids: self.external_key_ids.clone(),
            watermarks: self
                .attestations
                .iter()
                .map(|attestation| crate::ServiceSecurityWatermark {
                    service: attestation.service,
                    security_sequence: attestation.security_sequence,
                    security_state_sha256: attestation.security_state_sha256.clone(),
                })
                .collect(),
        }
    }
}

struct ActiveWriteBarrier {
    path: PathBuf,
    proof: WriteBarrierProof,
    proof_sha256: String,
}

impl ActiveWriteBarrier {
    fn acquire(plan: &BackupPlan) -> Result<Option<Self>, BackupError> {
        if plan.kind == RecoverySetKind::Independent {
            return Ok(None);
        }
        let proof_path = plan
            .write_barrier_proof_file
            .as_ref()
            .ok_or(BackupError::BarrierRequired)?;
        let (proof, proof_sha256) = WriteBarrierProof::load_private(proof_path)?;
        proof.validate_for(plan, now_unix()?)?;
        Ok(Some(Self {
            path: proof_path.clone(),
            proof,
            proof_sha256,
        }))
    }

    fn verify_active(&self, plan: &BackupPlan) -> Result<(), BackupError> {
        let (current_proof, current_sha256) =
            WriteBarrierProof::load_private(&self.path).map_err(|_| BackupError::BarrierChanged)?;
        if current_sha256 != self.proof_sha256 || current_proof != self.proof {
            return Err(BackupError::BarrierChanged);
        }
        current_proof.validate_for(plan, now_unix()?)
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
        let active_write_barrier = ActiveWriteBarrier::acquire(plan)?;
        let recovery_set_id = Uuid::new_v4();
        let created_at = now_unix()?;
        let staged = repository.begin_set(recovery_set_id)?;
        let mut members = Vec::with_capacity(3);
        for target in &plan.targets {
            if let Some(write_barrier) = &active_write_barrier {
                write_barrier.verify_active(plan)?;
            }
            match target {
                BackupTarget::Required { database } => {
                    let started_at = now_unix()?;
                    let archive = staged.prepare_archive(database.service)?;
                    self.tool.dump(database, &archive)?;
                    self.tool.verify(&archive)?;
                    if let Some(write_barrier) = &active_write_barrier {
                        write_barrier.verify_active(plan)?;
                    }
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
        if let Some(write_barrier) = &active_write_barrier {
            write_barrier.verify_active(plan)?;
        }
        let security_evidence = active_write_barrier.map_or_else(
            || RecoverySecurityEvidence::Unavailable {
                reason: RecoveryEvidenceUnavailableReason::IndependentBackup,
            },
            |write_barrier| write_barrier.proof.security_evidence(),
        );
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
            security_evidence,
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

pub(crate) fn hash_file(path: &Path, failure: BackupError) -> Result<String, BackupError> {
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

pub(crate) fn valid_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

pub(crate) fn valid_tool_path(path: &Path, expected_stem: &str) -> bool {
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
            write_barrier_proof_file: None,
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
    fn runner_failure_leaves_discardable_reconciliation_marker_and_never_publishes() {
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
        assert_eq!(repository.discard_incomplete_sets().unwrap().len(), 1);
        assert!(repository.manifests().unwrap().is_empty());
    }

    #[test]
    fn plan_requires_a_private_barrier_proof_and_rejects_duplicate_services_and_console_na() {
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

    fn write_barrier_proof(fixture: &Fixture, current_time: u64) -> WriteBarrierProof {
        WriteBarrierProof {
            schema_version: WRITE_BARRIER_PROOF_SCHEMA_VERSION,
            deployment_id: fixture.deployment_id,
            consistency_proof_id: Uuid::new_v4(),
            acquired_at_unix: current_time - 1,
            expires_at_unix: current_time + 60,
            external_key_ids: BTreeSet::from(["d".repeat(64)]),
            attestations: [BackupService::Console, BackupService::Auth]
                .into_iter()
                .map(|service| WriteBarrierServiceAttestation {
                    service,
                    drained_at_unix: current_time,
                    lease_expires_at_unix: current_time + 60,
                    write_gate_token_sha256: "e".repeat(64),
                    security_sequence: 31,
                    security_state_sha256: "f".repeat(64),
                })
                .collect(),
        }
    }

    #[test]
    fn coordinated_backup_rechecks_private_live_proof_and_publishes_security_evidence() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        let proof_path = fixture._base.path().join("write-barrier-proof.json");
        let proof = write_barrier_proof(&fixture, now_unix().unwrap());
        px_private_files::private::create_private(
            &proof_path,
            &serde_json::to_vec(&proof).unwrap(),
        )
        .unwrap();
        let mut coordinated_plan = plan(&fixture);
        coordinated_plan.kind = RecoverySetKind::WriteBarrier;
        coordinated_plan.write_barrier_proof_file = Some(proof_path);
        let manifest = BackupRunner::new(FakeTool {
            fail_verify: AtomicBool::new(false),
        })
        .run(&repository, &coordinated_plan)
        .unwrap();
        assert_eq!(manifest.kind, RecoverySetKind::WriteBarrier);
        assert_eq!(manifest.security_evidence, proof.security_evidence());
    }

    struct BarrierChangingTool {
        proof_path: PathBuf,
        changed: AtomicBool,
    }

    impl LogicalBackupTool for BarrierChangingTool {
        fn dump(&self, target: &DatabaseTarget, destination: &Path) -> Result<(), BackupError> {
            fs::write(destination, format!("archive:{:?}", target.service))
                .map_err(|_| BackupError::ArchiveFailed)?;
            if !self.changed.swap(true, Ordering::Relaxed) {
                fs::write(&self.proof_path, b"released").map_err(|_| BackupError::ArchiveFailed)?;
            }
            Ok(())
        }

        fn verify(&self, _archive: &Path) -> Result<(), BackupError> {
            Ok(())
        }
    }

    #[test]
    fn coordinated_backup_fails_when_barrier_is_released_during_export() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        let proof_path = fixture._base.path().join("write-barrier-proof.json");
        let proof = write_barrier_proof(&fixture, now_unix().unwrap());
        px_private_files::private::create_private(
            &proof_path,
            &serde_json::to_vec(&proof).unwrap(),
        )
        .unwrap();
        let mut coordinated_plan = plan(&fixture);
        coordinated_plan.kind = RecoverySetKind::WriteBarrier;
        coordinated_plan.write_barrier_proof_file = Some(proof_path.clone());
        let result = BackupRunner::new(BarrierChangingTool {
            proof_path,
            changed: AtomicBool::new(false),
        })
        .run(&repository, &coordinated_plan);
        assert_eq!(result, Err(BackupError::BarrierChanged));
        assert_eq!(repository.manifests(), Err(RepositoryError::Corrupt));
        assert_eq!(repository.discard_incomplete_sets().unwrap().len(), 1);
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
