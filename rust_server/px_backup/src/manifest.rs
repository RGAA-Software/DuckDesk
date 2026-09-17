use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;
use uuid::Uuid;

pub const MANIFEST_SCHEMA_VERSION: u32 = 3;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum BackupService {
    Console,
    Auth,
    Desk,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RecoverySetKind {
    Independent,
    WriteBarrier,
    Physical,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RecoverySetStatus {
    Created,
    Verified,
    OffsiteVerified,
    RestoreTested,
    Failed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RecoveryEvidenceUnavailableReason {
    IndependentBackup,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ServiceSecurityWatermark {
    pub service: BackupService,
    pub recovery_generation: Uuid,
    pub security_sequence: u64,
    pub security_state_sha256: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "state", rename_all = "snake_case", deny_unknown_fields)]
pub enum RecoverySecurityEvidence {
    Unavailable {
        reason: RecoveryEvidenceUnavailableReason,
    },
    Captured {
        consistency_proof_id: Uuid,
        external_key_ids: BTreeSet<String>,
        watermarks: Vec<ServiceSecurityWatermark>,
    },
}

impl RecoverySetStatus {
    pub fn is_verified(self) -> bool {
        matches!(
            self,
            Self::Verified | Self::OffsiteVerified | Self::RestoreTested
        )
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "state", rename_all = "snake_case", deny_unknown_fields)]
pub enum BackupMemberState {
    Required {
        database: String,
        schema_version: u32,
        archive_file: String,
        archive_sha256: String,
        started_at_unix: u64,
        completed_at_unix: u64,
    },
    NotApplicable {
        reason: String,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BackupMember {
    pub service: BackupService,
    pub member: BackupMemberState,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RecoverySetManifest {
    pub schema_version: u32,
    pub recovery_set_id: Uuid,
    pub deployment_id: Uuid,
    pub kind: RecoverySetKind,
    pub status: RecoverySetStatus,
    pub created_at_unix: u64,
    pub completed_at_unix: Option<u64>,
    pub locked: bool,
    pub restoring: bool,
    pub retention: BTreeSet<crate::RetentionClass>,
    pub previous_recovery_set_id: Option<Uuid>,
    pub members: Vec<BackupMember>,
    pub security_evidence: RecoverySecurityEvidence,
    pub failure_code: Option<String>,
}

impl RecoverySetManifest {
    pub fn validate(&self) -> Result<(), &'static str> {
        if self.schema_version != MANIFEST_SCHEMA_VERSION
            || self.recovery_set_id.is_nil()
            || self.deployment_id.is_nil()
            || self.created_at_unix == 0
        {
            return Err("invalid recovery-set identity");
        }
        if self.previous_recovery_set_id == Some(self.recovery_set_id) {
            return Err("recovery set cannot depend on itself");
        }
        let services = self
            .members
            .iter()
            .map(|member| member.service)
            .collect::<BTreeSet<_>>();
        if services
            != BTreeSet::from([
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ])
            || self.members.len() != 3
        {
            return Err("manifest must contain each service exactly once");
        }
        for member in &self.members {
            match &member.member {
                BackupMemberState::Required {
                    database,
                    schema_version,
                    archive_file,
                    archive_sha256,
                    started_at_unix,
                    completed_at_unix,
                } => {
                    if !valid_identifier(database)
                        || *schema_version == 0
                        || !valid_archive_name(archive_file)
                        || !valid_sha256(archive_sha256)
                        || *started_at_unix == 0
                        || completed_at_unix < started_at_unix
                    {
                        return Err("invalid required backup member");
                    }
                }
                BackupMemberState::NotApplicable { reason } => {
                    if member.service == BackupService::Console
                        || reason.trim().is_empty()
                        || reason.len() > 256
                    {
                        return Err("invalid not-applicable member");
                    }
                }
            }
        }
        let required_services = self
            .members
            .iter()
            .filter_map(|member| match member.member {
                BackupMemberState::Required { .. } => Some(member.service),
                BackupMemberState::NotApplicable { .. } => None,
            })
            .collect::<BTreeSet<_>>();
        match (&self.kind, &self.security_evidence) {
            (
                RecoverySetKind::Independent,
                RecoverySecurityEvidence::Unavailable {
                    reason: RecoveryEvidenceUnavailableReason::IndependentBackup,
                },
            ) => {}
            (
                RecoverySetKind::WriteBarrier | RecoverySetKind::Physical,
                RecoverySecurityEvidence::Captured {
                    consistency_proof_id,
                    external_key_ids,
                    watermarks,
                },
            ) => {
                let watermark_services = watermarks
                    .iter()
                    .map(|watermark| watermark.service)
                    .collect::<BTreeSet<_>>();
                if consistency_proof_id.is_nil()
                    || watermark_services != required_services
                    || watermarks.len() != required_services.len()
                    || external_key_ids.iter().any(|key_id| !valid_sha256(key_id))
                    || watermarks.iter().any(|watermark| {
                        watermark.recovery_generation.is_nil()
                            || watermark.security_sequence == 0
                            || !valid_sha256(&watermark.security_state_sha256)
                    })
                {
                    return Err("invalid coordinated recovery security evidence");
                }
            }
            _ => return Err("recovery-set kind and security evidence disagree"),
        }
        match self.status {
            RecoverySetStatus::Created => {
                if self.completed_at_unix.is_some() || self.failure_code.is_some() {
                    return Err("created set cannot be completed or failed");
                }
            }
            RecoverySetStatus::Failed => {
                if self.failure_code.as_deref().is_none_or(|value| {
                    value.is_empty()
                        || value.len() > 64
                        || !value
                            .bytes()
                            .all(|byte| byte.is_ascii_uppercase() || byte == b'_')
                }) {
                    return Err("failed set requires a bounded typed failure code");
                }
            }
            _ => {
                if self.completed_at_unix.is_none()
                    || self
                        .completed_at_unix
                        .is_some_and(|completed| completed < self.created_at_unix)
                    || self.failure_code.is_some()
                {
                    return Err("verified set requires completion without failure");
                }
            }
        }
        if self.retention.is_empty() {
            return Err("recovery set requires an explicit retention class");
        }
        Ok(())
    }
}

fn valid_identifier(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 63
        && value
            .bytes()
            .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
}

fn valid_archive_name(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 96
        && !value.contains("..")
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'))
        && value.ends_with(".dump")
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
    use crate::RetentionClass;

    fn manifest() -> RecoverySetManifest {
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
            members: [
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ]
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
                external_key_ids: BTreeSet::new(),
                watermarks: [
                    BackupService::Console,
                    BackupService::Auth,
                    BackupService::Desk,
                ]
                .into_iter()
                .map(|service| ServiceSecurityWatermark {
                    service,
                    recovery_generation: Uuid::new_v4(),
                    security_sequence: 7,
                    security_state_sha256: "b".repeat(64),
                })
                .collect(),
            },
            failure_code: None,
        }
    }

    #[test]
    fn strict_manifest_accepts_complete_set_and_rejects_missing_duplicate_or_unsafe_members() {
        let value = manifest();
        assert_eq!(value.validate(), Ok(()));

        let mut missing = value.clone();
        missing.members.pop();
        assert!(missing.validate().is_err());

        let mut duplicate = value.clone();
        duplicate.members[2].service = BackupService::Auth;
        assert!(duplicate.validate().is_err());

        let mut unsafe_path = value.clone();
        if let BackupMemberState::Required { archive_file, .. } = &mut unsafe_path.members[0].member
        {
            *archive_file = "../console.dump".into();
        }
        assert!(unsafe_path.validate().is_err());
    }

    #[test]
    fn console_cannot_be_not_applicable_and_status_transitions_are_explicit() {
        let mut value = manifest();
        value.members[0].member = BackupMemberState::NotApplicable {
            reason: "not installed".into(),
        };
        assert!(value.validate().is_err());

        let mut failed = manifest();
        failed.status = RecoverySetStatus::Failed;
        failed.completed_at_unix = Some(200);
        failed.failure_code = Some("ARCHIVE_FAILED".into());
        assert_eq!(failed.validate(), Ok(()));
        failed.failure_code = Some("archive failed: password=secret".into());
        assert!(failed.validate().is_err());
    }

    #[test]
    fn unknown_fields_and_unknown_states_are_rejected() {
        let value = serde_json::to_value(manifest()).unwrap();
        let mut object = value.as_object().unwrap().clone();
        object.insert("legacy_path".into(), serde_json::json!("ignored"));
        assert!(serde_json::from_value::<RecoverySetManifest>(object.into()).is_err());

        let mut value = serde_json::to_value(manifest()).unwrap();
        value["status"] = serde_json::json!("complete");
        assert!(serde_json::from_value::<RecoverySetManifest>(value).is_err());
    }

    #[test]
    fn completion_cannot_precede_recovery_set_creation() {
        let mut value = manifest();
        value.completed_at_unix = Some(value.created_at_unix - 1);
        assert!(value.validate().is_err());
    }

    #[test]
    fn independent_and_coordinated_sets_require_explicit_security_evidence() {
        let mut independent_manifest = manifest();
        independent_manifest.kind = RecoverySetKind::Independent;
        assert!(independent_manifest.validate().is_err());
        independent_manifest.security_evidence = RecoverySecurityEvidence::Unavailable {
            reason: RecoveryEvidenceUnavailableReason::IndependentBackup,
        };
        assert_eq!(independent_manifest.validate(), Ok(()));

        let mut coordinated_manifest = manifest();
        let RecoverySecurityEvidence::Captured { watermarks, .. } =
            &mut coordinated_manifest.security_evidence
        else {
            panic!("coordinated test manifest must carry captured evidence");
        };
        watermarks.pop();
        assert!(coordinated_manifest.validate().is_err());
    }
}
