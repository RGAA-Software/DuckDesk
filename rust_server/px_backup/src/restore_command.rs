use px_backup::{
    BackupCancellation, BackupRepository, ExternalRecoveryWitness, PinnedPgRecoverySealTool,
    PinnedPgRestoreProvisioner, PinnedPgRestoreTools, PinnedPgWriteBarrierCoordinator,
    RecoverySealPlan, RecoverySealReport, RecoverySealRunner, RecoverySetManifest,
    RestoreAdmissionState, RestoreAdmissionStore, RestoreExecutionPlan, RestoreOperationalCheck,
    RestoreOperatorProvisionPlan, RestoreRunner, WriteBarrierCoordinatorPlan,
};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeSet, path::PathBuf, time::Duration};
use uuid::Uuid;

const RESTORE_COMMAND_CONFIG_SCHEMA_VERSION: u32 = 2;
const RESTORE_APPROVAL_REQUEST_SCHEMA_VERSION: u32 = 1;
const RESTORE_EXECUTION_COMMAND_CONFIG_SCHEMA_VERSION: u32 = 1;
const RESTORE_PROVISION_COMMAND_CONFIG_SCHEMA_VERSION: u32 = 1;
const WRITE_BARRIER_COMMAND_CONFIG_SCHEMA_VERSION: u32 = 1;
const RECOVERY_SEAL_COMMAND_CONFIG_SCHEMA_VERSION: u32 = 1;

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct RestoreCommandConfig {
    schema_version: u32,
    deployment_id: Uuid,
    recovery_set_id: Uuid,
    target_environment_id: Uuid,
    repository_root: PathBuf,
    admission_root: PathBuf,
    witness_path: PathBuf,
    recovery_seal_report_path: PathBuf,
    completed_checks: BTreeSet<RestoreOperationalCheck>,
}

