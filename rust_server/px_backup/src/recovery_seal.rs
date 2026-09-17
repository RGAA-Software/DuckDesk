use crate::{
    executor::{hash_file, valid_sha256, valid_tool_path},
    BackupCancellation, BackupError, BackupService, RecoverySecurityEvidence, RecoverySetManifest,
    RestoreDatabaseTarget, RestoreExecutionPlan,
};
use fs2::FileExt;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeSet,
    env, fs,
    fs::{File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use uuid::Uuid;

pub const RECOVERY_SEAL_REPORT_SCHEMA_VERSION: u32 = 2;
const RECOVERY_SEAL_MARKER_SCHEMA_VERSION: u32 = 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum RecoverySealError {
    #[error("recovery-seal plan is invalid")]
    InvalidPlan,
    #[error("recovery-seal source evidence is invalid")]
    InvalidEvidence,
    #[error("recovery-seal state requires reconciliation")]
    ReconciliationRequired,
    #[error("pinned PostgreSQL recovery-seal tool is unavailable or changed")]
    ToolIdentity,
    #[error("recovery-seal database credential is unavailable")]
    Credential,
    #[error("recovery-seal database operation failed closed")]
    Database,
    #[error("recovery-seal command timed out")]
    ToolTimeout,
    #[error("recovery-seal operation was cancelled")]
    Cancelled,
    #[error("system clock is unavailable")]
    Clock,
    #[error("private recovery-seal evidence could not be persisted")]
    PrivateState,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RecoverySealPlan {
    pub deployment_id: Uuid,
    pub recovery_set_id: Uuid,
    pub target_environment_id: Uuid,
    pub lock_file: PathBuf,
    pub marker_file: PathBuf,
    pub report_file: PathBuf,
    pub targets: Vec<RestoreDatabaseTarget>,
}

impl RecoverySealPlan {
    pub fn validate(&self) -> Result<(), RecoverySealError> {
        if !self.lock_file.is_absolute()
            || !self.marker_file.is_absolute()
            || !self.report_file.is_absolute()
            || self.lock_file == self.marker_file
            || self.lock_file == self.report_file
            || self.marker_file == self.report_file
        {
            return Err(RecoverySealError::InvalidPlan);
        }
        RestoreExecutionPlan {
            deployment_id: self.deployment_id,
            recovery_set_id: self.recovery_set_id,
            target_environment_id: self.target_environment_id,
            targets: self.targets.clone(),
        }
        .validate()
        .map_err(|_| RecoverySealError::InvalidPlan)?;
        if self.targets.is_empty() || self.targets.len() > 3 {
            return Err(RecoverySealError::InvalidPlan);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct RecoverySealMarker {
    schema_version: u32,
    deployment_id: Uuid,
    recovery_set_id: Uuid,
    target_environment_id: Uuid,
    recovery_generation: Uuid,
    started_at_unix: u64,
    source_security_evidence_sha256: String,
    targets: Vec<RestoreDatabaseTarget>,
}

impl RecoverySealMarker {
    fn new(
        plan: &RecoverySealPlan,
        recovery_generation: Uuid,
        started_at_unix: u64,
        source_security_evidence_sha256: String,
    ) -> Self {
        Self {
            schema_version: RECOVERY_SEAL_MARKER_SCHEMA_VERSION,
            deployment_id: plan.deployment_id,
            recovery_set_id: plan.recovery_set_id,
            target_environment_id: plan.target_environment_id,
            recovery_generation,
            started_at_unix,
            source_security_evidence_sha256,
            targets: plan.targets.clone(),
        }
    }

    fn matches_plan(&self, plan: &RecoverySealPlan, evidence_sha256: &str) -> bool {
        self.schema_version == RECOVERY_SEAL_MARKER_SCHEMA_VERSION
            && self.deployment_id == plan.deployment_id
            && self.recovery_set_id == plan.recovery_set_id
            && self.target_environment_id == plan.target_environment_id
            && !self.recovery_generation.is_nil()
            && self.started_at_unix > 0
            && self.source_security_evidence_sha256 == evidence_sha256
            && self.targets == plan.targets
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RecoverySealServiceReport {
    pub service: BackupService,
    pub source_recovery_generation: Uuid,
    pub source_security_sequence: u64,
    pub source_security_state_sha256: String,
    pub sealed_security_sequence: u64,
    pub sealed_security_state_sha256: String,
    pub revoked_session_records: u64,
    pub invalidated_grant_records: u64,
    pub invalidated_control_records: u64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RecoverySealReport {
    pub schema_version: u32,
    pub deployment_id: Uuid,
    pub recovery_set_id: Uuid,
    pub target_environment_id: Uuid,
    pub recovery_generation: Uuid,
    pub started_at_unix: u64,
    pub completed_at_unix: u64,
    pub source_security_evidence_sha256: String,
    pub services: Vec<RecoverySealServiceReport>,
    pub old_sessions_revoked: bool,
    pub old_grants_invalidated: bool,
    pub pending_control_invalidated: bool,
    pub admission_required: bool,
}

impl RecoverySealReport {
    pub fn load_private(path: &Path) -> Result<Self, RecoverySealError> {
        if !path.is_absolute() {
            return Err(RecoverySealError::PrivateState);
        }
        let report_bytes = px_private_files::private::read_private(path)
            .map_err(|_| RecoverySealError::PrivateState)?;
        if report_bytes.is_empty() || report_bytes.len() > 256 * 1024 {
            return Err(RecoverySealError::PrivateState);
        }
        serde_json::from_slice(&report_bytes).map_err(|_| RecoverySealError::PrivateState)
    }

    pub fn validate_for_restore(
        &self,
        manifest: &RecoverySetManifest,
        target_environment_id: Uuid,
    ) -> Result<(), RecoverySealError> {
        manifest
            .validate()
            .map_err(|_| RecoverySealError::InvalidEvidence)?;
        let source_evidence_sha256 = security_evidence_sha256(manifest)?;
        let source_watermarks = match &manifest.security_evidence {
            RecoverySecurityEvidence::Captured { watermarks, .. } => watermarks,
            RecoverySecurityEvidence::Unavailable { .. } => {
                return Err(RecoverySealError::InvalidEvidence);
            }
        };
        let report_services = self
            .services
            .iter()
            .map(|service| service.service)
            .collect::<BTreeSet<_>>();
        let source_services = source_watermarks
            .iter()
            .map(|watermark| watermark.service)
            .collect::<BTreeSet<_>>();
        if self.schema_version != RECOVERY_SEAL_REPORT_SCHEMA_VERSION
            || self.deployment_id != manifest.deployment_id
            || self.recovery_set_id != manifest.recovery_set_id
            || self.target_environment_id != target_environment_id
            || self.recovery_generation.is_nil()
            || self.started_at_unix == 0
            || self.completed_at_unix < self.started_at_unix
            || self.source_security_evidence_sha256 != source_evidence_sha256
            || report_services != source_services
            || report_services.len() != self.services.len()
            || !self.old_sessions_revoked
            || !self.old_grants_invalidated
            || !self.pending_control_invalidated
            || !self.admission_required
        {
            return Err(RecoverySealError::InvalidEvidence);
        }
        for source_watermark in source_watermarks {
            let service_report = self
                .services
                .iter()
                .find(|service| service.service == source_watermark.service)
                .ok_or(RecoverySealError::InvalidEvidence)?;
            let expected_sealed_sha256 = security_state_sha256(
                self.deployment_id,
                service_report.service,
                self.recovery_generation,
                service_report.sealed_security_sequence,
            );
            if service_report.source_security_sequence != source_watermark.security_sequence
                || service_report.source_recovery_generation != source_watermark.recovery_generation
                || service_report.source_security_state_sha256
                    != source_watermark.security_state_sha256
                || service_report.sealed_security_sequence
                    <= service_report.source_security_sequence
                || service_report.sealed_security_state_sha256 != expected_sealed_sha256
            {
                return Err(RecoverySealError::InvalidEvidence);
            }
        }
        Ok(())
    }

    fn matches(
        &self,
        plan: &RecoverySealPlan,
        evidence_sha256: &str,
    ) -> Result<(), RecoverySealError> {
        let expected_services = plan
            .targets
            .iter()
            .map(|target| target.service)
            .collect::<BTreeSet<_>>();
        let report_services = self
            .services
            .iter()
            .map(|service| service.service)
            .collect::<BTreeSet<_>>();
        if self.schema_version != RECOVERY_SEAL_REPORT_SCHEMA_VERSION
            || self.deployment_id != plan.deployment_id
            || self.recovery_set_id != plan.recovery_set_id
            || self.target_environment_id != plan.target_environment_id
            || self.recovery_generation.is_nil()
            || self.started_at_unix == 0
            || self.completed_at_unix < self.started_at_unix
            || self.source_security_evidence_sha256 != evidence_sha256
            || report_services != expected_services
            || report_services.len() != self.services.len()
            || self.services.iter().any(|service| {
                service.source_recovery_generation.is_nil()
                    || service.source_security_sequence == 0
                    || service.sealed_security_sequence <= service.source_security_sequence
                    || !valid_sha256(&service.source_security_state_sha256)
                    || !valid_sha256(&service.sealed_security_state_sha256)
            })
            || !self.old_sessions_revoked
            || !self.old_grants_invalidated
            || !self.pending_control_invalidated
            || !self.admission_required
        {
            return Err(RecoverySealError::ReconciliationRequired);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecoverySealTargetResult {
    pub source_security_sequence: u64,
    pub source_security_state_sha256: String,
    pub sealed_security_sequence: u64,
    pub sealed_security_state_sha256: String,
    pub revoked_session_records: u64,
    pub invalidated_grant_records: u64,
    pub invalidated_control_records: u64,
}

pub trait RecoverySealTool {
    fn seal_target(
        &self,
        target: &RestoreDatabaseTarget,
        deployment_id: Uuid,
        expected_source_generation: Uuid,
        expected_source_sequence: u64,
        expected_source_state_sha256: &str,
        recovery_generation: Uuid,
    ) -> Result<RecoverySealTargetResult, RecoverySealError>;
}

pub struct RecoverySealRunner<T> {
    tool: T,
}

impl<T: RecoverySealTool> RecoverySealRunner<T> {
    pub fn new(tool: T) -> Self {
        Self { tool }
    }

    pub fn seal(
        &self,
        manifest: &RecoverySetManifest,
        plan: &RecoverySealPlan,
    ) -> Result<RecoverySealReport, RecoverySealError> {
        plan.validate()?;
        manifest
            .validate()
            .map_err(|_| RecoverySealError::InvalidEvidence)?;
        if manifest.deployment_id != plan.deployment_id
            || manifest.recovery_set_id != plan.recovery_set_id
            || !manifest.status.is_verified()
        {
            return Err(RecoverySealError::InvalidEvidence);
        }
        let source_watermarks = match &manifest.security_evidence {
            RecoverySecurityEvidence::Captured { watermarks, .. } => watermarks,
            RecoverySecurityEvidence::Unavailable { .. } => {
                return Err(RecoverySealError::InvalidEvidence);
            }
        };
        let expected_services = plan
            .targets
            .iter()
            .map(|target| target.service)
            .collect::<BTreeSet<_>>();
        let evidence_services = source_watermarks
            .iter()
            .map(|watermark| watermark.service)
            .collect::<BTreeSet<_>>();
        if evidence_services != expected_services {
            return Err(RecoverySealError::InvalidEvidence);
        }
        let source_security_evidence_sha256 = security_evidence_sha256(manifest)?;
        let _operation_lock = RecoverySealOperationLock::acquire(&plan.lock_file)?;
        if plan.report_file.exists() {
            if plan.marker_file.exists() {
                return Err(RecoverySealError::ReconciliationRequired);
            }
            let report = load_private::<RecoverySealReport>(&plan.report_file)?;
            report.matches(plan, &source_security_evidence_sha256)?;
            return Ok(report);
        }
        let marker = if plan.marker_file.exists() {
            let marker = load_private::<RecoverySealMarker>(&plan.marker_file)?;
            if !marker.matches_plan(plan, &source_security_evidence_sha256) {
                return Err(RecoverySealError::ReconciliationRequired);
            }
            marker
        } else {
            let marker = RecoverySealMarker::new(
                plan,
                Uuid::new_v4(),
                current_unix_time()?,
                source_security_evidence_sha256.clone(),
            );
            persist_private(&plan.marker_file, &marker)?;
            marker
        };
        let mut services = Vec::with_capacity(plan.targets.len());
        for target in &plan.targets {
            let source_watermark = source_watermarks
                .iter()
                .find(|watermark| watermark.service == target.service)
                .ok_or(RecoverySealError::InvalidEvidence)?;
            let result = self.tool.seal_target(
                target,
                plan.deployment_id,
                source_watermark.recovery_generation,
                source_watermark.security_sequence,
                &source_watermark.security_state_sha256,
                marker.recovery_generation,
            )?;
            if result.source_security_sequence != source_watermark.security_sequence
                || result.source_security_state_sha256 != source_watermark.security_state_sha256
                || result.sealed_security_sequence <= result.source_security_sequence
            {
                return Err(RecoverySealError::Database);
            }
            services.push(RecoverySealServiceReport {
                service: target.service,
                source_recovery_generation: source_watermark.recovery_generation,
                source_security_sequence: result.source_security_sequence,
                source_security_state_sha256: result.source_security_state_sha256,
                sealed_security_sequence: result.sealed_security_sequence,
                sealed_security_state_sha256: result.sealed_security_state_sha256,
                revoked_session_records: result.revoked_session_records,
                invalidated_grant_records: result.invalidated_grant_records,
                invalidated_control_records: result.invalidated_control_records,
            });
        }
        let report = RecoverySealReport {
            schema_version: RECOVERY_SEAL_REPORT_SCHEMA_VERSION,
            deployment_id: plan.deployment_id,
            recovery_set_id: plan.recovery_set_id,
            target_environment_id: plan.target_environment_id,
            recovery_generation: marker.recovery_generation,
            started_at_unix: marker.started_at_unix,
            completed_at_unix: current_unix_time()?,
            source_security_evidence_sha256,
            services,
            old_sessions_revoked: true,
            old_grants_invalidated: true,
            pending_control_invalidated: true,
            admission_required: true,
        };
        report.matches(plan, &report.source_security_evidence_sha256)?;
        persist_private(&plan.report_file, &report)?;
        fs::remove_file(&plan.marker_file).map_err(|_| RecoverySealError::PrivateState)?;
        Ok(report)
    }
}

struct RecoverySealOperationLock {
    file: File,
}

impl RecoverySealOperationLock {
    fn acquire(path: &Path) -> Result<Self, RecoverySealError> {
        const CONTENTS: &[u8] = b"pixels-recovery-seal-lock\n";
        if !path.exists() {
            match px_private_files::private::create_private(path, CONTENTS) {
                Ok(()) => {}
                Err(_) if path.exists() => {}
                Err(_) => return Err(RecoverySealError::PrivateState),
            }
        }
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(path)
            .map_err(|_| RecoverySealError::PrivateState)?;
        file.try_lock_exclusive()
            .map_err(|_| RecoverySealError::ReconciliationRequired)?;
        let mut contents = Vec::new();
        file.read_to_end(&mut contents)
            .map_err(|_| RecoverySealError::PrivateState)?;
        if contents != CONTENTS {
            return Err(RecoverySealError::PrivateState);
        }
        Ok(Self { file })
    }
}

impl Drop for RecoverySealOperationLock {
    fn drop(&mut self) {
        let _ = FileExt::unlock(&self.file);
    }
}

#[derive(Debug, Clone)]
pub struct PinnedPgRecoverySealTool {
    psql: PathBuf,
    psql_sha256: String,
    command_timeout: Duration,
    cancellation: BackupCancellation,
}

impl PinnedPgRecoverySealTool {
    pub fn new(
        psql: PathBuf,
        psql_sha256: String,
        command_timeout: Duration,
        cancellation: BackupCancellation,
    ) -> Result<Self, RecoverySealError> {
        if !valid_tool_path(&psql, "psql")
            || !valid_sha256(&psql_sha256)
            || command_timeout < Duration::from_secs(1)
            || command_timeout > Duration::from_secs(24 * 60 * 60)
        {
            return Err(RecoverySealError::ToolIdentity);
        }
        let tool = Self {
            psql,
            psql_sha256,
            command_timeout,
            cancellation,
        };
        tool.verify_tool()?;
        Ok(tool)
    }

    fn verify_tool(&self) -> Result<(), RecoverySealError> {
        let actual_sha256 = hash_file(&self.psql, BackupError::ToolIdentity)
            .map_err(|_: BackupError| RecoverySealError::ToolIdentity)?;
        if actual_sha256 != self.psql_sha256 {
            return Err(RecoverySealError::ToolIdentity);
        }
        Ok(())
    }

    fn query_state(
        &self,
        target: &RestoreDatabaseTarget,
    ) -> Result<DatabaseSecurityState, RecoverySealError> {
        let [session_count, grant_count, control_count] = match target.service {
            BackupService::Console => [
                "(SELECT count(*) FROM pixels.login_sessions WHERE revoked_at IS NULL)+(SELECT count(*) FROM pixels.guest_sessions WHERE revoked_at IS NULL)",
                "(SELECT count(*) FROM pixels.user_devices)+(SELECT count(*) FROM pixels.group_device_grants)+(SELECT count(*) FROM pixels.group_app_grants)",
                "(SELECT count(*) FROM pixels.nodes WHERE deleted_at IS NULL)+(SELECT count(*) FROM pixels.instance_commands WHERE state IN ('pending','claimed'))+(SELECT count(*) FROM pixels.instances WHERE ended_at IS NULL)+(SELECT count(*) FROM pixels.resource_sessions WHERE closed_at IS NULL)",
            ],
            BackupService::Auth => [
                "(SELECT count(*) FROM pixels.author_sessions WHERE revoked_at IS NULL)",
                "(SELECT count(*) FROM pixels.licenses WHERE revoked_at IS NULL)",
                "(SELECT count(*) FROM pixels.license_requests WHERE issuance_id IS NULL)",
            ],
            BackupService::Desk => [
                "(SELECT count(*) FROM pixels.admin_sessions WHERE revoked_at IS NULL)",
                "0",
                "0",
            ],
        };
        let state_query = format!(
            "SELECT deployment.deployment_id::text || '|' || deployment.service || '|' || state.recovery_generation::text || '|' || state.security_sequence::text || '|' || ({session_count})::text || '|' || ({grant_count})::text || '|' || ({control_count})::text FROM pixels.deployment_identity AS deployment CROSS JOIN pixels.recovery_security_state AS state WHERE deployment.singleton AND state.singleton;\n",
        );
        let output = self.run_psql(target, &state_query)?;
        DatabaseSecurityState::parse(output.trim())
    }

    fn run_psql(
        &self,
        target: &RestoreDatabaseTarget,
        script: &str,
    ) -> Result<String, RecoverySealError> {
        self.verify_tool()?;
        if self.cancellation.is_cancelled() {
            return Err(RecoverySealError::Cancelled);
        }
        drop(
            px_private_files::private::read_private(&target.password_file)
                .map_err(|_| RecoverySealError::Credential)?,
        );
        let mut command = Command::new(&self.psql);
        sanitize_postgres_environment(&mut command);
        command
            .args([
                "-X",
                "--quiet",
                "--no-password",
                "--tuples-only",
                "--no-align",
            ])
            .arg("--host")
            .arg(&target.host)
            .arg("--port")
            .arg(target.port.to_string())
            .arg("--username")
            .arg(&target.username)
            .arg("--dbname")
            .arg(&target.database)
            .args(["--set=ON_ERROR_STOP=1", "--file=-"])
            .env("PGPASSFILE", &target.password_file)
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null());
        run_command_with_input(
            &mut command,
            script.as_bytes(),
            self.command_timeout,
            &self.cancellation,
        )
    }
}

impl RecoverySealTool for PinnedPgRecoverySealTool {
    fn seal_target(
        &self,
        target: &RestoreDatabaseTarget,
        deployment_id: Uuid,
        expected_source_generation: Uuid,
        expected_source_sequence: u64,
        expected_source_state_sha256: &str,
        recovery_generation: Uuid,
    ) -> Result<RecoverySealTargetResult, RecoverySealError> {
        self.verify_tool()?;
        if expected_source_generation.is_nil()
            || recovery_generation.is_nil()
            || expected_source_sequence == 0
            || !valid_sha256(expected_source_state_sha256)
        {
            return Err(RecoverySealError::InvalidEvidence);
        }
        let source_state = self.query_state(target)?;
        if source_state.deployment_id != deployment_id || source_state.service != target.service {
            return Err(RecoverySealError::Database);
        }
        let source_state_sha256 = source_state.sha256();
        if source_state.recovery_generation != recovery_generation
            && (source_state.recovery_generation != expected_source_generation
                || source_state_sha256 != expected_source_state_sha256)
        {
            return Err(RecoverySealError::InvalidEvidence);
        }
        let script = recovery_seal_script(
            target.service,
            &source_state,
            expected_source_sequence,
            recovery_generation,
        );
        let result_output = self.run_psql(target, &script)?;
        let safe_state = SafeDatabaseState::parse(result_output.trim())?;
        if safe_state.deployment_id != deployment_id
            || safe_state.service != target.service
            || safe_state.recovery_generation != recovery_generation
            || (source_state.recovery_generation != recovery_generation
                && safe_state.security_sequence <= source_state.security_sequence)
            || (source_state.recovery_generation == recovery_generation
                && safe_state.security_sequence != source_state.security_sequence)
            || !safe_state.all_invariants_hold()
        {
            return Err(RecoverySealError::Database);
        }
        let sealed_state_sha256 = security_state_sha256(
            safe_state.deployment_id,
            safe_state.service,
            safe_state.recovery_generation,
            safe_state.security_sequence,
        );
        Ok(RecoverySealTargetResult {
            source_security_sequence: if source_state.recovery_generation == recovery_generation {
                expected_source_sequence
            } else {
                source_state.security_sequence
            },
            source_security_state_sha256: expected_source_state_sha256.to_owned(),
            sealed_security_sequence: safe_state.security_sequence,
            sealed_security_state_sha256: sealed_state_sha256,
            revoked_session_records: source_state.active_session_records,
            invalidated_grant_records: source_state.active_grant_records,
            invalidated_control_records: source_state.active_control_records,
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct DatabaseSecurityState {
    deployment_id: Uuid,
    service: BackupService,
    recovery_generation: Uuid,
    security_sequence: u64,
    active_session_records: u64,
    active_grant_records: u64,
    active_control_records: u64,
}

impl DatabaseSecurityState {
    fn parse(value: &str) -> Result<Self, RecoverySealError> {
        let fields = value.split('|').collect::<Vec<_>>();
        if fields.len() != 7 {
            return Err(RecoverySealError::Database);
        }
        let state = Self {
            deployment_id: Uuid::parse_str(fields[0]).map_err(|_| RecoverySealError::Database)?,
            service: parse_service(fields[1])?,
            recovery_generation: Uuid::parse_str(fields[2])
                .map_err(|_| RecoverySealError::Database)?,
            security_sequence: fields[3]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
            active_session_records: fields[4]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
            active_grant_records: fields[5]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
            active_control_records: fields[6]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
        };
        if state.deployment_id.is_nil()
            || state.recovery_generation.is_nil()
            || state.security_sequence == 0
        {
            return Err(RecoverySealError::Database);
        }
        Ok(state)
    }

    fn sha256(&self) -> String {
        security_state_sha256(
            self.deployment_id,
            self.service,
            self.recovery_generation,
            self.security_sequence,
        )
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct SafeDatabaseState {
    deployment_id: Uuid,
    service: BackupService,
    recovery_generation: Uuid,
    original_security_sequence: u64,
    security_sequence: u64,
    revoked_session_records: u64,
    invalidated_grant_records: u64,
    invalidated_control_records: u64,
    unsafe_record_counts: Vec<u64>,
}

impl SafeDatabaseState {
    fn parse(value: &str) -> Result<Self, RecoverySealError> {
        let fields = value.split('|').collect::<Vec<_>>();
        if fields.len() < 9 {
            return Err(RecoverySealError::Database);
        }
        let unsafe_record_counts = fields[8..]
            .iter()
            .map(|field| {
                field
                    .parse::<u64>()
                    .map_err(|_| RecoverySealError::Database)
            })
            .collect::<Result<Vec<_>, _>>()?;
        Ok(Self {
            deployment_id: Uuid::parse_str(fields[0]).map_err(|_| RecoverySealError::Database)?,
            service: parse_service(fields[1])?,
            recovery_generation: Uuid::parse_str(fields[2])
                .map_err(|_| RecoverySealError::Database)?,
            original_security_sequence: fields[3]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
            security_sequence: fields[4]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
            revoked_session_records: fields[5]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
            invalidated_grant_records: fields[6]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
            invalidated_control_records: fields[7]
                .parse::<u64>()
                .map_err(|_| RecoverySealError::Database)?,
            unsafe_record_counts,
        })
    }

    fn all_invariants_hold(&self) -> bool {
        !self.deployment_id.is_nil()
            && !self.recovery_generation.is_nil()
            && self.original_security_sequence > 0
            && self.security_sequence > self.original_security_sequence
            && !self.unsafe_record_counts.is_empty()
            && self.unsafe_record_counts.iter().all(|count| *count == 0)
    }
}

fn recovery_seal_script(
    service: BackupService,
    source_state: &DatabaseSecurityState,
    original_security_sequence: u64,
    recovery_generation: Uuid,
) -> String {
    let owner = format!("pixels_{}_owner", service_name(service));
    let service_statements = match service {
        BackupService::Console => console_seal_statements(recovery_generation),
        BackupService::Auth => auth_seal_statements(),
        BackupService::Desk => desk_seal_statements(),
    };
    let verification_query = match service {
        BackupService::Console => {
            console_verification_query(original_security_sequence, recovery_generation)
        }
        BackupService::Auth => auth_verification_query(original_security_sequence),
        BackupService::Desk => desk_verification_query(original_security_sequence),
    };
    format!(
        "BEGIN;\nSET LOCAL ROLE {owner};\nDO $pixels_recovery_seal$\nBEGIN\n    IF EXISTS (SELECT 1 FROM pixels.recovery_security_state WHERE singleton AND recovery_generation='{new_generation}'::uuid) THEN\n        RETURN;\n    END IF;\n    UPDATE pixels.recovery_security_state SET recovery_generation='{new_generation}'::uuid,security_sequence=security_sequence+1,write_barrier_id=NULL,write_barrier_expires_at=NULL,write_gate_token_sha256=NULL WHERE singleton AND recovery_generation='{source_generation}'::uuid AND security_sequence={source_sequence};\n    IF NOT FOUND THEN\n        RAISE EXCEPTION 'recovery seal source state changed';\n    END IF;\n{service_statements}\nEND\n$pixels_recovery_seal$;\nCOMMIT;\n{verification_query}\n",
        owner = owner,
        new_generation = recovery_generation,
        source_generation = source_state.recovery_generation,
        source_sequence = source_state.security_sequence,
    )
}

fn console_seal_statements(recovery_generation: Uuid) -> String {
    format!(
        "    UPDATE pixels.users SET authorization_revision=authorization_revision+1,revision=revision+1,updated_at=clock_timestamp() WHERE deleted_at IS NULL;\n\
    UPDATE pixels.login_sessions SET revoked_at=clock_timestamp() WHERE revoked_at IS NULL;\n\
    UPDATE pixels.guest_sessions SET revoked_at=clock_timestamp(),revision=revision+1 WHERE revoked_at IS NULL;\n\
    DELETE FROM pixels.user_devices;\n\
    DELETE FROM pixels.group_device_grants;\n\
    DELETE FROM pixels.group_app_grants;\n\
    DELETE FROM pixels.authorization_outbox WHERE delivered_at IS NULL;\n\
    DELETE FROM pixels.application_events WHERE delivered_at IS NULL;\n\
    DELETE FROM pixels.guest_events WHERE delivered_at IS NULL;\n\
    UPDATE pixels.devices SET enrollment_hash=decode(replace('{generation}'::text,'-','') || replace(id::text,'-',''),'hex'),disabled=TRUE,revision=revision+1,updated_at=clock_timestamp() WHERE deleted_at IS NULL;\n\
    UPDATE pixels.applications SET disabled=TRUE,revision=revision+1,access_revision=revision+1,updated_at=clock_timestamp() WHERE deleted_at IS NULL;\n\
    UPDATE pixels.application_deployments SET disabled=TRUE,revision=revision+1,observed_state='pending',observed_reason=NULL,observed_generation=NULL,observed_epoch=NULL,observed_endpoint_revision=NULL,observed_sequence=0,observed_at=NULL;\n\
    UPDATE pixels.nodes SET credential_hash=decode(replace('{generation}'::text,'-','') || replace(id::text,'-',''),'hex'),revision=revision+1,generation=generation+1,control_epoch=NULL,connection_hash=NULL,state='offline',draining=TRUE,disabled=TRUE,report_sequence=0,reconciliation_id=NULL,reconciliation_deadline=NULL,last_seen=NULL WHERE deleted_at IS NULL;\n\
    UPDATE pixels.instance_commands SET state='cancelled',lease_id=NULL,lease_until=NULL,completed_at=clock_timestamp(),outcome=NULL,completed_lease_id=NULL WHERE state IN ('pending','claimed');\n\
    UPDATE pixels.instances SET state='reconcile_required',desired_state='stopped',revision=revision+1 WHERE ended_at IS NULL;\n\
    DELETE FROM pixels.resource_session_retirements;\n\
    UPDATE pixels.resource_sessions SET state='reconcile_required',revision=revision+1,descriptor_hash=NULL,descriptor_expires_at=NULL WHERE closed_at IS NULL;\n\
    UPDATE pixels.file_transfers SET state='unknown',reason='control_lost',revision=revision+1,updated_at=clock_timestamp() WHERE state='active';\n\
    UPDATE pixels.cache_read_leases SET closed_at=clock_timestamp() WHERE closed_at IS NULL;",
        generation = recovery_generation,
    )
}

fn auth_seal_statements() -> String {
    "    UPDATE pixels.authors SET authorization_revision=authorization_revision+1;\n\
    UPDATE pixels.author_sessions SET revoked_at=clock_timestamp() WHERE revoked_at IS NULL;\n\
    UPDATE pixels.licenses SET revoked_at=clock_timestamp(),revision=revision+1,updated_at=clock_timestamp() WHERE revoked_at IS NULL;\n\
    DELETE FROM pixels.license_requests WHERE issuance_id IS NULL;"
        .to_owned()
}

fn desk_seal_statements() -> String {
    "    UPDATE pixels.admin_sessions SET revoked_at=clock_timestamp() WHERE revoked_at IS NULL;"
        .to_owned()
}

fn console_verification_query(
    original_security_sequence: u64,
    recovery_generation: Uuid,
) -> String {
    format!(
        "SELECT deployment.deployment_id::text || '|console|' || state.recovery_generation::text || '|{original_sequence}|' || state.security_sequence::text || '|' || (SELECT count(*) FROM pixels.login_sessions WHERE revoked_at IS NOT NULL)::text || '|' || ((SELECT count(*) FROM pixels.user_devices)+(SELECT count(*) FROM pixels.group_device_grants)+(SELECT count(*) FROM pixels.group_app_grants))::text || '|' || ((SELECT count(*) FROM pixels.nodes)+(SELECT count(*) FROM pixels.instances WHERE state='reconcile_required')+(SELECT count(*) FROM pixels.instance_commands WHERE state='cancelled'))::text || '|' || (SELECT count(*) FROM pixels.login_sessions WHERE revoked_at IS NULL)::text || '|' || (SELECT count(*) FROM pixels.guest_sessions WHERE revoked_at IS NULL)::text || '|' || ((SELECT count(*) FROM pixels.user_devices)+(SELECT count(*) FROM pixels.group_device_grants)+(SELECT count(*) FROM pixels.group_app_grants))::text || '|' || ((SELECT count(*) FROM pixels.authorization_outbox WHERE delivered_at IS NULL)+(SELECT count(*) FROM pixels.application_events WHERE delivered_at IS NULL)+(SELECT count(*) FROM pixels.guest_events WHERE delivered_at IS NULL))::text || '|' || (SELECT count(*) FROM pixels.devices WHERE deleted_at IS NULL AND (NOT disabled OR enrollment_hash<>decode(replace('{generation}'::text,'-','') || replace(id::text,'-',''),'hex')))::text || '|' || (SELECT count(*) FROM pixels.applications WHERE deleted_at IS NULL AND NOT disabled)::text || '|' || (SELECT count(*) FROM pixels.application_deployments WHERE NOT disabled OR observed_state<>'pending' OR observed_sequence<>0)::text || '|' || (SELECT count(*) FROM pixels.nodes WHERE deleted_at IS NULL AND (NOT disabled OR NOT draining OR state<>'offline' OR connection_hash IS NOT NULL OR credential_hash<>decode(replace('{generation}'::text,'-','') || replace(id::text,'-',''),'hex')))::text || '|' || (SELECT count(*) FROM pixels.instance_commands WHERE state IN ('pending','claimed'))::text || '|' || (SELECT count(*) FROM pixels.instances WHERE ended_at IS NULL AND state<>'reconcile_required')::text || '|' || (SELECT count(*) FROM pixels.resource_sessions WHERE closed_at IS NULL AND (state<>'reconcile_required' OR descriptor_hash IS NOT NULL))::text || '|' || (SELECT count(*) FROM pixels.resource_session_retirements)::text || '|' || (SELECT count(*) FROM pixels.file_transfers WHERE state='active')::text || '|' || (SELECT count(*) FROM pixels.cache_read_leases WHERE closed_at IS NULL)::text FROM pixels.deployment_identity AS deployment CROSS JOIN pixels.recovery_security_state AS state WHERE deployment.singleton AND state.singleton;\n",
        original_sequence = original_security_sequence,
        generation = recovery_generation,
    )
}

fn auth_verification_query(original_security_sequence: u64) -> String {
    format!("SELECT deployment.deployment_id::text || '|auth|' || state.recovery_generation::text || '|{original_security_sequence}|' || state.security_sequence::text || '|' || (SELECT count(*) FROM pixels.author_sessions WHERE revoked_at IS NOT NULL)::text || '|' || (SELECT count(*) FROM pixels.licenses WHERE revoked_at IS NOT NULL)::text || '|' || (SELECT count(*) FROM pixels.license_requests WHERE issuance_id IS NULL)::text || '|' || (SELECT count(*) FROM pixels.author_sessions WHERE revoked_at IS NULL)::text || '|' || (SELECT count(*) FROM pixels.licenses WHERE revoked_at IS NULL)::text || '|' || (SELECT count(*) FROM pixels.license_requests WHERE issuance_id IS NULL)::text FROM pixels.deployment_identity AS deployment CROSS JOIN pixels.recovery_security_state AS state WHERE deployment.singleton AND state.singleton;\n")
}

fn desk_verification_query(original_security_sequence: u64) -> String {
    format!("SELECT deployment.deployment_id::text || '|desk|' || state.recovery_generation::text || '|{original_security_sequence}|' || state.security_sequence::text || '|' || (SELECT count(*) FROM pixels.admin_sessions WHERE revoked_at IS NOT NULL)::text || '|0|0|' || (SELECT count(*) FROM pixels.admin_sessions WHERE revoked_at IS NULL)::text FROM pixels.deployment_identity AS deployment CROSS JOIN pixels.recovery_security_state AS state WHERE deployment.singleton AND state.singleton;\n")
}

fn security_evidence_sha256(manifest: &RecoverySetManifest) -> Result<String, RecoverySealError> {
    let evidence_bytes = serde_json::to_vec(&manifest.security_evidence)
        .map_err(|_| RecoverySealError::InvalidEvidence)?;
    Ok(format!("{:x}", Sha256::digest(evidence_bytes)))
}

fn security_state_sha256(
    deployment_id: Uuid,
    service: BackupService,
    recovery_generation: Uuid,
    security_sequence: u64,
) -> String {
    let state_material = format!(
        "{}|{}|{}|{}",
        deployment_id,
        service_name(service),
        recovery_generation,
        security_sequence
    );
    format!("{:x}", Sha256::digest(state_material.as_bytes()))
}

fn service_name(service: BackupService) -> &'static str {
    match service {
        BackupService::Console => "console",
        BackupService::Auth => "auth",
        BackupService::Desk => "desk",
    }
}

fn parse_service(value: &str) -> Result<BackupService, RecoverySealError> {
    match value {
        "console" => Ok(BackupService::Console),
        "auth" => Ok(BackupService::Auth),
        "desk" => Ok(BackupService::Desk),
        _ => Err(RecoverySealError::Database),
    }
}

fn sanitize_postgres_environment(command: &mut Command) {
    for (environment_name, _) in env::vars_os() {
        let normalized_name = environment_name.to_string_lossy().to_ascii_uppercase();
        if normalized_name.starts_with("PG") || normalized_name == "DATABASE_URL" {
            command.env_remove(environment_name);
        }
    }
    command.env("PGCONNECT_TIMEOUT", "10");
}

fn run_command_with_input(
    command: &mut Command,
    input: &[u8],
    timeout: Duration,
    cancellation: &BackupCancellation,
) -> Result<String, RecoverySealError> {
    let mut child = command.spawn().map_err(|_| RecoverySealError::Database)?;
    let mut stdin = child.stdin.take().ok_or(RecoverySealError::Database)?;
    stdin
        .write_all(input)
        .map_err(|_| RecoverySealError::Database)?;
    drop(stdin);
    wait_for_command(&mut child, timeout, cancellation)?;
    let mut stdout = child.stdout.take().ok_or(RecoverySealError::Database)?;
    let mut output = Vec::new();
    stdout
        .read_to_end(&mut output)
        .map_err(|_| RecoverySealError::Database)?;
    String::from_utf8(output).map_err(|_| RecoverySealError::Database)
}

fn wait_for_command(
    child: &mut Child,
    timeout: Duration,
    cancellation: &BackupCancellation,
) -> Result<(), RecoverySealError> {
    let started = Instant::now();
    loop {
        if cancellation.is_cancelled() {
            let _ = child.kill();
            let _ = child.wait();
            return Err(RecoverySealError::Cancelled);
        }
        if started.elapsed() >= timeout {
            let _ = child.kill();
            let _ = child.wait();
            return Err(RecoverySealError::ToolTimeout);
        }
        match child.try_wait() {
            Ok(Some(status)) if status.success() => return Ok(()),
            Ok(Some(_)) | Err(_) => return Err(RecoverySealError::Database),
            Ok(None) => thread::sleep(Duration::from_millis(25)),
        }
    }
}

fn persist_private<T: Serialize>(path: &Path, value: &T) -> Result<(), RecoverySealError> {
    if !path.is_absolute() || path.exists() {
        return Err(RecoverySealError::PrivateState);
    }
    let bytes = serde_json::to_vec_pretty(value).map_err(|_| RecoverySealError::PrivateState)?;
    px_private_files::private::create_private(path, &bytes)
        .map_err(|_| RecoverySealError::PrivateState)
}

fn load_private<T: for<'de> Deserialize<'de>>(path: &Path) -> Result<T, RecoverySealError> {
    let bytes = px_private_files::private::read_private(path)
        .map_err(|_| RecoverySealError::PrivateState)?;
    serde_json::from_slice(&bytes).map_err(|_| RecoverySealError::PrivateState)
}

fn current_unix_time() -> Result<u64, RecoverySealError> {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| RecoverySealError::Clock)
        .map(|duration| duration.as_secs())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        BackupMember, BackupMemberState, RecoverySetKind, RecoverySetStatus, RetentionClass,
        ServiceSecurityWatermark, MANIFEST_SCHEMA_VERSION,
    };
    use std::sync::{Arc, Mutex};
    use tempfile::TempDir;

    #[derive(Clone)]
    struct FakeSealTool {
        calls: Arc<Mutex<Vec<BackupService>>>,
        manifest: RecoverySetManifest,
    }

    impl RecoverySealTool for FakeSealTool {
        fn seal_target(
            &self,
            target: &RestoreDatabaseTarget,
            _deployment_id: Uuid,
            expected_source_generation: Uuid,
            expected_source_sequence: u64,
            expected_source_state_sha256: &str,
            _recovery_generation: Uuid,
        ) -> Result<RecoverySealTargetResult, RecoverySealError> {
            self.calls.lock().unwrap().push(target.service);
            let watermark = match &self.manifest.security_evidence {
                RecoverySecurityEvidence::Captured { watermarks, .. } => watermarks
                    .iter()
                    .find(|watermark| watermark.service == target.service)
                    .unwrap(),
                RecoverySecurityEvidence::Unavailable { .. } => unreachable!(),
            };
            assert_eq!(
                expected_source_state_sha256,
                watermark.security_state_sha256
            );
            assert_eq!(expected_source_sequence, watermark.security_sequence);
            assert_eq!(expected_source_generation, watermark.recovery_generation);
            Ok(RecoverySealTargetResult {
                source_security_sequence: watermark.security_sequence,
                source_security_state_sha256: watermark.security_state_sha256.clone(),
                sealed_security_sequence: watermark.security_sequence + 10,
                sealed_security_state_sha256: "b".repeat(64),
                revoked_session_records: 3,
                invalidated_grant_records: 2,
                invalidated_control_records: 1,
            })
        }
    }

    struct Fixture {
        _temporary_directory: TempDir,
        plan: RecoverySealPlan,
        manifest: RecoverySetManifest,
    }

    impl Fixture {
        fn new() -> Self {
            let temporary_directory = tempfile::tempdir().unwrap();
            make_private_directory(temporary_directory.path());
            let deployment_id = Uuid::new_v4();
            let recovery_set_id = Uuid::new_v4();
            let target_environment_id = Uuid::new_v4();
            let services = [
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ];
            let members = services
                .iter()
                .map(|service| BackupMember {
                    service: *service,
                    member: BackupMemberState::Required {
                        database: format!("pixels_{}", service_name(*service)),
                        schema_version: 1,
                        archive_file: format!("{}.dump", service_name(*service)),
                        archive_sha256: "a".repeat(64),
                        started_at_unix: 100,
                        completed_at_unix: 101,
                    },
                })
                .collect::<Vec<_>>();
            let watermarks = services
                .iter()
                .enumerate()
                .map(|(index, service)| ServiceSecurityWatermark {
                    service: *service,
                    recovery_generation: Uuid::new_v4(),
                    security_sequence: index as u64 + 5,
                    security_state_sha256: format!("{:064x}", index + 1),
                })
                .collect::<Vec<_>>();
            let manifest = RecoverySetManifest {
                schema_version: MANIFEST_SCHEMA_VERSION,
                recovery_set_id,
                deployment_id,
                kind: RecoverySetKind::WriteBarrier,
                status: RecoverySetStatus::Verified,
                created_at_unix: 100,
                completed_at_unix: Some(101),
                locked: false,
                restoring: false,
                retention: BTreeSet::from([RetentionClass::Hourly]),
                previous_recovery_set_id: None,
                members,
                security_evidence: RecoverySecurityEvidence::Captured {
                    consistency_proof_id: Uuid::new_v4(),
                    external_key_ids: BTreeSet::new(),
                    watermarks,
                },
                failure_code: None,
            };
            let targets = services
                .iter()
                .map(|service| RestoreDatabaseTarget {
                    service: *service,
                    host: "127.0.0.1".to_owned(),
                    port: 5432,
                    database: format!(
                        "pixels_restore_{}_{}",
                        &target_environment_id.simple().to_string()[..12],
                        service_name(*service)
                    ),
                    owner: format!("pixels_{}_owner", service_name(*service)),
                    username: "pixels_restore_operator".to_owned(),
                    password_file: temporary_directory.path().join("restore.pgpass"),
                })
                .collect::<Vec<_>>();
            let plan = RecoverySealPlan {
                deployment_id,
                recovery_set_id,
                target_environment_id,
                lock_file: temporary_directory.path().join("seal.lock"),
                marker_file: temporary_directory.path().join("seal.marker.json"),
                report_file: temporary_directory.path().join("seal.report.json"),
                targets,
            };
            Self {
                _temporary_directory: temporary_directory,
                plan,
                manifest,
            }
        }
    }

    #[test]
    fn seal_is_bound_to_captured_source_evidence_and_is_idempotent_after_report() {
        let fixture = Fixture::new();
        let calls = Arc::new(Mutex::new(Vec::new()));
        let runner = RecoverySealRunner::new(FakeSealTool {
            calls: Arc::clone(&calls),
            manifest: fixture.manifest.clone(),
        });
        let report = runner.seal(&fixture.manifest, &fixture.plan).unwrap();
        assert!(report.old_sessions_revoked);
        assert!(report.old_grants_invalidated);
        assert!(report.pending_control_invalidated);
        assert!(report.admission_required);
        assert!(!fixture.plan.marker_file.exists());
        assert!(fixture.plan.report_file.exists());
        assert_eq!(calls.lock().unwrap().len(), 3);

        let repeated_report = runner.seal(&fixture.manifest, &fixture.plan).unwrap();
        assert_eq!(repeated_report, report);
        assert_eq!(calls.lock().unwrap().len(), 3);
    }

    #[test]
    fn unavailable_or_mismatched_security_evidence_is_rejected_before_mutation() {
        let mut fixture = Fixture::new();
        fixture.manifest.security_evidence = RecoverySecurityEvidence::Unavailable {
            reason: crate::RecoveryEvidenceUnavailableReason::IndependentBackup,
        };
        let calls = Arc::new(Mutex::new(Vec::new()));
        let runner = RecoverySealRunner::new(FakeSealTool {
            calls: Arc::clone(&calls),
            manifest: fixture.manifest.clone(),
        });
        assert_eq!(
            runner.seal(&fixture.manifest, &fixture.plan),
            Err(RecoverySealError::InvalidEvidence)
        );
        assert!(calls.lock().unwrap().is_empty());
    }

    #[test]
    fn marker_with_a_different_plan_fails_closed() {
        let fixture = Fixture::new();
        let evidence_sha256 = security_evidence_sha256(&fixture.manifest).unwrap();
        let mut marker =
            RecoverySealMarker::new(&fixture.plan, Uuid::new_v4(), 100, evidence_sha256);
        marker.target_environment_id = Uuid::new_v4();
        persist_private(&fixture.plan.marker_file, &marker).unwrap();
        let runner = RecoverySealRunner::new(FakeSealTool {
            calls: Arc::new(Mutex::new(Vec::new())),
            manifest: fixture.manifest.clone(),
        });
        assert_eq!(
            runner.seal(&fixture.manifest, &fixture.plan),
            Err(RecoverySealError::ReconciliationRequired)
        );
    }

    #[test]
    fn a_second_recovery_seal_process_is_rejected_while_the_first_holds_the_lock() {
        let fixture = Fixture::new();
        let _operation_lock = RecoverySealOperationLock::acquire(&fixture.plan.lock_file).unwrap();
        let runner = RecoverySealRunner::new(FakeSealTool {
            calls: Arc::new(Mutex::new(Vec::new())),
            manifest: fixture.manifest.clone(),
        });
        assert_eq!(
            runner.seal(&fixture.manifest, &fixture.plan),
            Err(RecoverySealError::ReconciliationRequired)
        );
    }

    fn make_private_directory(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(path, fs::Permissions::from_mode(0o700)).unwrap();
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
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
                .args(["/inheritance:r", "/grant:r", &access_grant])
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(access_result.status.success());
        }
    }
}
