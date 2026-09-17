use crate::{
    BackupService, RecoverySecurityEvidence, RecoverySetKind, RecoverySetManifest,
    ServiceSecurityWatermark,
};
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeMap, BTreeSet},
    path::Path,
};
use uuid::Uuid;

pub const RECOVERY_WITNESS_SCHEMA_VERSION: u32 = 1;
const MAX_RECOVERY_WITNESS_BYTES: usize = 64 * 1024;

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum RestoreAdmissionError {
    #[error("recovery manifest is invalid")]
    InvalidManifest,
    #[error("external recovery witness is invalid")]
    InvalidWitness,
    #[error("private external recovery witness is unavailable")]
    WitnessUnavailable,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ExternalRecoveryWitness {
    pub schema_version: u32,
    pub deployment_id: Uuid,
    pub witnessed_at_unix: u64,
    pub available_key_ids: BTreeSet<String>,
    pub watermarks: Vec<ServiceSecurityWatermark>,
}

impl ExternalRecoveryWitness {
    pub fn load_private(path: &Path) -> Result<Self, RestoreAdmissionError> {
        if !path.is_absolute() {
            return Err(RestoreAdmissionError::InvalidWitness);
        }
        let witness_bytes = px_private_files::private::read_private(path)
            .map_err(|_| RestoreAdmissionError::WitnessUnavailable)?;
        if witness_bytes.is_empty() || witness_bytes.len() > MAX_RECOVERY_WITNESS_BYTES {
            return Err(RestoreAdmissionError::InvalidWitness);
        }
        let witness = serde_json::from_slice::<Self>(&witness_bytes)
            .map_err(|_| RestoreAdmissionError::InvalidWitness)?;
        witness.validate()?;
        Ok(witness)
    }

    fn validate(&self) -> Result<(), RestoreAdmissionError> {
        let service_set = self
            .watermarks
            .iter()
            .map(|watermark| watermark.service)
            .collect::<BTreeSet<_>>();
        if self.schema_version != RECOVERY_WITNESS_SCHEMA_VERSION
            || self.deployment_id.is_nil()
            || self.witnessed_at_unix == 0
            || service_set.len() != self.watermarks.len()
            || self
                .available_key_ids
                .iter()
                .any(|key_id| !valid_sha256(key_id))
            || self.watermarks.iter().any(|watermark| {
                watermark.security_sequence == 0 || !valid_sha256(&watermark.security_state_sha256)
            })
        {
            return Err(RestoreAdmissionError::InvalidWitness);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RestoreOperationalCheck {
    TargetNetworkIsolated,
    SideEffectsDisabled,
    RestoredIntoNewDatabases,
    DeploymentIdentityMatched,
    SchemaAndConstraintsVerified,
    BusinessSummariesVerified,
    PendingCommandsReconciled,
    ApplicationArtifactsVerified,
    NodeWorkspaceFactsReconciled,
}

impl RestoreOperationalCheck {
    fn required() -> BTreeSet<Self> {
        BTreeSet::from([
            Self::TargetNetworkIsolated,
            Self::SideEffectsDisabled,
            Self::RestoredIntoNewDatabases,
            Self::DeploymentIdentityMatched,
            Self::SchemaAndConstraintsVerified,
            Self::BusinessSummariesVerified,
            Self::PendingCommandsReconciled,
            Self::ApplicationArtifactsVerified,
            Self::NodeWorkspaceFactsReconciled,
        ])
    }
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(tag = "reason", rename_all = "snake_case")]
pub enum RestoreAdmissionBlocker {
    IndependentRecoverySet,
    SecurityEvidenceUnavailable,
    WitnessDeploymentMismatch,
    WitnessMissingService { service: BackupService },
    WitnessBehindBackup { service: BackupService },
    WitnessAheadOfBackup { service: BackupService },
    WitnessStateMismatch { service: BackupService },
    RequiredKeyUnavailable { key_id: String },
    OperationalCheckMissing { check: RestoreOperationalCheck },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "decision", rename_all = "snake_case")]
pub enum RestoreAdmissionDecision {
    RecoveryRequired {
        blockers: BTreeSet<RestoreAdmissionBlocker>,
    },
    ReadyForManualApproval,
}

pub fn evaluate_restore_admission(
    manifest: &RecoverySetManifest,
    witness: &ExternalRecoveryWitness,
    completed_checks: &BTreeSet<RestoreOperationalCheck>,
) -> Result<RestoreAdmissionDecision, RestoreAdmissionError> {
    manifest
        .validate()
        .map_err(|_| RestoreAdmissionError::InvalidManifest)?;
    witness.validate()?;
    let mut blockers = BTreeSet::new();
    if manifest.kind == RecoverySetKind::Independent {
        blockers.insert(RestoreAdmissionBlocker::IndependentRecoverySet);
    }
    if witness.deployment_id != manifest.deployment_id {
        blockers.insert(RestoreAdmissionBlocker::WitnessDeploymentMismatch);
    }
    match &manifest.security_evidence {
        RecoverySecurityEvidence::Unavailable { .. } => {
            blockers.insert(RestoreAdmissionBlocker::SecurityEvidenceUnavailable);
        }
        RecoverySecurityEvidence::Captured {
            external_key_ids,
            watermarks,
            ..
        } => {
            compare_security_watermarks(watermarks, witness, &mut blockers);
            for key_id in external_key_ids.difference(&witness.available_key_ids) {
                blockers.insert(RestoreAdmissionBlocker::RequiredKeyUnavailable {
                    key_id: key_id.clone(),
                });
            }
        }
    }
    for required_check in RestoreOperationalCheck::required().difference(completed_checks) {
        blockers.insert(RestoreAdmissionBlocker::OperationalCheckMissing {
            check: *required_check,
        });
    }
    if blockers.is_empty() {
        Ok(RestoreAdmissionDecision::ReadyForManualApproval)
    } else {
        Ok(RestoreAdmissionDecision::RecoveryRequired { blockers })
    }
}

fn compare_security_watermarks(
    backup_watermarks: &[ServiceSecurityWatermark],
    witness: &ExternalRecoveryWitness,
    blockers: &mut BTreeSet<RestoreAdmissionBlocker>,
) {
    let witness_by_service = witness
        .watermarks
        .iter()
        .map(|watermark| (watermark.service, watermark))
        .collect::<BTreeMap<_, _>>();
    for backup_watermark in backup_watermarks {
        let Some(witness_watermark) = witness_by_service.get(&backup_watermark.service) else {
            blockers.insert(RestoreAdmissionBlocker::WitnessMissingService {
                service: backup_watermark.service,
            });
            continue;
        };
        if witness_watermark.security_sequence < backup_watermark.security_sequence {
            blockers.insert(RestoreAdmissionBlocker::WitnessBehindBackup {
                service: backup_watermark.service,
            });
        } else if witness_watermark.security_sequence > backup_watermark.security_sequence {
            blockers.insert(RestoreAdmissionBlocker::WitnessAheadOfBackup {
                service: backup_watermark.service,
            });
        } else if witness_watermark.security_state_sha256 != backup_watermark.security_state_sha256
        {
            blockers.insert(RestoreAdmissionBlocker::WitnessStateMismatch {
                service: backup_watermark.service,
            });
        }
    }
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
    use crate::{
        BackupMember, BackupMemberState, RecoverySetStatus, RetentionClass,
        ServiceSecurityWatermark, MANIFEST_SCHEMA_VERSION,
    };

    fn make_private_directory(path: &Path) {
        #[cfg(unix)]
        {
            use std::{fs, os::unix::fs::PermissionsExt};
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

    fn coordinated_manifest() -> RecoverySetManifest {
        let services = [
            BackupService::Console,
            BackupService::Auth,
            BackupService::Desk,
        ];
        RecoverySetManifest {
            schema_version: MANIFEST_SCHEMA_VERSION,
            recovery_set_id: Uuid::new_v4(),
            deployment_id: Uuid::new_v4(),
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
                        archive_sha256: "a".repeat(64),
                        started_at_unix: 100,
                        completed_at_unix: 200,
                    },
                })
                .collect(),
            security_evidence: RecoverySecurityEvidence::Captured {
                consistency_proof_id: Uuid::new_v4(),
                external_key_ids: BTreeSet::from(["b".repeat(64)]),
                watermarks: services
                    .into_iter()
                    .map(|service| ServiceSecurityWatermark {
                        service,
                        security_sequence: 17,
                        security_state_sha256: "c".repeat(64),
                    })
                    .collect(),
            },
            failure_code: None,
        }
    }

    fn matching_witness(manifest: &RecoverySetManifest) -> ExternalRecoveryWitness {
        let RecoverySecurityEvidence::Captured {
            external_key_ids,
            watermarks,
            ..
        } = &manifest.security_evidence
        else {
            panic!("coordinated test manifest must carry captured evidence");
        };
        ExternalRecoveryWitness {
            schema_version: RECOVERY_WITNESS_SCHEMA_VERSION,
            deployment_id: manifest.deployment_id,
            witnessed_at_unix: 300,
            available_key_ids: external_key_ids.clone(),
            watermarks: watermarks.clone(),
        }
    }

    #[test]
    fn exact_external_witness_and_all_operational_checks_only_reach_manual_approval() {
        let manifest = coordinated_manifest();
        let witness = matching_witness(&manifest);
        assert_eq!(
            evaluate_restore_admission(&manifest, &witness, &RestoreOperationalCheck::required()),
            Ok(RestoreAdmissionDecision::ReadyForManualApproval)
        );
    }

    #[test]
    fn independent_backup_and_missing_checks_remain_recovery_required() {
        let mut manifest = coordinated_manifest();
        manifest.kind = RecoverySetKind::Independent;
        manifest.security_evidence = RecoverySecurityEvidence::Unavailable {
            reason: crate::RecoveryEvidenceUnavailableReason::IndependentBackup,
        };
        let mut witness = matching_witness(&coordinated_manifest());
        witness.deployment_id = manifest.deployment_id;
        let decision = evaluate_restore_admission(&manifest, &witness, &BTreeSet::new()).unwrap();
        let RestoreAdmissionDecision::RecoveryRequired { blockers } = decision else {
            panic!("independent backup must never become admissible");
        };
        assert!(blockers.contains(&RestoreAdmissionBlocker::IndependentRecoverySet));
        assert!(blockers.contains(&RestoreAdmissionBlocker::SecurityEvidenceUnavailable));
        assert_eq!(
            blockers
                .iter()
                .filter(|blocker| matches!(
                    blocker,
                    RestoreAdmissionBlocker::OperationalCheckMissing { .. }
                ))
                .count(),
            RestoreOperationalCheck::required().len()
        );
    }

    #[test]
    fn newer_revocation_witness_and_missing_key_fail_closed() {
        let manifest = coordinated_manifest();
        let mut witness = matching_witness(&manifest);
        witness.available_key_ids.clear();
        let console_watermark = witness
            .watermarks
            .iter_mut()
            .find(|watermark| watermark.service == BackupService::Console)
            .unwrap();
        console_watermark.security_sequence += 1;
        let decision =
            evaluate_restore_admission(&manifest, &witness, &RestoreOperationalCheck::required())
                .unwrap();
        let RestoreAdmissionDecision::RecoveryRequired { blockers } = decision else {
            panic!("newer external security state must require reconciliation");
        };
        assert!(
            blockers.contains(&RestoreAdmissionBlocker::WitnessAheadOfBackup {
                service: BackupService::Console,
            })
        );
        assert!(
            blockers.contains(&RestoreAdmissionBlocker::RequiredKeyUnavailable {
                key_id: "b".repeat(64),
            })
        );
    }

    #[test]
    fn private_witness_rejects_unknown_fields_and_relative_paths() {
        let manifest = coordinated_manifest();
        let witness = matching_witness(&manifest);
        assert_eq!(
            ExternalRecoveryWitness::load_private(Path::new("witness.json")),
            Err(RestoreAdmissionError::InvalidWitness)
        );
        let temporary_directory = tempfile::tempdir().unwrap();
        make_private_directory(temporary_directory.path());
        let witness_path = temporary_directory.path().join("witness.json");
        let mut witness_value = serde_json::to_value(witness).unwrap();
        witness_value["legacy_sequence"] = serde_json::json!(9);
        px_private_files::private::create_private(
            &witness_path,
            &serde_json::to_vec(&witness_value).unwrap(),
        )
        .unwrap();
        assert_eq!(
            ExternalRecoveryWitness::load_private(&witness_path),
            Err(RestoreAdmissionError::InvalidWitness)
        );
    }
}
