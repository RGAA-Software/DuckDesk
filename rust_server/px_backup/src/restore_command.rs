use px_backup::{
    ExternalRecoveryWitness, RecoverySetManifest, RestoreAdmissionState, RestoreAdmissionStore,
    RestoreOperationalCheck,
};
use serde::{Deserialize, Serialize};
use std::{collections::BTreeSet, path::PathBuf};
use uuid::Uuid;

const RESTORE_COMMAND_CONFIG_SCHEMA_VERSION: u32 = 1;
const RESTORE_APPROVAL_REQUEST_SCHEMA_VERSION: u32 = 1;

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
            && self.repository_root != self.admission_root
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
        BackupMember, BackupMemberState, BackupRepository, BackupService, RecoverySecurityEvidence,
        RecoverySetKind, RecoverySetManifest, RecoverySetStatus, RetentionClass,
        ServiceSecurityWatermark, MANIFEST_SCHEMA_VERSION, RECOVERY_WITNESS_SCHEMA_VERSION,
    };
    use sha2::{Digest, Sha256};
    use std::{fs, path::Path};

    struct CommandFixture {
        _temporary_directory: tempfile::TempDir,
        config_path: PathBuf,
        approval_path: PathBuf,
        admission_root: PathBuf,
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
                watermarks,
            };
            let witness_path = private_root.join("witness.json");
            px_private_files::private::create_private(
                &witness_path,
                &serde_json::to_vec(&witness).unwrap(),
            )
            .unwrap();
            let config = RestoreCommandConfig {
                schema_version: RESTORE_COMMAND_CONFIG_SCHEMA_VERSION,
                deployment_id,
                recovery_set_id,
                target_environment_id: Uuid::new_v4(),
                repository_root,
                admission_root: admission_root.clone(),
                witness_path,
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

    fn read_persisted_record(admission_root: &Path) -> px_backup::RestoreAdmissionRecord {
        let record_bytes =
            px_private_files::private::read_private(&admission_root.join("admission.json"))
                .unwrap();
        serde_json::from_slice(&record_bytes).unwrap()
    }
}
