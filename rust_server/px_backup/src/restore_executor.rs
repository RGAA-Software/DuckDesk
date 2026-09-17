use crate::{BackupMemberState, BackupRepository, BackupService, RepositoryError};
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeMap, BTreeSet},
    net::IpAddr,
    path::{Path, PathBuf},
};
use uuid::Uuid;

pub const RESTORE_EXECUTION_REPORT_SCHEMA_VERSION: u32 = 1;

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum RestoreExecutionError {
    #[error("restore execution plan is invalid")]
    InvalidPlan,
    #[error("verified recovery set is unavailable")]
    RecoverySetUnavailable,
    #[error("backup repository rejected restore execution")]
    Repository,
    #[error("fresh isolated target database creation failed")]
    TargetCreationFailed,
    #[error("database archive restore failed")]
    RestoreFailed,
    #[error("restored database identity or schema verification failed")]
    VerificationFailed,
    #[error("system clock is unavailable")]
    Clock,
}

impl From<RepositoryError> for RestoreExecutionError {
    fn from(_: RepositoryError) -> Self {
        Self::Repository
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreDatabaseTarget {
    pub service: BackupService,
    pub host: String,
    pub port: u16,
    pub database: String,
    pub owner: String,
    pub username: String,
    pub password_file: PathBuf,
}

impl RestoreDatabaseTarget {
    fn validate(&self, target_environment_id: Uuid) -> Result<(), RestoreExecutionError> {
        let expected_database = isolated_database_name(target_environment_id, self.service);
        let expected_owner = format!("pixels_{}_owner", service_name(self.service));
        if self.port == 0
            || !valid_host(&self.host)
            || self.database != expected_database
            || self.owner != expected_owner
            || self.username != "pixels_restore_operator"
            || !self.password_file.is_absolute()
        {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreExecutionPlan {
    pub deployment_id: Uuid,
    pub recovery_set_id: Uuid,
    pub target_environment_id: Uuid,
    pub targets: Vec<RestoreDatabaseTarget>,
}

impl RestoreExecutionPlan {
    fn validate(&self) -> Result<(), RestoreExecutionError> {
        if self.deployment_id.is_nil()
            || self.recovery_set_id.is_nil()
            || self.target_environment_id.is_nil()
        {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        let services = self
            .targets
            .iter()
            .map(|target| target.service)
            .collect::<BTreeSet<_>>();
        if services.len() != self.targets.len() {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        for target in &self.targets {
            target.validate(self.target_environment_id)?;
        }
        Ok(())
    }
}

pub trait LogicalRestoreTool {
    fn create_fresh_database(
        &self,
        target: &RestoreDatabaseTarget,
    ) -> Result<(), RestoreExecutionError>;
    fn restore_archive(
        &self,
        target: &RestoreDatabaseTarget,
        archive_path: &Path,
    ) -> Result<(), RestoreExecutionError>;
    fn verify_restored_database(
        &self,
        target: &RestoreDatabaseTarget,
        expected_deployment_id: Uuid,
        expected_schema_version: u32,
    ) -> Result<(), RestoreExecutionError>;
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "state", rename_all = "snake_case", deny_unknown_fields)]
pub enum RestoredMember {
    Restored {
        service: BackupService,
        database: String,
        archive_sha256: String,
        schema_version: u32,
    },
    NotApplicable {
        service: BackupService,
        reason: String,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreExecutionReport {
    pub schema_version: u32,
    pub deployment_id: Uuid,
    pub recovery_set_id: Uuid,
    pub target_environment_id: Uuid,
    pub started_at_unix: u64,
    pub completed_at_unix: u64,
    pub members: Vec<RestoredMember>,
    pub admission_required: bool,
}

pub struct RestoreRunner<T> {
    tool: T,
}

impl<T: LogicalRestoreTool> RestoreRunner<T> {
    pub fn new(tool: T) -> Self {
        Self { tool }
    }

    pub fn run(
        &self,
        repository: &BackupRepository,
        plan: &RestoreExecutionPlan,
    ) -> Result<RestoreExecutionReport, RestoreExecutionError> {
        plan.validate()?;
        if repository.deployment_id() != plan.deployment_id {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        let manifest = repository
            .manifests()?
            .into_iter()
            .find(|manifest| {
                manifest.recovery_set_id == plan.recovery_set_id && manifest.status.is_verified()
            })
            .ok_or(RestoreExecutionError::RecoverySetUnavailable)?;
        let required_services = manifest
            .members
            .iter()
            .filter_map(|member| match member.member {
                BackupMemberState::Required { .. } => Some(member.service),
                BackupMemberState::NotApplicable { .. } => None,
            })
            .collect::<BTreeSet<_>>();
        let targets_by_service = plan
            .targets
            .iter()
            .map(|target| (target.service, target))
            .collect::<BTreeMap<_, _>>();
        if targets_by_service.keys().copied().collect::<BTreeSet<_>>() != required_services {
            return Err(RestoreExecutionError::InvalidPlan);
        }
        let started_at_unix = current_unix_time()?;
        let recovery_set_directory = repository.root().join(plan.recovery_set_id.to_string());
        let mut restored_members = Vec::with_capacity(manifest.members.len());
        for member in &manifest.members {
            match &member.member {
                BackupMemberState::Required {
                    database: source_database,
                    schema_version,
                    archive_file,
                    archive_sha256,
                    ..
                } => {
                    let target = targets_by_service
                        .get(&member.service)
                        .copied()
                        .ok_or(RestoreExecutionError::InvalidPlan)?;
                    if target.database == *source_database {
                        return Err(RestoreExecutionError::InvalidPlan);
                    }
                    self.tool.create_fresh_database(target)?;
                    self.tool
                        .restore_archive(target, &recovery_set_directory.join(archive_file))?;
                    self.tool.verify_restored_database(
                        target,
                        plan.deployment_id,
                        *schema_version,
                    )?;
                    restored_members.push(RestoredMember::Restored {
                        service: member.service,
                        database: target.database.clone(),
                        archive_sha256: archive_sha256.clone(),
                        schema_version: *schema_version,
                    });
                }
                BackupMemberState::NotApplicable { reason } => {
                    restored_members.push(RestoredMember::NotApplicable {
                        service: member.service,
                        reason: reason.clone(),
                    });
                }
            }
        }
        Ok(RestoreExecutionReport {
            schema_version: RESTORE_EXECUTION_REPORT_SCHEMA_VERSION,
            deployment_id: plan.deployment_id,
            recovery_set_id: plan.recovery_set_id,
            target_environment_id: plan.target_environment_id,
            started_at_unix,
            completed_at_unix: current_unix_time()?,
            members: restored_members,
            admission_required: true,
        })
    }
}

fn isolated_database_name(target_environment_id: Uuid, service: BackupService) -> String {
    let environment_text = target_environment_id.simple().to_string();
    format!(
        "pixels_restore_{}_{}",
        &environment_text[..12],
        service_name(service)
    )
}

fn service_name(service: BackupService) -> &'static str {
    match service {
        BackupService::Console => "console",
        BackupService::Auth => "auth",
        BackupService::Desk => "desk",
    }
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

fn current_unix_time() -> Result<u64, RestoreExecutionError> {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .map_err(|_| RestoreExecutionError::Clock)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        BackupMember, RecoveryEvidenceUnavailableReason, RecoverySecurityEvidence, RecoverySetKind,
        RecoverySetManifest, RecoverySetStatus, RetentionClass, MANIFEST_SCHEMA_VERSION,
    };
    use sha2::{Digest, Sha256};
    use std::{fs, sync::Mutex};

    struct RestoreToolProbe {
        operations: Mutex<Vec<String>>,
        fail_restore_service: Option<BackupService>,
    }

    impl LogicalRestoreTool for RestoreToolProbe {
        fn create_fresh_database(
            &self,
            target: &RestoreDatabaseTarget,
        ) -> Result<(), RestoreExecutionError> {
            self.operations
                .lock()
                .unwrap()
                .push(format!("create:{}", service_name(target.service)));
            Ok(())
        }

        fn restore_archive(
            &self,
            target: &RestoreDatabaseTarget,
            archive_path: &Path,
        ) -> Result<(), RestoreExecutionError> {
            assert!(archive_path.is_file());
            self.operations
                .lock()
                .unwrap()
                .push(format!("restore:{}", service_name(target.service)));
            if self.fail_restore_service == Some(target.service) {
                Err(RestoreExecutionError::RestoreFailed)
            } else {
                Ok(())
            }
        }

        fn verify_restored_database(
            &self,
            target: &RestoreDatabaseTarget,
            expected_deployment_id: Uuid,
            expected_schema_version: u32,
        ) -> Result<(), RestoreExecutionError> {
            assert!(!expected_deployment_id.is_nil());
            assert_eq!(expected_schema_version, 1);
            self.operations
                .lock()
                .unwrap()
                .push(format!("verify:{}", service_name(target.service)));
            Ok(())
        }
    }

    struct RestoreFixture {
        _temporary_directory: tempfile::TempDir,
        repository: BackupRepository,
        manifest: RecoverySetManifest,
        password_file: PathBuf,
    }

    impl RestoreFixture {
        fn new() -> Self {
            let temporary_directory = tempfile::tempdir().unwrap();
            make_private_directory(temporary_directory.path());
            let repository_root = temporary_directory.path().join("repository");
            fs::create_dir(&repository_root).unwrap();
            make_private_directory(&repository_root);
            let deployment_id = Uuid::new_v4();
            let repository = BackupRepository::open(&repository_root, deployment_id).unwrap();
            let recovery_set_id = Uuid::new_v4();
            let staged_recovery_set = repository.begin_set(recovery_set_id).unwrap();
            let archive_sha256 = format!("{:x}", Sha256::digest(b"archive"));
            let services = [
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ];
            for service in services {
                let archive_path = staged_recovery_set.prepare_archive(service).unwrap();
                fs::write(archive_path, b"archive").unwrap();
            }
            let manifest = RecoverySetManifest {
                schema_version: MANIFEST_SCHEMA_VERSION,
                recovery_set_id,
                deployment_id,
                kind: RecoverySetKind::Independent,
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
                            database: format!("pixels_{}", service_name(service)),
                            schema_version: 1,
                            archive_file: format!("{}.dump", service_name(service)),
                            archive_sha256: archive_sha256.clone(),
                            started_at_unix: 100,
                            completed_at_unix: 200,
                        },
                    })
                    .collect(),
                security_evidence: RecoverySecurityEvidence::Unavailable {
                    reason: RecoveryEvidenceUnavailableReason::IndependentBackup,
                },
                failure_code: None,
            };
            staged_recovery_set.publish(&manifest).unwrap();
            let password_file = temporary_directory.path().join("restore.pgpass");
            px_private_files::private::create_private(&password_file, b"credential\n").unwrap();
            Self {
                _temporary_directory: temporary_directory,
                repository,
                manifest,
                password_file,
            }
        }

        fn plan(&self, target_environment_id: Uuid) -> RestoreExecutionPlan {
            RestoreExecutionPlan {
                deployment_id: self.manifest.deployment_id,
                recovery_set_id: self.manifest.recovery_set_id,
                target_environment_id,
                targets: [
                    BackupService::Console,
                    BackupService::Auth,
                    BackupService::Desk,
                ]
                .into_iter()
                .map(|service| RestoreDatabaseTarget {
                    service,
                    host: "127.0.0.1".to_string(),
                    port: 5432,
                    database: isolated_database_name(target_environment_id, service),
                    owner: format!("pixels_{}_owner", service_name(service)),
                    username: "pixels_restore_operator".to_string(),
                    password_file: self.password_file.clone(),
                })
                .collect(),
            }
        }
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
    fn isolated_restore_uses_exact_derived_targets_and_remains_unadmitted() {
        let fixture = RestoreFixture::new();
        let target_environment_id = Uuid::new_v4();
        let plan = fixture.plan(target_environment_id);
        let tool = RestoreToolProbe {
            operations: Mutex::new(Vec::new()),
            fail_restore_service: None,
        };
        let report = RestoreRunner::new(tool)
            .run(&fixture.repository, &plan)
            .unwrap();
        assert_eq!(report.target_environment_id, target_environment_id);
        assert_eq!(report.members.len(), 3);
        assert!(report.admission_required);
        assert!(report.members.iter().all(|member| matches!(
            member,
            RestoredMember::Restored { database, .. } if database.starts_with("pixels_restore_")
        )));
    }

    #[test]
    fn restore_failure_stops_without_verifying_or_cleaning_the_failed_target() {
        let fixture = RestoreFixture::new();
        let plan = fixture.plan(Uuid::new_v4());
        let tool = RestoreToolProbe {
            operations: Mutex::new(Vec::new()),
            fail_restore_service: Some(BackupService::Auth),
        };
        let runner = RestoreRunner::new(tool);
        assert_eq!(
            runner.run(&fixture.repository, &plan),
            Err(RestoreExecutionError::RestoreFailed)
        );
        assert_eq!(
            runner.tool.operations.lock().unwrap().as_slice(),
            [
                "create:console",
                "restore:console",
                "verify:console",
                "create:auth",
                "restore:auth"
            ]
        );
    }

    #[test]
    fn source_database_and_arbitrary_target_names_are_rejected_before_tool_use() {
        let fixture = RestoreFixture::new();
        let mut plan = fixture.plan(Uuid::new_v4());
        plan.targets[0].database = "pixels_console".to_string();
        let tool = RestoreToolProbe {
            operations: Mutex::new(Vec::new()),
            fail_restore_service: None,
        };
        let runner = RestoreRunner::new(tool);
        assert_eq!(
            runner.run(&fixture.repository, &plan),
            Err(RestoreExecutionError::InvalidPlan)
        );
        assert!(runner.tool.operations.lock().unwrap().is_empty());
    }
}
