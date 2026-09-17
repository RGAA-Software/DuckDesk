use crate::{
    executor::hash_file, BackupCancellation, BackupError, BackupService, WriteBarrierProof,
    WriteBarrierServiceAttestation, WRITE_BARRIER_PROOF_SCHEMA_VERSION,
};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::BTreeSet,
    env, fs,
    io::{Read, Write},
    net::IpAddr,
    path::{Path, PathBuf},
    process::{Child, Command, Stdio},
    thread,
    time::{Duration, Instant, SystemTime, UNIX_EPOCH},
};
use uuid::Uuid;

const BARRIER_MARKER_SCHEMA_VERSION: u32 = 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum WriteBarrierError {
    #[error("write-barrier plan is invalid")]
    InvalidPlan,
    #[error("write-barrier state requires reconciliation")]
    ReconciliationRequired,
    #[error("pinned PostgreSQL write-barrier tool is unavailable or changed")]
    ToolIdentity,
    #[error("write-barrier credential is unavailable")]
    Credential,
    #[error("write-barrier database operation failed closed")]
    Database,
    #[error("write-barrier command timed out")]
    ToolTimeout,
    #[error("write-barrier operation was cancelled")]
    Cancelled,
    #[error("system clock is unavailable")]
    Clock,
    #[error("private write-barrier evidence could not be persisted")]
    PrivateState,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WriteBarrierDatabaseTarget {
    pub service: BackupService,
    pub host: String,
    pub port: u16,
    pub database: String,
    pub admin_username: String,
    pub admin_password_file: PathBuf,
}

impl WriteBarrierDatabaseTarget {
    fn validate(&self) -> Result<(), WriteBarrierError> {
        if self.port == 0
            || !valid_host(&self.host)
            || !valid_identifier(&self.admin_username)
            || !self.admin_password_file.is_absolute()
            || self.database != format!("pixels_{}", service_name(self.service))
        {
            return Err(WriteBarrierError::InvalidPlan);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct WriteBarrierCoordinatorPlan {
    pub deployment_id: Uuid,
    pub proof_file: PathBuf,
    pub marker_file: PathBuf,
    pub lease_seconds: u64,
    pub external_key_ids: BTreeSet<String>,
    pub targets: Vec<WriteBarrierDatabaseTarget>,
}

impl WriteBarrierCoordinatorPlan {
    pub fn validate(&self) -> Result<(), WriteBarrierError> {
        let services = self
            .targets
            .iter()
            .map(|target| target.service)
            .collect::<BTreeSet<_>>();
        if self.deployment_id.is_nil()
            || !self.proof_file.is_absolute()
            || !self.marker_file.is_absolute()
            || self.proof_file == self.marker_file
            || !(30..=60 * 60).contains(&self.lease_seconds)
            || self.external_key_ids.len() > 128
            || self
                .external_key_ids
                .iter()
                .any(|key_id| !valid_sha256(key_id))
            || self.targets.is_empty()
            || self.targets.len() > 3
            || services.len() != self.targets.len()
            || !services.contains(&BackupService::Console)
        {
            return Err(WriteBarrierError::InvalidPlan);
        }
        for target in &self.targets {
            target.validate()?;
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct WriteBarrierMarkerTarget {
    target: WriteBarrierDatabaseTarget,
    write_gate_token_sha256: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct WriteBarrierMarker {
    schema_version: u32,
    deployment_id: Uuid,
    consistency_proof_id: Uuid,
    acquired_at_unix: u64,
    expires_at_unix: u64,
    proof_file: PathBuf,
    targets: Vec<WriteBarrierMarkerTarget>,
}

impl WriteBarrierMarker {
    fn matches_plan(&self, plan: &WriteBarrierCoordinatorPlan) -> bool {
        self.schema_version == BARRIER_MARKER_SCHEMA_VERSION
            && self.deployment_id == plan.deployment_id
            && !self.consistency_proof_id.is_nil()
            && self.acquired_at_unix > 0
            && self.expires_at_unix > self.acquired_at_unix
            && self.expires_at_unix - self.acquired_at_unix == plan.lease_seconds
            && self.proof_file == plan.proof_file
            && self.targets.len() == plan.targets.len()
            && self
                .targets
                .iter()
                .zip(&plan.targets)
                .all(|(marker_target, plan_target)| {
                    marker_target.target == *plan_target
                        && valid_sha256(&marker_target.write_gate_token_sha256)
                })
    }
}

#[derive(Debug, Clone)]
pub struct PinnedPgWriteBarrierCoordinator {
    psql: PathBuf,
    psql_sha256: String,
    command_timeout: Duration,
    cancellation: BackupCancellation,
}

impl PinnedPgWriteBarrierCoordinator {
    pub fn new(
        psql: PathBuf,
        psql_sha256: String,
        command_timeout: Duration,
        cancellation: BackupCancellation,
    ) -> Result<Self, WriteBarrierError> {
        if !valid_tool_path(&psql, "psql")
            || !valid_sha256(&psql_sha256)
            || command_timeout < Duration::from_secs(1)
            || command_timeout > Duration::from_secs(24 * 60 * 60)
        {
            return Err(WriteBarrierError::ToolIdentity);
        }
        let coordinator = Self {
            psql,
            psql_sha256,
            command_timeout,
            cancellation,
        };
        coordinator.verify_tool()?;
        Ok(coordinator)
    }

    pub fn acquire(
        &self,
        plan: &WriteBarrierCoordinatorPlan,
    ) -> Result<WriteBarrierProof, WriteBarrierError> {
        plan.validate()?;
        self.verify_tool()?;
        if plan.proof_file.exists() || plan.marker_file.exists() {
            return Err(WriteBarrierError::ReconciliationRequired);
        }
        for target in &plan.targets {
            drop(
                px_private_files::private::read_private(&target.admin_password_file)
                    .map_err(|_| WriteBarrierError::Credential)?,
            );
        }
        let acquired_at_unix = current_unix_time()?;
        let expires_at_unix = acquired_at_unix
            .checked_add(plan.lease_seconds)
            .ok_or(WriteBarrierError::Clock)?;
        let consistency_proof_id = Uuid::new_v4();
        let marker_targets = plan
            .targets
            .iter()
            .map(|target| WriteBarrierMarkerTarget {
                target: target.clone(),
                write_gate_token_sha256: write_gate_token_sha256(
                    consistency_proof_id,
                    target.service,
                ),
            })
            .collect::<Vec<_>>();
        let marker = WriteBarrierMarker {
            schema_version: BARRIER_MARKER_SCHEMA_VERSION,
            deployment_id: plan.deployment_id,
            consistency_proof_id,
            acquired_at_unix,
            expires_at_unix,
            proof_file: plan.proof_file.clone(),
            targets: marker_targets,
        };
        persist_private(&plan.marker_file, &marker)?;

        let mut attestations = Vec::with_capacity(marker.targets.len());
        for marker_target in &marker.targets {
            match self.acquire_target(plan, &marker, marker_target) {
                Ok(attestation) => attestations.push(attestation),
                Err(error) => {
                    let mut compensated = true;
                    for applied_target in marker.targets[..=attestations.len()].iter().rev() {
                        if self.release_target(&marker, applied_target).is_err() {
                            compensated = false;
                        }
                    }
                    if compensated {
                        let _ = fs::remove_file(&plan.marker_file);
                    }
                    return Err(error);
                }
            }
        }
        let proof = WriteBarrierProof {
            schema_version: WRITE_BARRIER_PROOF_SCHEMA_VERSION,
            deployment_id: plan.deployment_id,
            consistency_proof_id,
            acquired_at_unix,
            expires_at_unix,
            external_key_ids: plan.external_key_ids.clone(),
            attestations,
        };
        if let Err(error) = persist_private(&plan.proof_file, &proof) {
            let mut compensated = true;
            for target in marker.targets.iter().rev() {
                if self.release_target(&marker, target).is_err() {
                    compensated = false;
                }
            }
            if compensated {
                let _ = fs::remove_file(&plan.marker_file);
            }
            return Err(error);
        }
        Ok(proof)
    }

    pub fn release(&self, plan: &WriteBarrierCoordinatorPlan) -> Result<(), WriteBarrierError> {
        plan.validate()?;
        self.verify_tool()?;
        if !plan.marker_file.exists() {
            return if plan.proof_file.exists() {
                Err(WriteBarrierError::ReconciliationRequired)
            } else {
                Ok(())
            };
        }
        let marker = load_private::<WriteBarrierMarker>(&plan.marker_file)?;
        if !marker.matches_plan(plan) {
            return Err(WriteBarrierError::ReconciliationRequired);
        }
        for target in marker.targets.iter().rev() {
            self.release_target(&marker, target)?;
        }
        if plan.proof_file.exists() {
            let proof = load_private::<WriteBarrierProof>(&plan.proof_file)?;
            if proof.deployment_id != marker.deployment_id
                || proof.consistency_proof_id != marker.consistency_proof_id
            {
                return Err(WriteBarrierError::ReconciliationRequired);
            }
            fs::remove_file(&plan.proof_file).map_err(|_| WriteBarrierError::PrivateState)?;
        }
        fs::remove_file(&plan.marker_file).map_err(|_| WriteBarrierError::PrivateState)
    }

    fn acquire_target(
        &self,
        plan: &WriteBarrierCoordinatorPlan,
        marker: &WriteBarrierMarker,
        marker_target: &WriteBarrierMarkerTarget,
    ) -> Result<WriteBarrierServiceAttestation, WriteBarrierError> {
        let target = &marker_target.target;
        let service = service_name(target.service);
        let runtime_role = format!("pixels_{service}_runtime");
        let barrier_script = format!(
            "UPDATE pixels.recovery_security_state SET write_barrier_id='{barrier_id}'::uuid,write_barrier_expires_at=to_timestamp({expires}),write_gate_token_sha256='{token}' WHERE singleton AND (write_barrier_id IS NULL OR write_barrier_expires_at<=clock_timestamp());\n\
SELECT deployment.deployment_id::text || '|' || deployment.service || '|' || state.recovery_generation::text || '|' || state.security_sequence::text || '|' || state.write_barrier_id::text || '|' || state.write_gate_token_sha256 FROM pixels.deployment_identity AS deployment CROSS JOIN pixels.recovery_security_state AS state WHERE deployment.singleton AND state.singleton;\n",
            barrier_id = marker.consistency_proof_id,
            expires = marker.expires_at_unix,
            token = marker_target.write_gate_token_sha256,
        );
        let identity_output = self.run_psql(target, &target.database, &barrier_script)?;
        let fields = identity_output.trim().split('|').collect::<Vec<_>>();
        if fields.len() != 6
            || fields[0] != plan.deployment_id.to_string()
            || fields[1] != service
            || fields[4] != marker.consistency_proof_id.to_string()
            || fields[5] != marker_target.write_gate_token_sha256
        {
            return Err(WriteBarrierError::Database);
        }
        let recovery_generation =
            Uuid::parse_str(fields[2]).map_err(|_| WriteBarrierError::Database)?;
        let security_sequence = fields[3]
            .parse::<u64>()
            .map_err(|_| WriteBarrierError::Database)?;
        if recovery_generation.is_nil() || security_sequence == 0 {
            return Err(WriteBarrierError::Database);
        }
        let fence_script = format!(
            "REVOKE CONNECT ON DATABASE {database} FROM {runtime_role};\n\
SELECT pg_terminate_backend(activity.pid) FROM pg_catalog.pg_stat_activity AS activity WHERE activity.datname='{database}' AND activity.usename='{runtime_role}' AND activity.pid<>pg_backend_pid();\n\
SELECT (NOT pg_catalog.has_database_privilege('{runtime_role}','{database}','CONNECT'))::text || '|' || (SELECT count(*)::text FROM pg_catalog.pg_stat_activity AS activity WHERE activity.datname='{database}' AND activity.usename='{runtime_role}');\n",
            database = target.database,
        );
        let fence_output = self.run_psql(target, "postgres", &fence_script)?;
        let fence_result = fence_output
            .lines()
            .map(str::trim)
            .rfind(|line| line.contains('|'))
            .ok_or(WriteBarrierError::Database)?;
        if fence_result != "true|0" {
            return Err(WriteBarrierError::Database);
        }
        let drained_at_unix = current_unix_time()?;
        if drained_at_unix >= marker.expires_at_unix {
            return Err(WriteBarrierError::Database);
        }
        let state_material = format!(
            "{}|{}|{}|{}",
            plan.deployment_id, service, recovery_generation, security_sequence
        );
        Ok(WriteBarrierServiceAttestation {
            service: target.service,
            recovery_generation,
            drained_at_unix,
            lease_expires_at_unix: marker.expires_at_unix,
            write_gate_token_sha256: marker_target.write_gate_token_sha256.clone(),
            security_sequence,
            security_state_sha256: format!("{:x}", Sha256::digest(state_material.as_bytes())),
        })
    }

    fn release_target(
        &self,
        marker: &WriteBarrierMarker,
        marker_target: &WriteBarrierMarkerTarget,
    ) -> Result<(), WriteBarrierError> {
        let target = &marker_target.target;
        let service = service_name(target.service);
        let runtime_role = format!("pixels_{service}_runtime");
        let clear_script = format!(
            "UPDATE pixels.recovery_security_state SET write_barrier_id=NULL,write_barrier_expires_at=NULL,write_gate_token_sha256=NULL WHERE singleton AND write_barrier_id='{barrier_id}'::uuid;\n\
SELECT (write_barrier_id IS NULL)::text FROM pixels.recovery_security_state WHERE singleton;\n",
            barrier_id = marker.consistency_proof_id,
        );
        if self
            .run_psql(target, &target.database, &clear_script)?
            .trim()
            != "true"
        {
            return Err(WriteBarrierError::Database);
        }
        let grant_script = format!(
            "GRANT CONNECT ON DATABASE {database} TO {runtime_role};\n\
SELECT pg_catalog.has_database_privilege('{runtime_role}','{database}','CONNECT')::text;\n",
            database = target.database,
        );
        if self.run_psql(target, "postgres", &grant_script)?.trim() != "true" {
            return Err(WriteBarrierError::Database);
        }
        Ok(())
    }

    fn run_psql(
        &self,
        target: &WriteBarrierDatabaseTarget,
        database: &str,
        script: &str,
    ) -> Result<String, WriteBarrierError> {
        self.verify_tool()?;
        if self.cancellation.is_cancelled() {
            return Err(WriteBarrierError::Cancelled);
        }
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
            .arg(&target.admin_username)
            .arg("--dbname")
            .arg(database)
            .args(["--set=ON_ERROR_STOP=1", "--file=-"])
            .env("PGPASSFILE", &target.admin_password_file)
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

    fn verify_tool(&self) -> Result<(), WriteBarrierError> {
        let actual_sha256 = hash_file(&self.psql, BackupError::ToolIdentity)
            .map_err(|_| WriteBarrierError::ToolIdentity)?;
        if actual_sha256 == self.psql_sha256 {
            Ok(())
        } else {
            Err(WriteBarrierError::ToolIdentity)
        }
    }
}

fn persist_private<T: Serialize>(path: &Path, value: &T) -> Result<(), WriteBarrierError> {
    let bytes = serde_json::to_vec(value).map_err(|_| WriteBarrierError::PrivateState)?;
    px_private_files::private::create_private(path, &bytes)
        .map_err(|_| WriteBarrierError::PrivateState)
}

fn load_private<T: for<'de> Deserialize<'de>>(path: &Path) -> Result<T, WriteBarrierError> {
    let bytes = px_private_files::private::read_private(path)
        .map_err(|_| WriteBarrierError::PrivateState)?;
    serde_json::from_slice(&bytes).map_err(|_| WriteBarrierError::PrivateState)
}

fn run_command_with_input(
    command: &mut Command,
    standard_input: &[u8],
    timeout: Duration,
    cancellation: &BackupCancellation,
) -> Result<String, WriteBarrierError> {
    let mut child = command.spawn().map_err(|_| WriteBarrierError::Database)?;
    let write_result = child
        .stdin
        .take()
        .ok_or(WriteBarrierError::Database)
        .and_then(|mut input| {
            input
                .write_all(standard_input)
                .map_err(|_| WriteBarrierError::Database)
        });
    if let Err(error) = write_result {
        stop_child(&mut child);
        return Err(error);
    }
    let started = Instant::now();
    loop {
        if cancellation.is_cancelled() {
            stop_child(&mut child);
            return Err(WriteBarrierError::Cancelled);
        }
        if started.elapsed() >= timeout {
            stop_child(&mut child);
            return Err(WriteBarrierError::ToolTimeout);
        }
        match child.try_wait() {
            Ok(Some(status)) if status.success() => {
                let mut output = String::new();
                child
                    .stdout
                    .take()
                    .ok_or(WriteBarrierError::Database)?
                    .take(4097)
                    .read_to_string(&mut output)
                    .map_err(|_| WriteBarrierError::Database)?;
                if output.len() > 4096 {
                    return Err(WriteBarrierError::Database);
                }
                return Ok(output.trim().to_string());
            }
            Ok(Some(_)) => return Err(WriteBarrierError::Database),
            Ok(None) => thread::sleep(Duration::from_millis(100)),
            Err(_) => {
                stop_child(&mut child);
                return Err(WriteBarrierError::Database);
            }
        }
    }
}

fn stop_child(child: &mut Child) {
    let _ = child.kill();
    let _ = child.wait();
}

fn write_gate_token_sha256(consistency_proof_id: Uuid, service: BackupService) -> String {
    let nonce = Uuid::new_v4();
    let material = format!(
        "{}|{}|{}",
        consistency_proof_id,
        service_name(service),
        nonce
    );
    format!("{:x}", Sha256::digest(material.as_bytes()))
}

fn current_unix_time() -> Result<u64, WriteBarrierError> {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .map_err(|_| WriteBarrierError::Clock)
}

fn sanitize_postgres_environment(command: &mut Command) {
    for (environment_name, _) in env::vars_os() {
        let normalized_name = environment_name.to_string_lossy().to_ascii_uppercase();
        if normalized_name.starts_with("PG") || normalized_name == "DATABASE_URL" {
            command.env_remove(environment_name);
        }
    }
}

fn valid_tool_path(path: &Path, expected_stem: &str) -> bool {
    if !path.is_absolute() || !path.is_file() {
        return false;
    }
    path.file_stem()
        .and_then(|name| name.to_str())
        .is_some_and(|name| name.eq_ignore_ascii_case(expected_stem))
}

fn valid_host(value: &str) -> bool {
    if value.is_empty() || value.len() > 253 || value.contains(['/', '\\', '?', '#', '@']) {
        return false;
    }
    if value.parse::<IpAddr>().is_ok() {
        return true;
    }
    value.split('.').all(|label| {
        !label.is_empty()
            && label.len() <= 63
            && !label.starts_with('-')
            && !label.ends_with('-')
            && label
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
    })
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

fn service_name(service: BackupService) -> &'static str {
    match service {
        BackupService::Console => "console",
        BackupService::Auth => "auth",
        BackupService::Desk => "desk",
    }
}