impl RestoreCommandConfig {
    fn load_private(config_path: &std::path::Path) -> Result<Self, &'static str> {
        if !config_path.is_absolute() {
            return Err("restore configuration rejected");
        }
        let config_bytes = px_private_files::private::read_private(config_path)
            .map_err(|_| "restore configuration rejected")?;
        let config = serde_json::from_slice::<Self>(&config_bytes)
            .map_err(|_| "restore configuration rejected")?;
        if !config.is_valid() {
            return Err("restore configuration rejected");
        }
        Ok(config)
    }

    fn is_valid(&self) -> bool {
        self.schema_version == RESTORE_COMMAND_CONFIG_SCHEMA_VERSION
            && !self.deployment_id.is_nil()
            && !self.recovery_set_id.is_nil()
            && !self.target_environment_id.is_nil()
            && self.repository_root.is_absolute()
            && self.admission_root.is_absolute()
            && self.witness_path.is_absolute()
            && self.recovery_seal_report_path.is_absolute()
            && self.repository_root != self.admission_root
            && self.recovery_seal_report_path != self.witness_path
            && !self
                .recovery_seal_report_path
                .starts_with(&self.repository_root)
    }
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct RestoreApprovalRequest {
    schema_version: u32,
    approval_id: Uuid,
    administrator_id: Uuid,
    expected_revision: u64,
    expected_evidence_sha256: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct RestoreExecutionCommandConfig {
    schema_version: u32,
    repository_root: PathBuf,
    report_path: PathBuf,
    plan: RestoreExecutionPlan,
    createdb_path: PathBuf,
    createdb_sha256: String,
    pg_restore_path: PathBuf,
    pg_restore_sha256: String,
    psql_path: PathBuf,
    psql_sha256: String,
    command_timeout_seconds: u64,
}

impl RestoreExecutionCommandConfig {
    fn load_private(config_path: &std::path::Path) -> Result<Self, &'static str> {
        if !config_path.is_absolute() {
            return Err("restore execution configuration rejected");
        }
        let config_bytes = px_private_files::private::read_private(config_path)
            .map_err(|_| "restore execution configuration rejected")?;
        let config = serde_json::from_slice::<Self>(&config_bytes)
            .map_err(|_| "restore execution configuration rejected")?;
        if !config.is_valid() {
            return Err("restore execution configuration rejected");
        }
        Ok(config)
    }

    fn is_valid(&self) -> bool {
        self.schema_version == RESTORE_EXECUTION_COMMAND_CONFIG_SCHEMA_VERSION
            && self.repository_root.is_absolute()
            && self.report_path.is_absolute()
            && !self.report_path.starts_with(&self.repository_root)
            && self.createdb_path.is_absolute()
            && self.pg_restore_path.is_absolute()
            && self.psql_path.is_absolute()
            && valid_sha256(&self.createdb_sha256)
            && valid_sha256(&self.pg_restore_sha256)
            && valid_sha256(&self.psql_sha256)
            && (1..=24 * 60 * 60).contains(&self.command_timeout_seconds)
            && self.plan.validate().is_ok()
    }
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct RestoreProvisionCommandConfig {
    schema_version: u32,
    plan: RestoreOperatorProvisionPlan,
    psql_path: PathBuf,
    psql_sha256: String,
    command_timeout_seconds: u64,
}

impl RestoreProvisionCommandConfig {
    fn load_private(config_path: &std::path::Path) -> Result<Self, &'static str> {
        if !config_path.is_absolute() {
            return Err("restore provision configuration rejected");
        }
        let config_bytes = px_private_files::private::read_private(config_path)
            .map_err(|_| "restore provision configuration rejected")?;
        let config = serde_json::from_slice::<Self>(&config_bytes)
            .map_err(|_| "restore provision configuration rejected")?;
        if !config.is_valid() {
            return Err("restore provision configuration rejected");
        }
        Ok(config)
    }

    fn is_valid(&self) -> bool {
        self.schema_version == RESTORE_PROVISION_COMMAND_CONFIG_SCHEMA_VERSION
            && self.plan.validate().is_ok()
            && self.psql_path.is_absolute()
            && valid_sha256(&self.psql_sha256)
            && (1..=24 * 60 * 60).contains(&self.command_timeout_seconds)
    }
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct WriteBarrierCommandConfig {
    schema_version: u32,
    plan: WriteBarrierCoordinatorPlan,
    psql_path: PathBuf,
    psql_sha256: String,
    command_timeout_seconds: u64,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct RecoverySealCommandConfig {
    schema_version: u32,
    repository_root: PathBuf,
    plan: RecoverySealPlan,
    psql_path: PathBuf,
    psql_sha256: String,
    command_timeout_seconds: u64,
}

impl RecoverySealCommandConfig {
    fn load_private(config_path: &std::path::Path) -> Result<Self, &'static str> {
        if !config_path.is_absolute() {
            return Err("recovery seal configuration rejected");
        }
        let config_bytes = px_private_files::private::read_private(config_path)
            .map_err(|_| "recovery seal configuration rejected")?;
        let config = serde_json::from_slice::<Self>(&config_bytes)
            .map_err(|_| "recovery seal configuration rejected")?;
        if !config.is_valid() {
            return Err("recovery seal configuration rejected");
        }
        Ok(config)
    }

    fn is_valid(&self) -> bool {
        self.schema_version == RECOVERY_SEAL_COMMAND_CONFIG_SCHEMA_VERSION
            && self.repository_root.is_absolute()
            && !self.plan.lock_file.starts_with(&self.repository_root)
            && !self.plan.marker_file.starts_with(&self.repository_root)
            && !self.plan.report_file.starts_with(&self.repository_root)
            && self.plan.validate().is_ok()
            && self.psql_path.is_absolute()
            && valid_sha256(&self.psql_sha256)
            && (1..=24 * 60 * 60).contains(&self.command_timeout_seconds)
    }
}

impl WriteBarrierCommandConfig {
    fn load_private(config_path: &std::path::Path) -> Result<Self, &'static str> {
        if !config_path.is_absolute() {
            return Err("write barrier configuration rejected");
        }
        let config_bytes = px_private_files::private::read_private(config_path)
            .map_err(|_| "write barrier configuration rejected")?;
        let config = serde_json::from_slice::<Self>(&config_bytes)
            .map_err(|_| "write barrier configuration rejected")?;
        if !config.is_valid() {
            return Err("write barrier configuration rejected");
        }
        Ok(config)
    }

    fn is_valid(&self) -> bool {
        self.schema_version == WRITE_BARRIER_COMMAND_CONFIG_SCHEMA_VERSION
            && self.plan.validate().is_ok()
            && self.psql_path.is_absolute()
            && valid_sha256(&self.psql_sha256)
            && (1..=24 * 60 * 60).contains(&self.command_timeout_seconds)
    }
}

impl RestoreApprovalRequest {
    fn load_private(request_path: &std::path::Path) -> Result<Self, &'static str> {
        if !request_path.is_absolute() {
            return Err("restore approval request rejected");
        }
        let request_bytes = px_private_files::private::read_private(request_path)
            .map_err(|_| "restore approval request rejected")?;
        let request = serde_json::from_slice::<Self>(&request_bytes)
            .map_err(|_| "restore approval request rejected")?;
        if !request.is_valid() {
            return Err("restore approval request rejected");
        }
        Ok(request)
    }

    fn is_valid(&self) -> bool {
        self.schema_version == RESTORE_APPROVAL_REQUEST_SCHEMA_VERSION
            && !self.approval_id.is_nil()
            && !self.administrator_id.is_nil()
            && self.expected_revision > 0
            && valid_sha256(&self.expected_evidence_sha256)
    }
}

