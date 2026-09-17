use crate::{
    evaluate_restore_admission, restore_admission_evidence_sha256, ExternalRecoveryWitness,
    RecoverySetManifest, RestoreAdmissionBlocker, RestoreAdmissionDecision, RestoreAdmissionError,
    RestoreOperationalCheck,
};
use fs2::FileExt;
use serde::{Deserialize, Serialize};
use std::{
    collections::BTreeSet,
    fs::{self, File, OpenOptions},
    io::Read,
    path::{Path, PathBuf},
};
use uuid::Uuid;

pub const RESTORE_ADMISSION_RECORD_SCHEMA_VERSION: u32 = 1;
const MAX_RESTORE_ADMISSION_RECORD_BYTES: usize = 4 * 1024;

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum RestoreAdmissionStoreError {
    #[error("restore admission store input is invalid")]
    InvalidInput,
    #[error("restore admission store is unavailable")]
    Unavailable,
    #[error("restore admission store is busy")]
    Busy,
    #[error("restore admission store requires reconciliation")]
    Corrupt,
    #[error("restore admission transition is not allowed")]
    InvalidTransition,
    #[error("restore admission evidence was rejected")]
    Evidence,
}

impl From<RestoreAdmissionError> for RestoreAdmissionStoreError {
    fn from(_: RestoreAdmissionError) -> Self {
        Self::Evidence
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum RestoreAdmissionState {
    RecoveryRequired,
    ReadyForManualApproval,
    Admitted,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreAdmissionApproval {
    pub approval_id: Uuid,
    pub administrator_id: Uuid,
    pub approved_at_unix: u64,
    pub evidence_sha256: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RestoreAdmissionRecord {
    pub schema_version: u32,
    pub deployment_id: Uuid,
    pub recovery_set_id: Uuid,
    pub target_environment_id: Uuid,
    pub state: RestoreAdmissionState,
    pub revision: u64,
    pub created_at_unix: u64,
    pub updated_at_unix: u64,
    pub evidence_sha256: Option<String>,
    pub blockers: BTreeSet<RestoreAdmissionBlocker>,
    pub approval: Option<RestoreAdmissionApproval>,
}

impl RestoreAdmissionRecord {
    pub(crate) fn validate(&self) -> Result<(), RestoreAdmissionStoreError> {
        if self.schema_version != RESTORE_ADMISSION_RECORD_SCHEMA_VERSION
            || self.deployment_id.is_nil()
            || self.recovery_set_id.is_nil()
            || self.target_environment_id.is_nil()
            || self.revision == 0
            || self.created_at_unix == 0
            || self.updated_at_unix < self.created_at_unix
            || self
                .evidence_sha256
                .as_ref()
                .is_some_and(|digest| !valid_sha256(digest))
        {
            return Err(RestoreAdmissionStoreError::Corrupt);
        }
        match (
            self.state,
            &self.evidence_sha256,
            &self.blockers,
            &self.approval,
        ) {
            (RestoreAdmissionState::RecoveryRequired, None, blockers, None)
                if blockers.is_empty() =>
            {
                Ok(())
            }
            (RestoreAdmissionState::RecoveryRequired, Some(_), blockers, None)
                if !blockers.is_empty() =>
            {
                Ok(())
            }
            (RestoreAdmissionState::ReadyForManualApproval, Some(_), blockers, None)
                if blockers.is_empty() =>
            {
                Ok(())
            }
            (RestoreAdmissionState::Admitted, Some(evidence_sha256), blockers, Some(approval))
                if blockers.is_empty()
                    && !approval.approval_id.is_nil()
                    && !approval.administrator_id.is_nil()
                    && approval.approved_at_unix >= self.updated_at_unix
                    && approval.evidence_sha256 == *evidence_sha256 =>
            {
                Ok(())
            }
            _ => Err(RestoreAdmissionStoreError::Corrupt),
        }
    }
}

pub struct RestoreAdmissionStore {
    root: PathBuf,
    record: RestoreAdmissionRecord,
    lock_file: File,
}

impl RestoreAdmissionStore {
    pub fn open(
        root: &Path,
        deployment_id: Uuid,
        recovery_set_id: Uuid,
        target_environment_id: Uuid,
        now_unix: u64,
    ) -> Result<Self, RestoreAdmissionStoreError> {
        if !root.is_absolute()
            || deployment_id.is_nil()
            || recovery_set_id.is_nil()
            || target_environment_id.is_nil()
            || now_unix == 0
        {
            return Err(RestoreAdmissionStoreError::InvalidInput);
        }
        px_private_files::private::verify_private_directory(root)
            .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
        verify_registered_entries(root)?;
        let lock_path = root.join("admission.lock");
        if !lock_path.exists() {
            px_private_files::private::create_private(&lock_path, b"restore-admission-lock\n")
                .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
        }
        let mut lock_options = OpenOptions::new();
        lock_options.read(true);
        #[cfg(unix)]
        {
            use std::os::unix::fs::OpenOptionsExt;
            lock_options.custom_flags(libc::O_NOFOLLOW);
        }
        #[cfg(windows)]
        {
            use std::os::windows::fs::OpenOptionsExt;
            lock_options.share_mode(1).custom_flags(0x00200000);
        }
        let lock_file = lock_options
            .open(&lock_path)
            .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
        lock_file.try_lock_exclusive().map_err(|error| {
            if error.kind() == std::io::ErrorKind::WouldBlock || error.raw_os_error() == Some(33) {
                RestoreAdmissionStoreError::Busy
            } else {
                RestoreAdmissionStoreError::Unavailable
            }
        })?;
        verify_lock_file(&lock_file)?;
        recover_atomic_record_files(root)?;
        let record_path = root.join("admission.json");
        let record = if record_path.exists() {
            read_record(&record_path)?
        } else {
            let initial_record = RestoreAdmissionRecord {
                schema_version: RESTORE_ADMISSION_RECORD_SCHEMA_VERSION,
                deployment_id,
                recovery_set_id,
                target_environment_id,
                state: RestoreAdmissionState::RecoveryRequired,
                revision: 1,
                created_at_unix: now_unix,
                updated_at_unix: now_unix,
                evidence_sha256: None,
                blockers: BTreeSet::new(),
                approval: None,
            };
            persist_record(root, &initial_record)?;
            initial_record
        };
        record.validate()?;
        if record.deployment_id != deployment_id
            || record.recovery_set_id != recovery_set_id
            || record.target_environment_id != target_environment_id
        {
            return Err(RestoreAdmissionStoreError::InvalidInput);
        }
        Ok(Self {
            root: root.to_path_buf(),
            record,
            lock_file,
        })
    }

    pub fn record(&self) -> &RestoreAdmissionRecord {
        &self.record
    }

    pub fn evaluate(
        &mut self,
        manifest: &RecoverySetManifest,
        witness: &ExternalRecoveryWitness,
        completed_checks: &BTreeSet<RestoreOperationalCheck>,
        now_unix: u64,
    ) -> Result<&RestoreAdmissionRecord, RestoreAdmissionStoreError> {
        if self.record.state == RestoreAdmissionState::Admitted
            || manifest.deployment_id != self.record.deployment_id
            || manifest.recovery_set_id != self.record.recovery_set_id
            || now_unix < self.record.updated_at_unix
        {
            return Err(RestoreAdmissionStoreError::InvalidTransition);
        }
        let decision = evaluate_restore_admission(manifest, witness, completed_checks)?;
        let evidence_sha256 =
            restore_admission_evidence_sha256(manifest, witness, completed_checks)?;
        let (next_state, blockers) = match decision {
            RestoreAdmissionDecision::RecoveryRequired { blockers } => {
                (RestoreAdmissionState::RecoveryRequired, blockers)
            }
            RestoreAdmissionDecision::ReadyForManualApproval => (
                RestoreAdmissionState::ReadyForManualApproval,
                BTreeSet::new(),
            ),
        };
        if self.record.state == next_state
            && self.record.evidence_sha256.as_deref() == Some(&evidence_sha256)
            && self.record.blockers == blockers
        {
            return Ok(&self.record);
        }
        self.record.state = next_state;
        self.record.revision = self
            .record
            .revision
            .checked_add(1)
            .ok_or(RestoreAdmissionStoreError::Corrupt)?;
        self.record.updated_at_unix = now_unix;
        self.record.evidence_sha256 = Some(evidence_sha256);
        self.record.blockers = blockers;
        self.record.approval = None;
        self.record.validate()?;
        persist_record(&self.root, &self.record)?;
        Ok(&self.record)
    }

    pub fn approve(
        &mut self,
        approval_id: Uuid,
        administrator_id: Uuid,
        expected_revision: u64,
        expected_evidence_sha256: &str,
        now_unix: u64,
    ) -> Result<&RestoreAdmissionRecord, RestoreAdmissionStoreError> {
        if self.record.state == RestoreAdmissionState::Admitted {
            let existing_approval = self
                .record
                .approval
                .as_ref()
                .ok_or(RestoreAdmissionStoreError::Corrupt)?;
            if existing_approval.approval_id == approval_id
                && existing_approval.administrator_id == administrator_id
                && existing_approval.evidence_sha256 == expected_evidence_sha256
            {
                return Ok(&self.record);
            }
            return Err(RestoreAdmissionStoreError::InvalidTransition);
        }
        if self.record.state != RestoreAdmissionState::ReadyForManualApproval
            || approval_id.is_nil()
            || administrator_id.is_nil()
            || expected_revision != self.record.revision
            || self.record.evidence_sha256.as_deref() != Some(expected_evidence_sha256)
            || !valid_sha256(expected_evidence_sha256)
            || now_unix < self.record.updated_at_unix
        {
            return Err(RestoreAdmissionStoreError::InvalidTransition);
        }
        self.record.state = RestoreAdmissionState::Admitted;
        self.record.revision = self
            .record
            .revision
            .checked_add(1)
            .ok_or(RestoreAdmissionStoreError::Corrupt)?;
        self.record.updated_at_unix = now_unix;
        self.record.approval = Some(RestoreAdmissionApproval {
            approval_id,
            administrator_id,
            approved_at_unix: now_unix,
            evidence_sha256: expected_evidence_sha256.to_string(),
        });
        self.record.validate()?;
        persist_record(&self.root, &self.record)?;
        Ok(&self.record)
    }
}

fn verify_lock_file(lock_file: &File) -> Result<(), RestoreAdmissionStoreError> {
    const LOCK_CONTENTS: &[u8] = b"restore-admission-lock\n";
    let metadata = lock_file
        .metadata()
        .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
    if !metadata.is_file() || metadata.len() != LOCK_CONTENTS.len() as u64 {
        return Err(RestoreAdmissionStoreError::Corrupt);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if metadata.permissions().mode() & 0o077 != 0 {
            return Err(RestoreAdmissionStoreError::Corrupt);
        }
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        if metadata.file_attributes() & 0x400 != 0 {
            return Err(RestoreAdmissionStoreError::Corrupt);
        }
    }
    let mut lock_contents = Vec::with_capacity(LOCK_CONTENTS.len());
    let mut lock_reader = lock_file;
    lock_reader
        .read_to_end(&mut lock_contents)
        .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
    if lock_contents == LOCK_CONTENTS {
        Ok(())
    } else {
        Err(RestoreAdmissionStoreError::Corrupt)
    }
}

impl Drop for RestoreAdmissionStore {
    fn drop(&mut self) {
        let _ = FileExt::unlock(&self.lock_file);
    }
}

fn verify_registered_entries(root: &Path) -> Result<(), RestoreAdmissionStoreError> {
    for directory_entry in
        fs::read_dir(root).map_err(|_| RestoreAdmissionStoreError::Unavailable)?
    {
        let directory_entry =
            directory_entry.map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
        let file_name = directory_entry
            .file_name()
            .to_str()
            .ok_or(RestoreAdmissionStoreError::Corrupt)?
            .to_string();
        if !matches!(
            file_name.as_str(),
            "admission.lock" | "admission.json" | "admission.previous" | "admission.next"
        ) || !directory_entry
            .file_type()
            .map_err(|_| RestoreAdmissionStoreError::Unavailable)?
            .is_file()
        {
            return Err(RestoreAdmissionStoreError::Corrupt);
        }
    }
    Ok(())
}

fn recover_atomic_record_files(root: &Path) -> Result<(), RestoreAdmissionStoreError> {
    let current_path = root.join("admission.json");
    let previous_path = root.join("admission.previous");
    let next_path = root.join("admission.next");
    match (
        current_path.exists(),
        previous_path.exists(),
        next_path.exists(),
    ) {
        (true, false, false) | (false, false, false) => {}
        (true, true, false) => remove_file_and_sync(&previous_path, root)?,
        (true, false, true) => remove_file_and_sync(&next_path, root)?,
        (false, true, true) => {
            fs::rename(&next_path, &current_path)
                .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
            remove_file_and_sync(&previous_path, root)?;
        }
        (false, true, false) => {
            fs::rename(&previous_path, &current_path)
                .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
            sync_directory(root)?;
        }
        (false, false, true) => {
            fs::rename(&next_path, &current_path)
                .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
            sync_directory(root)?;
        }
        (true, true, true) => return Err(RestoreAdmissionStoreError::Corrupt),
    }
    Ok(())
}

fn read_record(path: &Path) -> Result<RestoreAdmissionRecord, RestoreAdmissionStoreError> {
    let record_bytes = px_private_files::private::read_private(path)
        .map_err(|_| RestoreAdmissionStoreError::Corrupt)?;
    if record_bytes.len() > MAX_RESTORE_ADMISSION_RECORD_BYTES {
        return Err(RestoreAdmissionStoreError::Corrupt);
    }
    let record = serde_json::from_slice::<RestoreAdmissionRecord>(&record_bytes)
        .map_err(|_| RestoreAdmissionStoreError::Corrupt)?;
    record.validate()?;
    Ok(record)
}

fn persist_record(
    root: &Path,
    record: &RestoreAdmissionRecord,
) -> Result<(), RestoreAdmissionStoreError> {
    let record_bytes =
        serde_json::to_vec_pretty(record).map_err(|_| RestoreAdmissionStoreError::Corrupt)?;
    if record_bytes.is_empty() || record_bytes.len() > MAX_RESTORE_ADMISSION_RECORD_BYTES {
        return Err(RestoreAdmissionStoreError::Corrupt);
    }
    let current_path = root.join("admission.json");
    let previous_path = root.join("admission.previous");
    let next_path = root.join("admission.next");
    if previous_path.exists() || next_path.exists() {
        return Err(RestoreAdmissionStoreError::Corrupt);
    }
    px_private_files::private::create_private(&next_path, &record_bytes)
        .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
    if current_path.exists() {
        fs::rename(&current_path, &previous_path)
            .map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
    }
    fs::rename(&next_path, &current_path).map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
    sync_directory(root)?;
    if previous_path.exists() {
        remove_file_and_sync(&previous_path, root)?;
    }
    Ok(())
}

fn remove_file_and_sync(file_path: &Path, root: &Path) -> Result<(), RestoreAdmissionStoreError> {
    fs::remove_file(file_path).map_err(|_| RestoreAdmissionStoreError::Unavailable)?;
    sync_directory(root)
}

fn sync_directory(path: &Path) -> Result<(), RestoreAdmissionStoreError> {
    #[cfg(windows)]
    {
        let _ = path;
        Ok(())
    }
    #[cfg(unix)]
    {
        File::open(path)
            .and_then(|directory| directory.sync_all())
            .map_err(|_| RestoreAdmissionStoreError::Unavailable)
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
        BackupMember, BackupMemberState, BackupService, RecoverySecurityEvidence, RecoverySetKind,
        RecoverySetStatus, RetentionClass, ServiceSecurityWatermark, MANIFEST_SCHEMA_VERSION,
        RECOVERY_WITNESS_SCHEMA_VERSION,
    };

    struct StoreFixture {
        _temporary_directory: tempfile::TempDir,
        root: PathBuf,
        manifest: RecoverySetManifest,
        witness: ExternalRecoveryWitness,
    }

    impl StoreFixture {
        fn new() -> Self {
            let temporary_directory = tempfile::tempdir().unwrap();
            make_private_directory(temporary_directory.path());
            let root = temporary_directory.path().join("admission");
            fs::create_dir(&root).unwrap();
            make_private_directory(&root);
            let services = [
                BackupService::Console,
                BackupService::Auth,
                BackupService::Desk,
            ];
            let deployment_id = Uuid::new_v4();
            let watermarks = services
                .into_iter()
                .map(|service| ServiceSecurityWatermark {
                    service,
                    recovery_generation: Uuid::new_v4(),
                    security_sequence: 19,
                    security_state_sha256: "c".repeat(64),
                })
                .collect::<Vec<_>>();
            let manifest = RecoverySetManifest {
                schema_version: MANIFEST_SCHEMA_VERSION,
                recovery_set_id: Uuid::new_v4(),
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
                            archive_sha256: "a".repeat(64),
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
            let witness = ExternalRecoveryWitness {
                schema_version: RECOVERY_WITNESS_SCHEMA_VERSION,
                deployment_id,
                witnessed_at_unix: 300,
                available_key_ids: BTreeSet::from(["b".repeat(64)]),
                watermarks,
            };
            Self {
                _temporary_directory: temporary_directory,
                root,
                manifest,
                witness,
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
    fn approval_requires_exact_revision_and_evidence_then_survives_restart() {
        let fixture = StoreFixture::new();
        let target_environment_id = Uuid::new_v4();
        let completed_checks = RestoreOperationalCheck::required();
        let mut store = RestoreAdmissionStore::open(
            &fixture.root,
            fixture.manifest.deployment_id,
            fixture.manifest.recovery_set_id,
            target_environment_id,
            400,
        )
        .unwrap();
        assert_eq!(
            store.record().state,
            RestoreAdmissionState::RecoveryRequired
        );
        assert_eq!(
            store.approve(Uuid::new_v4(), Uuid::new_v4(), 1, &"d".repeat(64), 401),
            Err(RestoreAdmissionStoreError::InvalidTransition)
        );
        store
            .evaluate(&fixture.manifest, &fixture.witness, &completed_checks, 402)
            .unwrap();
        assert_eq!(
            store.record().state,
            RestoreAdmissionState::ReadyForManualApproval
        );
        let ready_revision = store.record().revision;
        let evidence_sha256 = store.record().evidence_sha256.clone().unwrap();
        assert_eq!(
            store.approve(
                Uuid::new_v4(),
                Uuid::new_v4(),
                ready_revision - 1,
                &evidence_sha256,
                403,
            ),
            Err(RestoreAdmissionStoreError::InvalidTransition)
        );
        let approval_id = Uuid::new_v4();
        let administrator_id = Uuid::new_v4();
        store
            .approve(
                approval_id,
                administrator_id,
                ready_revision,
                &evidence_sha256,
                404,
            )
            .unwrap();
        assert_eq!(store.record().state, RestoreAdmissionState::Admitted);
        assert!(store
            .approve(
                approval_id,
                administrator_id,
                ready_revision,
                &evidence_sha256,
                405,
            )
            .is_ok());
        assert_eq!(
            store.approve(
                Uuid::new_v4(),
                administrator_id,
                ready_revision,
                &evidence_sha256,
                406,
            ),
            Err(RestoreAdmissionStoreError::InvalidTransition)
        );
        drop(store);

        let reopened = RestoreAdmissionStore::open(
            &fixture.root,
            fixture.manifest.deployment_id,
            fixture.manifest.recovery_set_id,
            target_environment_id,
            405,
        )
        .unwrap();
        assert_eq!(reopened.record().state, RestoreAdmissionState::Admitted);
        assert_eq!(
            reopened.record().approval.as_ref().unwrap().approval_id,
            approval_id
        );
    }

    #[test]
    fn changed_or_incomplete_evidence_revokes_readiness_without_auto_approval() {
        let fixture = StoreFixture::new();
        let mut store = RestoreAdmissionStore::open(
            &fixture.root,
            fixture.manifest.deployment_id,
            fixture.manifest.recovery_set_id,
            Uuid::new_v4(),
            400,
        )
        .unwrap();
        let completed_checks = RestoreOperationalCheck::required();
        store
            .evaluate(&fixture.manifest, &fixture.witness, &completed_checks, 401)
            .unwrap();
        let ready_revision = store.record().revision;
        let mut incomplete_checks = completed_checks;
        incomplete_checks.remove(&RestoreOperationalCheck::PendingCommandsReconciled);
        store
            .evaluate(&fixture.manifest, &fixture.witness, &incomplete_checks, 402)
            .unwrap();
        assert_eq!(
            store.record().state,
            RestoreAdmissionState::RecoveryRequired
        );
        assert!(store.record().revision > ready_revision);
        assert!(store.record().approval.is_none());
        assert!(store.record().blockers.contains(
            &RestoreAdmissionBlocker::OperationalCheckMissing {
                check: RestoreOperationalCheck::PendingCommandsReconciled,
            }
        ));
    }

    #[test]
    fn store_is_exclusive_and_unknown_content_fails_closed() {
        let fixture = StoreFixture::new();
        let target_environment_id = Uuid::new_v4();
        let store = RestoreAdmissionStore::open(
            &fixture.root,
            fixture.manifest.deployment_id,
            fixture.manifest.recovery_set_id,
            target_environment_id,
            400,
        )
        .unwrap();
        let second_open_error = match RestoreAdmissionStore::open(
            &fixture.root,
            fixture.manifest.deployment_id,
            fixture.manifest.recovery_set_id,
            target_environment_id,
            401,
        ) {
            Ok(_) => panic!("restore admission store must be process exclusive"),
            Err(open_error) => open_error,
        };
        assert_eq!(second_open_error, RestoreAdmissionStoreError::Busy);
        drop(store);
        fs::write(fixture.root.join("unregistered"), b"do not trust").unwrap();
        assert!(matches!(
            RestoreAdmissionStore::open(
                &fixture.root,
                fixture.manifest.deployment_id,
                fixture.manifest.recovery_set_id,
                target_environment_id,
                402,
            ),
            Err(RestoreAdmissionStoreError::Corrupt)
        ));
    }

    #[test]
    fn interrupted_atomic_record_rotation_is_completed_before_reading_state() {
        let fixture = StoreFixture::new();
        let target_environment_id = Uuid::new_v4();
        let store = RestoreAdmissionStore::open(
            &fixture.root,
            fixture.manifest.deployment_id,
            fixture.manifest.recovery_set_id,
            target_environment_id,
            400,
        )
        .unwrap();
        let expected_record = store.record().clone();
        drop(store);
        let current_path = fixture.root.join("admission.json");
        let previous_path = fixture.root.join("admission.previous");
        let next_path = fixture.root.join("admission.next");
        let record_bytes = px_private_files::private::read_private(&current_path).unwrap();
        fs::rename(&current_path, &previous_path).unwrap();
        px_private_files::private::create_private(&next_path, &record_bytes).unwrap();

        let recovered = RestoreAdmissionStore::open(
            &fixture.root,
            fixture.manifest.deployment_id,
            fixture.manifest.recovery_set_id,
            target_environment_id,
            401,
        )
        .unwrap();
        assert_eq!(recovered.record(), &expected_record);
        assert!(current_path.is_file());
        assert!(!previous_path.exists());
        assert!(!next_path.exists());
    }
}