pub fn evaluate(config_path: PathBuf) -> Result<(), &'static str> {
    let config = RestoreCommandConfig::load_private(&config_path)?;
    let current_time = super::now_unix()?;
    let manifest = load_verified_manifest(&config)?;
    load_verified_recovery_seal(&config, &manifest)?;
    let witness = ExternalRecoveryWitness::load_private(&config.witness_path)
        .map_err(|_| "external recovery witness rejected")?;
    let mut admission_store = RestoreAdmissionStore::open(
        &config.admission_root,
        config.deployment_id,
        config.recovery_set_id,
        config.target_environment_id,
        current_time,
    )
    .map_err(|_| "restore admission store rejected")?;
    admission_store
        .evaluate(&manifest, &witness, &config.completed_checks, current_time)
        .map_err(|_| "restore evidence rejected")?;
    print_record(admission_store.record())?;
    if admission_store.record().state == RestoreAdmissionState::RecoveryRequired {
        Err("restore remains RecoveryRequired")
    } else {
        Ok(())
    }
}

pub fn approve(config_path: PathBuf, request_path: PathBuf) -> Result<(), &'static str> {
    let config = RestoreCommandConfig::load_private(&config_path)?;
    let approval_request = RestoreApprovalRequest::load_private(&request_path)?;
    let current_time = super::now_unix()?;
    let manifest = load_verified_manifest(&config)?;
    load_verified_recovery_seal(&config, &manifest)?;
    let witness = ExternalRecoveryWitness::load_private(&config.witness_path)
        .map_err(|_| "external recovery witness rejected")?;
    let mut admission_store = RestoreAdmissionStore::open(
        &config.admission_root,
        config.deployment_id,
        config.recovery_set_id,
        config.target_environment_id,
        current_time,
    )
    .map_err(|_| "restore admission store rejected")?;
    admission_store
        .evaluate(&manifest, &witness, &config.completed_checks, current_time)
        .map_err(|_| "restore evidence rejected")?;
    admission_store
        .approve(
            approval_request.approval_id,
            approval_request.administrator_id,
            approval_request.expected_revision,
            &approval_request.expected_evidence_sha256,
            current_time,
        )
        .map_err(|_| "restore approval rejected")?;
    print_record(admission_store.record())
}

pub fn execute(config_path: PathBuf, cancellation: BackupCancellation) -> Result<(), &'static str> {
    let config = RestoreExecutionCommandConfig::load_private(&config_path)?;
    let tools = PinnedPgRestoreTools::new(
        config.createdb_path.clone(),
        config.createdb_sha256.clone(),
        config.pg_restore_path.clone(),
        config.pg_restore_sha256.clone(),
        config.psql_path.clone(),
        config.psql_sha256.clone(),
        Duration::from_secs(config.command_timeout_seconds),
        cancellation,
    )
    .map_err(|_| "restore execution tool identity rejected")?;
    let repository = BackupRepository::open(&config.repository_root, config.plan.deployment_id)
        .map_err(|_| "backup repository rejected restore execution")?;
    let report = RestoreRunner::new(tools)
        .run(&repository, &config.plan)
        .map_err(|_| "restore execution failed closed")?;
    let report_json =
        serde_json::to_vec(&report).map_err(|_| "restore result serialization failed")?;
    px_private_files::private::create_private(&config.report_path, &report_json)
        .map_err(|_| "restore execution report rejected")?;
    println!(
        "{}",
        String::from_utf8(report_json).map_err(|_| "restore result serialization failed")?
    );
    Ok(())
}

pub fn provision(
    config_path: PathBuf,
    cancellation: BackupCancellation,
) -> Result<(), &'static str> {
    let config = RestoreProvisionCommandConfig::load_private(&config_path)?;
    let provisioner = PinnedPgRestoreProvisioner::new(
        config.psql_path,
        config.psql_sha256,
        Duration::from_secs(config.command_timeout_seconds),
        cancellation,
    )
    .map_err(|_| "restore provision tool identity rejected")?;
    provisioner
        .provision(&config.plan)
        .map_err(|_| "restore operator provisioning failed closed")?;
    println!("restore operator provisioned and verified");
    Ok(())
}

pub fn acquire_write_barrier(
    config_path: PathBuf,
    cancellation: BackupCancellation,
) -> Result<(), &'static str> {
    let config = WriteBarrierCommandConfig::load_private(&config_path)?;
    let coordinator = PinnedPgWriteBarrierCoordinator::new(
        config.psql_path,
        config.psql_sha256,
        Duration::from_secs(config.command_timeout_seconds),
        cancellation,
    )
    .map_err(|_| "write barrier tool identity rejected")?;
    let proof = coordinator
        .acquire(&config.plan)
        .map_err(|_| "write barrier acquisition failed closed")?;
    println!(
        "write barrier acquired proof_id={} expires_at_unix={}",
        proof.consistency_proof_id, proof.expires_at_unix
    );
    Ok(())
}

pub fn release_write_barrier(
    config_path: PathBuf,
    cancellation: BackupCancellation,
) -> Result<(), &'static str> {
    let config = WriteBarrierCommandConfig::load_private(&config_path)?;
    let coordinator = PinnedPgWriteBarrierCoordinator::new(
        config.psql_path,
        config.psql_sha256,
        Duration::from_secs(config.command_timeout_seconds),
        cancellation,
    )
    .map_err(|_| "write barrier tool identity rejected")?;
    coordinator
        .release(&config.plan)
        .map_err(|_| "write barrier release requires reconciliation")?;
    println!("write barrier released");
    Ok(())
}

pub fn seal_recovery(
    config_path: PathBuf,
    cancellation: BackupCancellation,
) -> Result<(), &'static str> {
    let config = RecoverySealCommandConfig::load_private(&config_path)?;
    let repository = BackupRepository::open(&config.repository_root, config.plan.deployment_id)
        .map_err(|_| "backup repository rejected restore access")?;
    let manifest = repository
        .manifests()
        .map_err(|_| "backup repository rejected restore access")?
        .into_iter()
        .find(|manifest| manifest.recovery_set_id == config.plan.recovery_set_id)
        .ok_or("recovery set is unavailable")?;
    drop(repository);
    let tool = PinnedPgRecoverySealTool::new(
        config.psql_path,
        config.psql_sha256,
        Duration::from_secs(config.command_timeout_seconds),
        cancellation,
    )
    .map_err(|_| "recovery seal tool identity rejected")?;
    let report = RecoverySealRunner::new(tool)
        .seal(&manifest, &config.plan)
        .map_err(|_| "recovery seal failed closed")?;
    let report_json =
        serde_json::to_string(&report).map_err(|_| "restore result serialization failed")?;
    println!("{report_json}");
    Ok(())
}

fn load_verified_manifest(
    config: &RestoreCommandConfig,
) -> Result<RecoverySetManifest, &'static str> {
    let repository =
        px_backup::BackupRepository::open(&config.repository_root, config.deployment_id)
            .map_err(|_| "backup repository rejected restore access")?;
    repository
        .manifests()
        .map_err(|_| "backup repository rejected restore access")?
        .into_iter()
        .find(|manifest| manifest.recovery_set_id == config.recovery_set_id)
        .ok_or("recovery set is unavailable")
}

fn load_verified_recovery_seal(
    config: &RestoreCommandConfig,
    manifest: &RecoverySetManifest,
) -> Result<RecoverySealReport, &'static str> {
    let report = RecoverySealReport::load_private(&config.recovery_seal_report_path)
        .map_err(|_| "recovery seal evidence rejected")?;
    report
        .validate_for_restore(manifest, config.target_environment_id)
        .map_err(|_| "recovery seal evidence rejected")?;
    Ok(report)
}

fn print_record(record: &px_backup::RestoreAdmissionRecord) -> Result<(), &'static str> {
    let record_json =
        serde_json::to_string(record).map_err(|_| "restore result serialization failed")?;
    println!("{record_json}");
    Ok(())
}

fn valid_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

#[cfg(test)]
mod tests {
    use super::*;
    use px_backup::{
        BackupMember, BackupMemberState, BackupRepository, BackupService,
        RecoverySealServiceReport, RecoverySecurityEvidence, RecoverySetKind, RecoverySetManifest,
        RecoverySetStatus, RestoreDatabaseTarget, RestoreExecutionReport, RetentionClass,
        ServiceSecurityWatermark, MANIFEST_SCHEMA_VERSION, RECOVERY_SEAL_REPORT_SCHEMA_VERSION,
        RECOVERY_WITNESS_SCHEMA_VERSION,
    };
    use sha2::{Digest, Sha256};
    use std::{fs, path::Path};

    struct CommandFixture {
        _temporary_directory: tempfile::TempDir,
        config_path: PathBuf,
        approval_path: PathBuf,
        admission_root: PathBuf,
        private_root: PathBuf,
        config: RestoreCommandConfig,
    }

    impl CommandFixture {
        fn new() -> Self {
            let temporary_directory = tempfile::tempdir().unwrap();
            make_private_directory(temporary_directory.path());
            let repository_root = create_private_child(temporary_directory.path(), "repository");
            let admission_root = create_private_child(temporary_directory.path(), "admission");
            let private_root = create_private_child(temporary_directory.path(), "private");
            let deployment_id = Uuid::new_v4();
            let recovery_set_id = Uuid::new_v4();
            let services = [
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ];
            let watermarks = services
                .into_iter()
                .map(|service| ServiceSecurityWatermark {
                    service,
                    security_sequence: 23,
                    security_state_sha256: "c".repeat(64),
                })
                .collect::<Vec<_>>();
            let repository = BackupRepository::open(&repository_root, deployment_id).unwrap();
            let staged_recovery_set = repository.begin_set(recovery_set_id).unwrap();
            let archive_sha256 = format!("{:x}", Sha256::digest(b"archive"));
            for service in services {
                let archive_path = staged_recovery_set.prepare_archive(service).unwrap();
                fs::write(archive_path, b"archive").unwrap();
            }
            let manifest = RecoverySetManifest {
                schema_version: MANIFEST_SCHEMA_VERSION,
                recovery_set_id,
                deployment_id,
                kind: RecoverySetKind::WriteBarrier,
                status: RecoverySetStatus::Verified,
                created_at_unix: 100,
                completed_at_unix: Some(200),
                locked: false,
                restoring: false,
                retention: BTreeSet::from([RetentionClass::Hourly]),
                previous_recovery_set_id: None,
                members: services
                    .into_iter()
                    .map(|service| BackupMember {
                        service,
                        member: BackupMemberState::Required {
                            database: format!("pixels_{service:?}").to_ascii_lowercase(),
                            schema_version: 1,
                            archive_file: format!("{service:?}.dump").to_ascii_lowercase(),
                            archive_sha256: archive_sha256.clone(),
                            started_at_unix: 100,
                            completed_at_unix: 200,
                        },
                    })
                    .collect(),
                security_evidence: RecoverySecurityEvidence::Captured {
                    consistency_proof_id: Uuid::new_v4(),
                    external_key_ids: BTreeSet::from(["b".repeat(64)]),
                    watermarks: watermarks.clone(),
                },
                failure_code: None,
            };
            staged_recovery_set.publish(&manifest).unwrap();
            drop(repository);

            let witness = ExternalRecoveryWitness {
                schema_version: RECOVERY_WITNESS_SCHEMA_VERSION,
                deployment_id,
                witnessed_at_unix: 300,
                available_key_ids: BTreeSet::from(["b".repeat(64)]),
                watermarks: watermarks.clone(),
            };
            let witness_path = private_root.join("witness.json");
            px_private_files::private::create_private(
                &witness_path,
                &serde_json::to_vec(&witness).unwrap(),
            )
            .unwrap();
            let target_environment_id = Uuid::new_v4();
            let recovery_generation = Uuid::new_v4();
            let source_security_evidence_sha256 = format!(
                "{:x}",
                Sha256::digest(serde_json::to_vec(&manifest.security_evidence).unwrap())
            );
            let recovery_seal_report = RecoverySealReport {
                schema_version: RECOVERY_SEAL_REPORT_SCHEMA_VERSION,
                deployment_id,
                recovery_set_id,
                target_environment_id,
                recovery_generation,
                started_at_unix: 301,
                completed_at_unix: 302,
                source_security_evidence_sha256,
                services: watermarks
                    .iter()
                    .map(|watermark| {
                        let sealed_security_sequence = watermark.security_sequence + 1;
                        let state_material = format!(
                            "{}|{}|{}|{}",
                            deployment_id,
                            test_service_name(watermark.service),
                            recovery_generation,
                            sealed_security_sequence
                        );
                        RecoverySealServiceReport {
                            service: watermark.service,
                            source_security_sequence: watermark.security_sequence,
                            source_security_state_sha256: watermark.security_state_sha256.clone(),
                            sealed_security_sequence,
                            sealed_security_state_sha256: format!(
                                "{:x}",
                                Sha256::digest(state_material.as_bytes())
                            ),
                            revoked_session_records: 0,
                            invalidated_grant_records: 0,
                            invalidated_control_records: 0,
                        }
                    })
                    .collect(),
                old_sessions_revoked: true,
                old_grants_invalidated: true,
                pending_control_invalidated: true,
                admission_required: true,
            };
            let recovery_seal_report_path = private_root.join("recovery-seal-report.json");
            px_private_files::private::create_private(
                &recovery_seal_report_path,
                &serde_json::to_vec(&recovery_seal_report).unwrap(),
            )
            .unwrap();
            let config = RestoreCommandConfig {
                schema_version: RESTORE_COMMAND_CONFIG_SCHEMA_VERSION,
                deployment_id,
                recovery_set_id,
                target_environment_id,
                repository_root,
                admission_root: admission_root.clone(),
                witness_path,
                recovery_seal_report_path,
                completed_checks: all_operational_checks(),
            };
            let config_path = private_root.join("restore-config.json");
            px_private_files::private::create_private(
                &config_path,
                &serde_json::to_vec(&config).unwrap(),
            )
            .unwrap();
            Self {
                _temporary_directory: temporary_directory,
                config_path,
                approval_path: private_root.join("restore-approval.json"),
                admission_root,
                private_root,
                config,
            }
        }
    }

    fn all_operational_checks() -> BTreeSet<RestoreOperationalCheck> {
        BTreeSet::from([
            RestoreOperationalCheck::TargetNetworkIsolated,
            RestoreOperationalCheck::SideEffectsDisabled,
            RestoreOperationalCheck::RestoredIntoNewDatabases,
            RestoreOperationalCheck::DeploymentIdentityMatched,
            RestoreOperationalCheck::SchemaAndConstraintsVerified,
            RestoreOperationalCheck::BusinessSummariesVerified,
            RestoreOperationalCheck::PendingCommandsReconciled,
            RestoreOperationalCheck::ApplicationArtifactsVerified,
            RestoreOperationalCheck::NodeWorkspaceFactsReconciled,
        ])
    }

    fn test_service_name(service: BackupService) -> &'static str {
        match service {
            BackupService::Console => "console",
            BackupService::Auth => "auth",
            BackupService::Desk => "desk",
        }
    }

    fn create_private_child(parent: &Path, directory_name: &str) -> PathBuf {
        let child_path = parent.join(directory_name);
        fs::create_dir(&child_path).unwrap();
        make_private_directory(&child_path);
        child_path
    }

    fn make_private_directory(path: &Path) {
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

    fn isolated_database_name(target_environment_id: Uuid, service: BackupService) -> String {
        let environment_text = target_environment_id.simple().to_string();
        format!(
            "pixels_restore_{}_{}",
            &environment_text[..12],
            format!("{service:?}").to_ascii_lowercase()
        )
    }

    fn create_restore_tool(
        directory: &Path,
        tool_name: &str,
        verification_rows: &[(String, String)],
    ) -> PathBuf {
        #[cfg(windows)]
        let tool_path = directory.join(format!("{tool_name}.cmd"));
        #[cfg(unix)]
        let tool_path = directory.join(tool_name);
        #[cfg(windows)]
        let script = if tool_name == "psql" {
            let cases = verification_rows
                .iter()
                .map(|(database, row)| {
                    format!(
                        "echo %*| findstr /c:\"{database}\" >nul && (echo {}& exit /b 0)\r\n",
                        row.replace('|', "^|")
                    )
                })
                .collect::<String>();
            format!("@echo off\r\n{cases}exit /b 1\r\n")
        } else {
            "@echo off\r\nexit /b 0\r\n".to_string()
        };
        #[cfg(unix)]
        let script = if tool_name == "psql" {
            let cases = verification_rows
                .iter()
                .map(|(database, row)| {
                    format!("  *{database}*) printf '%s\\n' '{row}'; exit 0;;\n")
                })
                .collect::<String>();
            format!("#!/bin/sh\ncase \"$*\" in\n{cases}esac\nexit 1\n")
        } else {
            "#!/bin/sh\nexit 0\n".to_string()
        };
        fs::write(&tool_path, script).unwrap();
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&tool_path, fs::Permissions::from_mode(0o700)).unwrap();
        }
        tool_path
    }

    fn file_sha256(path: &Path) -> String {
        format!("{:x}", Sha256::digest(fs::read(path).unwrap()))
    }

    #[test]
    fn sha256_validation_rejects_uppercase_short_and_non_hex_values() {
        assert!(valid_sha256(&"a".repeat(64)));
        assert!(!valid_sha256(&"A".repeat(64)));
        assert!(!valid_sha256(&"a".repeat(63)));
        assert!(!valid_sha256(&"z".repeat(64)));
    }

    #[test]
    fn restore_configuration_requires_distinct_absolute_roots_and_exact_schema() {
        let absolute_root = std::env::current_dir().unwrap();
        let mut config = RestoreCommandConfig {
            schema_version: RESTORE_COMMAND_CONFIG_SCHEMA_VERSION,
            deployment_id: Uuid::new_v4(),
            recovery_set_id: Uuid::new_v4(),
            target_environment_id: Uuid::new_v4(),
            repository_root: absolute_root.join("repository"),
            admission_root: absolute_root.join("admission"),
            witness_path: absolute_root.join("witness.json"),
            recovery_seal_report_path: absolute_root.join("recovery-seal-report.json"),
            completed_checks: BTreeSet::new(),
        };
        assert!(config.is_valid());
        config.admission_root = config.repository_root.clone();
        assert!(!config.is_valid());
        config.admission_root = PathBuf::from("relative-admission");
        assert!(!config.is_valid());
        config.admission_root = absolute_root.join("admission");
        config.schema_version += 1;
        assert!(!config.is_valid());
    }

    #[test]
    fn approval_request_requires_exact_revision_identity_and_lowercase_digest() {
        let mut request = RestoreApprovalRequest {
            schema_version: RESTORE_APPROVAL_REQUEST_SCHEMA_VERSION,
            approval_id: Uuid::new_v4(),
            administrator_id: Uuid::new_v4(),
            expected_revision: 2,
            expected_evidence_sha256: "a".repeat(64),
        };
        assert!(request.is_valid());
        request.expected_revision = 0;
        assert!(!request.is_valid());
        request.expected_revision = 2;
        request.expected_evidence_sha256 = "A".repeat(64);
        assert!(!request.is_valid());
    }

    #[test]
    fn restore_execution_configuration_requires_private_fixed_inputs_and_external_report() {
        let absolute_root = std::env::current_dir().unwrap();
        let mut config = RestoreExecutionCommandConfig {
            schema_version: RESTORE_EXECUTION_COMMAND_CONFIG_SCHEMA_VERSION,
            repository_root: absolute_root.join("repository"),
            report_path: absolute_root.join("private").join("restore-report.json"),
            plan: RestoreExecutionPlan {
                deployment_id: Uuid::new_v4(),
                recovery_set_id: Uuid::new_v4(),
                target_environment_id: Uuid::new_v4(),
                targets: Vec::new(),
            },
            createdb_path: absolute_root.join("tools").join("createdb.exe"),
            createdb_sha256: "a".repeat(64),
            pg_restore_path: absolute_root.join("tools").join("pg_restore.exe"),
            pg_restore_sha256: "b".repeat(64),
            psql_path: absolute_root.join("tools").join("psql.exe"),
            psql_sha256: "c".repeat(64),
            command_timeout_seconds: 60,
        };
        assert!(config.is_valid());
        config.report_path = config.repository_root.join("report.json");
        assert!(!config.is_valid());
        config.report_path = absolute_root.join("private").join("restore-report.json");
        config.createdb_sha256 = "A".repeat(64);
        assert!(!config.is_valid());
        config.createdb_sha256 = "a".repeat(64);
        config.plan.target_environment_id = Uuid::nil();
        assert!(!config.is_valid());
    }

    #[test]
    fn restore_provision_configuration_requires_console_and_pinned_psql() {
        let absolute_root = std::env::current_dir().unwrap();
        let mut config = RestoreProvisionCommandConfig {
            schema_version: RESTORE_PROVISION_COMMAND_CONFIG_SCHEMA_VERSION,
            plan: RestoreOperatorProvisionPlan {
                host: "127.0.0.1".to_string(),
                port: 5432,
                admin_username: "pixels_admin".to_string(),
                admin_password_file: absolute_root.join("admin.pgpass"),
                restore_password_file: absolute_root.join("restore-password.secret"),
                services: BTreeSet::from([BackupService::Console]),
            },
            psql_path: absolute_root.join("tools").join("psql.exe"),
            psql_sha256: "a".repeat(64),
            command_timeout_seconds: 60,
        };
        assert!(config.is_valid());
        config.plan.services = BTreeSet::from([BackupService::Auth]);
        assert!(!config.is_valid());
        config.plan.services = BTreeSet::from([BackupService::Console]);
        config.psql_sha256 = "A".repeat(64);
        assert!(!config.is_valid());
    }

    #[test]
    fn write_barrier_configuration_requires_distinct_private_state_and_console_target() {
        let absolute_root = std::env::current_dir().unwrap();
        let target = px_backup::WriteBarrierDatabaseTarget {
            service: BackupService::Console,
            host: "127.0.0.1".to_string(),
            port: 5432,
            database: "pixels_console".to_string(),
            admin_username: "pixels_admin".to_string(),
            admin_password_file: absolute_root.join("admin.pgpass"),
        };
        let mut config = WriteBarrierCommandConfig {
            schema_version: WRITE_BARRIER_COMMAND_CONFIG_SCHEMA_VERSION,
            plan: WriteBarrierCoordinatorPlan {
                deployment_id: Uuid::new_v4(),
                proof_file: absolute_root.join("write-barrier-proof.json"),
                marker_file: absolute_root.join("write-barrier-marker.json"),
                lease_seconds: 120,
                external_key_ids: BTreeSet::new(),
                targets: vec![target],
            },
            psql_path: absolute_root.join("tools").join("psql.exe"),
            psql_sha256: "a".repeat(64),
            command_timeout_seconds: 60,
        };
        assert!(config.is_valid());
        config.plan.marker_file = config.plan.proof_file.clone();
        assert!(!config.is_valid());
        config.plan.marker_file = absolute_root.join("write-barrier-marker.json");
        config.plan.targets[0].service = BackupService::Auth;
        assert!(!config.is_valid());
    }

    #[test]
    fn restore_execute_runs_pinned_plan_and_persists_unadmitted_report() {
        let fixture = CommandFixture::new();
        let target_environment_id = fixture.config.target_environment_id;
        let password_file = fixture.private_root.join("restore.pgpass");
        px_private_files::private::create_private(&password_file, b"credential\n").unwrap();
        let targets = [
            BackupService::Console,
            BackupService::Auth,
            BackupService::Desk,
        ]
        .into_iter()
        .map(|service| {
            let service_name = format!("{service:?}").to_ascii_lowercase();
            RestoreDatabaseTarget {
                service,
                host: "127.0.0.1".to_string(),
                port: 5432,
                database: isolated_database_name(target_environment_id, service),
                owner: format!("pixels_{service_name}_owner"),
                username: "pixels_restore_operator".to_string(),
                password_file: password_file.clone(),
            }
        })
        .collect::<Vec<_>>();
        let verification_rows = targets
            .iter()
            .map(|target| {
                let service_name = format!("{:?}", target.service).to_ascii_lowercase();
                (
                    target.database.clone(),
                    format!(
                        "{}|{}|{}|{}|1|true",
                        service_name, fixture.config.deployment_id, target.database, target.owner
                    ),
                )
            })
            .collect::<Vec<_>>();
        let createdb = create_restore_tool(&fixture.private_root, "createdb", &verification_rows);
        let pg_restore =
            create_restore_tool(&fixture.private_root, "pg_restore", &verification_rows);
        let psql = create_restore_tool(&fixture.private_root, "psql", &verification_rows);
        let report_path = fixture.private_root.join("restore-execution-report.json");
        let execution_config = RestoreExecutionCommandConfig {
            schema_version: RESTORE_EXECUTION_COMMAND_CONFIG_SCHEMA_VERSION,
            repository_root: fixture.config.repository_root.clone(),
            report_path: report_path.clone(),
            plan: RestoreExecutionPlan {
                deployment_id: fixture.config.deployment_id,
                recovery_set_id: fixture.config.recovery_set_id,
                target_environment_id,
                targets,
            },
            createdb_path: createdb.clone(),
            createdb_sha256: file_sha256(&createdb),
            pg_restore_path: pg_restore.clone(),
            pg_restore_sha256: file_sha256(&pg_restore),
            psql_path: psql.clone(),
            psql_sha256: file_sha256(&psql),
            command_timeout_seconds: 5,
        };
        let execution_config_path = fixture.private_root.join("restore-execution-config.json");
        px_private_files::private::create_private(
            &execution_config_path,
            &serde_json::to_vec(&execution_config).unwrap(),
        )
        .unwrap();

        execute(execution_config_path, BackupCancellation::default()).unwrap();
        let report_bytes = px_private_files::private::read_private(&report_path).unwrap();
        let report = serde_json::from_slice::<RestoreExecutionReport>(&report_bytes).unwrap();
        assert_eq!(report.deployment_id, fixture.config.deployment_id);
        assert_eq!(report.recovery_set_id, fixture.config.recovery_set_id);
        assert_eq!(report.target_environment_id, target_environment_id);
        assert_eq!(report.members.len(), 3);
        assert!(report.admission_required);
    }

    #[test]
    fn restore_commands_reverify_repository_and_evidence_before_persisting_approval() {
        let fixture = CommandFixture::new();
        evaluate(fixture.config_path.clone()).unwrap();
        let ready_record = read_persisted_record(&fixture.admission_root);
        assert_eq!(
            ready_record.state,
            RestoreAdmissionState::ReadyForManualApproval
        );
        let approval_request = RestoreApprovalRequest {
            schema_version: RESTORE_APPROVAL_REQUEST_SCHEMA_VERSION,
            approval_id: Uuid::new_v4(),
            administrator_id: Uuid::new_v4(),
            expected_revision: ready_record.revision,
            expected_evidence_sha256: ready_record.evidence_sha256.unwrap(),
        };
        px_private_files::private::create_private(
            &fixture.approval_path,
            &serde_json::to_vec(&approval_request).unwrap(),
        )
        .unwrap();
        approve(fixture.config_path, fixture.approval_path).unwrap();
        let admitted_record = read_persisted_record(&fixture.admission_root);
        assert_eq!(admitted_record.state, RestoreAdmissionState::Admitted);
        assert_eq!(admitted_record.deployment_id, fixture.config.deployment_id);
        assert_eq!(
            admitted_record.recovery_set_id,
            fixture.config.recovery_set_id
        );
    }

    #[test]
    fn restore_admission_requires_an_exact_command_generated_recovery_seal() {
        let fixture = CommandFixture::new();
        fs::remove_file(&fixture.config.recovery_seal_report_path).unwrap();
        assert_eq!(
            evaluate(fixture.config_path.clone()),
            Err("recovery seal evidence rejected")
        );

        let second_fixture = CommandFixture::new();
        let report_bytes = px_private_files::private::read_private(
            &second_fixture.config.recovery_seal_report_path,
        )
        .unwrap();
        let mut report = serde_json::from_slice::<RecoverySealReport>(&report_bytes).unwrap();
        report.target_environment_id = Uuid::new_v4();
        fs::remove_file(&second_fixture.config.recovery_seal_report_path).unwrap();
        px_private_files::private::create_private(
            &second_fixture.config.recovery_seal_report_path,
            &serde_json::to_vec(&report).unwrap(),
        )
        .unwrap();
        assert_eq!(
            evaluate(second_fixture.config_path),
            Err("recovery seal evidence rejected")
        );
    }

    fn read_persisted_record(admission_root: &Path) -> px_backup::RestoreAdmissionRecord {
        let record_bytes =
            px_private_files::private::read_private(&admission_root.join("admission.json"))
                .unwrap();
        serde_json::from_slice(&record_bytes).unwrap()
    }
}
