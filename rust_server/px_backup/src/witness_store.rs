use crate::{
    executor::valid_sha256, ExternalRecoveryWitness, RecoverySealReport, RecoverySecurityEvidence,
    RecoverySetManifest, RestoreAdmissionRecord, RestoreAdmissionState,
    RECOVERY_WITNESS_SCHEMA_VERSION,
};
use fs2::FileExt;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs::{self, File, OpenOptions},
    io::Read,
    path::{Path, PathBuf},
    time::{SystemTime, UNIX_EPOCH},
};
use uuid::Uuid;

const WITNESS_STORE_IDENTITY_SCHEMA_VERSION: u32 = 1;
const WITNESS_RECORD_SCHEMA_VERSION: u32 = 1;
const WITNESS_CURRENT_SCHEMA_VERSION: u32 = 1;
const MAX_PRIVATE_STATE_BYTES: usize = 256 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum WitnessStoreError {
    #[error("external witness store input is invalid")]
    InvalidInput,
    #[error("external witness store is unavailable")]
    Unavailable,
    #[error("external witness store is busy")]
    Busy,
    #[error("external witness store is corrupt")]
    Corrupt,
    #[error("external witness would move backwards")]
    NonMonotonic,
    #[error("system clock is unavailable")]
    Clock,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct WitnessStoreIdentity {
    schema_version: u32,
    deployment_id: Uuid,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct WitnessJournalRecord {
    schema_version: u32,
    deployment_id: Uuid,
    ordinal: u64,
    recovery_set_id: Uuid,
    consistency_proof_id: Uuid,
    recorded_at_unix: u64,
    previous_record_sha256: Option<String>,
    witness_sha256: String,
    generation_transition: Option<WitnessGenerationTransition>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct WitnessGenerationTransition {
    source_recovery_set_id: Uuid,
    target_environment_id: Uuid,
    approval_id: Uuid,
    recovery_seal_report_sha256: String,
    admission_record_sha256: String,
}

struct ValidatedGenerationTransition {
    record: WitnessGenerationTransition,
    source_generations: BTreeMap<crate::BackupService, Uuid>,
    recovery_generation: Uuid,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct CurrentWitnessRecord {
    schema_version: u32,
    deployment_id: Uuid,
    ordinal: u64,
    recovery_set_id: Uuid,
    record_sha256: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RecordedRecoveryWitness {
    pub witness_path: PathBuf,
    pub ordinal: u64,
    pub recovery_set_id: Uuid,
    pub record_sha256: String,
}

pub struct RecoveryWitnessStore {
    root: PathBuf,
    deployment_id: Uuid,
    _lock: WitnessStoreLock,
}

impl RecoveryWitnessStore {
    pub fn open(root: &Path, deployment_id: Uuid) -> Result<Self, WitnessStoreError> {
        if !root.is_absolute() || deployment_id.is_nil() || !root.is_dir() {
            return Err(WitnessStoreError::InvalidInput);
        }
        px_private_files::private::verify_private_directory(root)
            .map_err(|_| WitnessStoreError::Unavailable)?;
        let identity_path = root.join("identity.json");
        if !identity_path.exists() {
            let create_result = persist_private(
                &identity_path,
                &WitnessStoreIdentity {
                    schema_version: WITNESS_STORE_IDENTITY_SCHEMA_VERSION,
                    deployment_id,
                },
            );
            if let Err(error) = create_result {
                if !identity_path.exists() {
                    return Err(error);
                }
            }
        }
        let identity = load_private::<WitnessStoreIdentity>(&identity_path)?;
        if identity.schema_version != WITNESS_STORE_IDENTITY_SCHEMA_VERSION
            || identity.deployment_id != deployment_id
        {
            return Err(WitnessStoreError::Corrupt);
        }
        let lock_path = root.join("witness.lock");
        let operation_lock = WitnessStoreLock::acquire(&lock_path)?;
        recover_current(root)?;
        for directory_name in ["journal", "witnesses"] {
            let directory = root.join(directory_name);
            if !directory.exists() {
                fs::create_dir(&directory).map_err(|_| WitnessStoreError::Unavailable)?;
                make_private_directory(&directory)?;
            }
            if !directory.is_dir() {
                return Err(WitnessStoreError::Corrupt);
            }
        }
        verify_root_entries(root)?;
        reconcile_orphan_witnesses(root)?;
        verify_journal(root, deployment_id)?;
        Ok(Self {
            root: root.to_path_buf(),
            deployment_id,
            _lock: operation_lock,
        })
    }

    pub fn record_manifest(
        &self,
        manifest: &RecoverySetManifest,
    ) -> Result<RecordedRecoveryWitness, WitnessStoreError> {
        self.record_manifest_internal(manifest, None)
    }

    pub fn record_manifest_after_recovery(
        &self,
        manifest: &RecoverySetManifest,
        source_manifest: &RecoverySetManifest,
        recovery_seal_report: &RecoverySealReport,
        admission_record: &RestoreAdmissionRecord,
    ) -> Result<RecordedRecoveryWitness, WitnessStoreError> {
        let transition = validate_generation_transition(
            self.deployment_id,
            manifest,
            source_manifest,
            recovery_seal_report,
            admission_record,
        )?;
        self.record_manifest_internal(manifest, Some(transition))
    }

    fn record_manifest_internal(
        &self,
        manifest: &RecoverySetManifest,
        generation_transition: Option<ValidatedGenerationTransition>,
    ) -> Result<RecordedRecoveryWitness, WitnessStoreError> {
        manifest
            .validate()
            .map_err(|_| WitnessStoreError::InvalidInput)?;
        if manifest.deployment_id != self.deployment_id || !manifest.status.is_verified() {
            return Err(WitnessStoreError::InvalidInput);
        }
        let (consistency_proof_id, external_key_ids, watermarks) = match &manifest.security_evidence
        {
            RecoverySecurityEvidence::Captured {
                consistency_proof_id,
                external_key_ids,
                watermarks,
            } => (
                *consistency_proof_id,
                external_key_ids.clone(),
                watermarks.clone(),
            ),
            RecoverySecurityEvidence::Unavailable { .. } => {
                return Err(WitnessStoreError::InvalidInput);
            }
        };
        let witness = ExternalRecoveryWitness {
            schema_version: RECOVERY_WITNESS_SCHEMA_VERSION,
            deployment_id: self.deployment_id,
            witnessed_at_unix: current_unix_time()?,
            available_key_ids: external_key_ids,
            watermarks,
        };
        witness
            .validate()
            .map_err(|_| WitnessStoreError::InvalidInput)?;

        let witness_path = self
            .root
            .join("witnesses")
            .join(format!("{}.json", manifest.recovery_set_id));
        let existing_record = find_record_for_set(&self.root, manifest.recovery_set_id)?;
        if let Some((record, record_sha256)) = existing_record {
            let existing_witness = ExternalRecoveryWitness::load_private(&witness_path)
                .map_err(|_| WitnessStoreError::Corrupt)?;
            if existing_witness.deployment_id != witness.deployment_id
                || existing_witness.available_key_ids != witness.available_key_ids
                || existing_witness.watermarks != witness.watermarks
                || record.consistency_proof_id != consistency_proof_id
            {
                return Err(WitnessStoreError::Corrupt);
            }
            return Ok(RecordedRecoveryWitness {
                witness_path,
                ordinal: record.ordinal,
                recovery_set_id: record.recovery_set_id,
                record_sha256,
            });
        }
        if witness_path.exists() {
            return Err(WitnessStoreError::Corrupt);
        }

        let previous = load_current(&self.root)?;
        if previous.is_none() && generation_transition.is_some() {
            return Err(WitnessStoreError::InvalidInput);
        }
        if let Some((current, current_record, current_witness)) = &previous {
            match &generation_transition {
                Some(transition) => verify_generation_advance(
                    &current_witness.watermarks,
                    &witness.watermarks,
                    transition,
                )?,
                None => verify_monotonic(&current_witness.watermarks, &witness.watermarks)?,
            }
            if current.ordinal != current_record.ordinal
                || current.recovery_set_id != current_record.recovery_set_id
            {
                return Err(WitnessStoreError::Corrupt);
            }
        }
        let ordinal = previous.as_ref().map_or(Ok(1), |(current, _, _)| {
            current
                .ordinal
                .checked_add(1)
                .ok_or(WitnessStoreError::Corrupt)
        })?;
        let witness_bytes =
            serde_json::to_vec_pretty(&witness).map_err(|_| WitnessStoreError::Corrupt)?;
        let witness_sha256 = sha256_bytes(&witness_bytes);
        px_private_files::private::create_private(&witness_path, &witness_bytes)
            .map_err(|_| WitnessStoreError::Unavailable)?;
        let record = WitnessJournalRecord {
            schema_version: WITNESS_RECORD_SCHEMA_VERSION,
            deployment_id: self.deployment_id,
            ordinal,
            recovery_set_id: manifest.recovery_set_id,
            consistency_proof_id,
            recorded_at_unix: witness.witnessed_at_unix,
            previous_record_sha256: previous
                .as_ref()
                .map(|(current, _, _)| current.record_sha256.clone()),
            witness_sha256,
            generation_transition: generation_transition.map(|transition| transition.record),
        };
        let record_path = journal_path(&self.root, ordinal, manifest.recovery_set_id);
        let record_bytes =
            serde_json::to_vec_pretty(&record).map_err(|_| WitnessStoreError::Corrupt)?;
        let record_sha256 = sha256_bytes(&record_bytes);
        px_private_files::private::create_private(&record_path, &record_bytes)
            .map_err(|_| WitnessStoreError::Unavailable)?;
        replace_current(
            &self.root,
            &CurrentWitnessRecord {
                schema_version: WITNESS_CURRENT_SCHEMA_VERSION,
                deployment_id: self.deployment_id,
                ordinal,
                recovery_set_id: manifest.recovery_set_id,
                record_sha256: record_sha256.clone(),
            },
        )?;
        Ok(RecordedRecoveryWitness {
            witness_path,
            ordinal,
            recovery_set_id: manifest.recovery_set_id,
            record_sha256,
        })
    }
}

struct WitnessStoreLock {
    file: File,
}

impl WitnessStoreLock {
    fn acquire(path: &Path) -> Result<Self, WitnessStoreError> {
        const CONTENTS: &[u8] = b"pixels-recovery-witness-lock\n";
        if !path.exists() {
            match px_private_files::private::create_private(path, CONTENTS) {
                Ok(()) => {}
                Err(_) if path.exists() => {}
                Err(_) => return Err(WitnessStoreError::Unavailable),
            }
        }
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(path)
            .map_err(|_| WitnessStoreError::Unavailable)?;
        file.try_lock_exclusive()
            .map_err(|_| WitnessStoreError::Busy)?;
        let mut contents = Vec::new();
        file.read_to_end(&mut contents)
            .map_err(|_| WitnessStoreError::Unavailable)?;
        if contents != CONTENTS {
            return Err(WitnessStoreError::Corrupt);
        }
        Ok(Self { file })
    }
}

impl Drop for WitnessStoreLock {
    fn drop(&mut self) {
        let _ = FileExt::unlock(&self.file);
    }
}

fn verify_root_entries(root: &Path) -> Result<(), WitnessStoreError> {
    for directory_entry in fs::read_dir(root).map_err(|_| WitnessStoreError::Unavailable)? {
        let directory_entry = directory_entry.map_err(|_| WitnessStoreError::Unavailable)?;
        let name = directory_entry
            .file_name()
            .to_str()
            .ok_or(WitnessStoreError::Corrupt)?
            .to_owned();
        if !matches!(
            name.as_str(),
            "identity.json"
                | "witness.lock"
                | "journal"
                | "witnesses"
                | "current.json"
                | "current.previous"
                | "current.next"
        ) {
            return Err(WitnessStoreError::Corrupt);
        }
    }
    Ok(())
}

fn verify_journal(root: &Path, deployment_id: Uuid) -> Result<(), WitnessStoreError> {
    let mut records = Vec::new();
    for directory_entry in
        fs::read_dir(root.join("journal")).map_err(|_| WitnessStoreError::Unavailable)?
    {
        let path = directory_entry
            .map_err(|_| WitnessStoreError::Unavailable)?
            .path();
        if !path.is_file() {
            return Err(WitnessStoreError::Corrupt);
        }
        let record_bytes = read_private_bytes(&path)?;
        let record = serde_json::from_slice::<WitnessJournalRecord>(&record_bytes)
            .map_err(|_| WitnessStoreError::Corrupt)?;
        validate_record(&record, deployment_id)?;
        if path != journal_path(root, record.ordinal, record.recovery_set_id) {
            return Err(WitnessStoreError::Corrupt);
        }
        records.push((record, sha256_bytes(&record_bytes)));
    }
    records.sort_by_key(|(record, _)| record.ordinal);
    let mut previous_sha256 = None;
    for (index, (record, record_sha256)) in records.iter().enumerate() {
        if record.ordinal != index as u64 + 1 || record.previous_record_sha256 != previous_sha256 {
            return Err(WitnessStoreError::Corrupt);
        }
        let witness_path = root
            .join("witnesses")
            .join(format!("{}.json", record.recovery_set_id));
        let witness_bytes = read_private_bytes(&witness_path)?;
        if sha256_bytes(&witness_bytes) != record.witness_sha256 {
            return Err(WitnessStoreError::Corrupt);
        }
        let witness = serde_json::from_slice::<ExternalRecoveryWitness>(&witness_bytes)
            .map_err(|_| WitnessStoreError::Corrupt)?;
        witness.validate().map_err(|_| WitnessStoreError::Corrupt)?;
        if witness.deployment_id != deployment_id {
            return Err(WitnessStoreError::Corrupt);
        }
        previous_sha256 = Some(record_sha256.clone());
    }
    let witness_files = fs::read_dir(root.join("witnesses"))
        .map_err(|_| WitnessStoreError::Unavailable)?
        .count();
    if witness_files != records.len() {
        return Err(WitnessStoreError::Corrupt);
    }
    match (records.last(), load_current(root)?) {
        (None, None) => Ok(()),
        (Some((last_record, last_sha256)), Some((current, current_record, _)))
            if current.ordinal == last_record.ordinal
                && current.recovery_set_id == last_record.recovery_set_id
                && current.record_sha256 == *last_sha256
                && current_record == *last_record =>
        {
            Ok(())
        }
        (Some((last_record, last_sha256)), current) => {
            if let Some((current_record, _, _)) = &current {
                if current_record.ordinal >= last_record.ordinal {
                    return Err(WitnessStoreError::Corrupt);
                }
            }
            replace_current(
                root,
                &CurrentWitnessRecord {
                    schema_version: WITNESS_CURRENT_SCHEMA_VERSION,
                    deployment_id,
                    ordinal: last_record.ordinal,
                    recovery_set_id: last_record.recovery_set_id,
                    record_sha256: last_sha256.clone(),
                },
            )
        }
        _ => Err(WitnessStoreError::Corrupt),
    }
}

fn reconcile_orphan_witnesses(root: &Path) -> Result<(), WitnessStoreError> {
    let mut recorded_sets = BTreeSet::new();
    for directory_entry in
        fs::read_dir(root.join("journal")).map_err(|_| WitnessStoreError::Unavailable)?
    {
        let path = directory_entry
            .map_err(|_| WitnessStoreError::Unavailable)?
            .path();
        let record = load_private::<WitnessJournalRecord>(&path)?;
        recorded_sets.insert(record.recovery_set_id);
    }
    for directory_entry in
        fs::read_dir(root.join("witnesses")).map_err(|_| WitnessStoreError::Unavailable)?
    {
        let path = directory_entry
            .map_err(|_| WitnessStoreError::Unavailable)?
            .path();
        let recovery_set_id = path
            .file_stem()
            .and_then(|value| value.to_str())
            .and_then(|value| Uuid::parse_str(value).ok())
            .ok_or(WitnessStoreError::Corrupt)?;
        if path.extension().and_then(|value| value.to_str()) != Some("json") {
            return Err(WitnessStoreError::Corrupt);
        }
        if !recorded_sets.contains(&recovery_set_id) {
            fs::remove_file(path).map_err(|_| WitnessStoreError::Unavailable)?;
        }
    }
    Ok(())
}

fn load_current(
    root: &Path,
) -> Result<
    Option<(
        CurrentWitnessRecord,
        WitnessJournalRecord,
        ExternalRecoveryWitness,
    )>,
    WitnessStoreError,
> {
    let current_path = root.join("current.json");
    if !current_path.exists() {
        return Ok(None);
    }
    let current = load_private::<CurrentWitnessRecord>(&current_path)?;
    if current.schema_version != WITNESS_CURRENT_SCHEMA_VERSION
        || current.deployment_id.is_nil()
        || current.ordinal == 0
        || current.recovery_set_id.is_nil()
        || !valid_sha256(&current.record_sha256)
    {
        return Err(WitnessStoreError::Corrupt);
    }
    let record_path = journal_path(root, current.ordinal, current.recovery_set_id);
    let record_bytes = read_private_bytes(&record_path)?;
    if sha256_bytes(&record_bytes) != current.record_sha256 {
        return Err(WitnessStoreError::Corrupt);
    }
    let record = serde_json::from_slice::<WitnessJournalRecord>(&record_bytes)
        .map_err(|_| WitnessStoreError::Corrupt)?;
    validate_record(&record, current.deployment_id)?;
    let witness_path = root
        .join("witnesses")
        .join(format!("{}.json", current.recovery_set_id));
    let witness = ExternalRecoveryWitness::load_private(&witness_path)
        .map_err(|_| WitnessStoreError::Corrupt)?;
    Ok(Some((current, record, witness)))
}

fn find_record_for_set(
    root: &Path,
    recovery_set_id: Uuid,
) -> Result<Option<(WitnessJournalRecord, String)>, WitnessStoreError> {
    for directory_entry in
        fs::read_dir(root.join("journal")).map_err(|_| WitnessStoreError::Unavailable)?
    {
        let path = directory_entry
            .map_err(|_| WitnessStoreError::Unavailable)?
            .path();
        let record_bytes = read_private_bytes(&path)?;
        let record = serde_json::from_slice::<WitnessJournalRecord>(&record_bytes)
            .map_err(|_| WitnessStoreError::Corrupt)?;
        if record.recovery_set_id == recovery_set_id {
            return Ok(Some((record, sha256_bytes(&record_bytes))));
        }
    }
    Ok(None)
}

fn validate_generation_transition(
    deployment_id: Uuid,
    manifest: &RecoverySetManifest,
    source_manifest: &RecoverySetManifest,
    recovery_seal_report: &RecoverySealReport,
    admission_record: &RestoreAdmissionRecord,
) -> Result<ValidatedGenerationTransition, WitnessStoreError> {
    manifest
        .validate()
        .map_err(|_| WitnessStoreError::InvalidInput)?;
    source_manifest
        .validate()
        .map_err(|_| WitnessStoreError::InvalidInput)?;
    admission_record
        .validate()
        .map_err(|_| WitnessStoreError::InvalidInput)?;
    recovery_seal_report
        .validate_for_restore(source_manifest, recovery_seal_report.target_environment_id)
        .map_err(|_| WitnessStoreError::InvalidInput)?;
    let approval = admission_record
        .approval
        .as_ref()
        .ok_or(WitnessStoreError::InvalidInput)?;
    if manifest.deployment_id != deployment_id
        || source_manifest.deployment_id != deployment_id
        || manifest.recovery_set_id == source_manifest.recovery_set_id
        || admission_record.state != RestoreAdmissionState::Admitted
        || admission_record.deployment_id != deployment_id
        || admission_record.recovery_set_id != source_manifest.recovery_set_id
        || admission_record.target_environment_id != recovery_seal_report.target_environment_id
    {
        return Err(WitnessStoreError::InvalidInput);
    }
    let source_generations = match &source_manifest.security_evidence {
        RecoverySecurityEvidence::Captured { watermarks, .. } => watermarks
            .iter()
            .map(|watermark| (watermark.service, watermark.recovery_generation))
            .collect::<BTreeMap<_, _>>(),
        RecoverySecurityEvidence::Unavailable { .. } => {
            return Err(WitnessStoreError::InvalidInput);
        }
    };
    let sealed_sequences = recovery_seal_report
        .services
        .iter()
        .map(|service| (service.service, service.sealed_security_sequence))
        .collect::<BTreeMap<_, _>>();
    let next_watermarks = match &manifest.security_evidence {
        RecoverySecurityEvidence::Captured { watermarks, .. } => watermarks,
        RecoverySecurityEvidence::Unavailable { .. } => {
            return Err(WitnessStoreError::InvalidInput);
        }
    };
    if next_watermarks.iter().any(|watermark| {
        watermark.recovery_generation != recovery_seal_report.recovery_generation
            || sealed_sequences
                .get(&watermark.service)
                .is_none_or(|sealed_sequence| watermark.security_sequence < *sealed_sequence)
    }) {
        return Err(WitnessStoreError::InvalidInput);
    }
    let report_bytes =
        serde_json::to_vec(recovery_seal_report).map_err(|_| WitnessStoreError::InvalidInput)?;
    let admission_bytes =
        serde_json::to_vec(admission_record).map_err(|_| WitnessStoreError::InvalidInput)?;
    Ok(ValidatedGenerationTransition {
        record: WitnessGenerationTransition {
            source_recovery_set_id: source_manifest.recovery_set_id,
            target_environment_id: recovery_seal_report.target_environment_id,
            approval_id: approval.approval_id,
            recovery_seal_report_sha256: sha256_bytes(&report_bytes),
            admission_record_sha256: sha256_bytes(&admission_bytes),
        },
        source_generations,
        recovery_generation: recovery_seal_report.recovery_generation,
    })
}

fn verify_generation_advance(
    previous: &[crate::ServiceSecurityWatermark],
    next: &[crate::ServiceSecurityWatermark],
    transition: &ValidatedGenerationTransition,
) -> Result<(), WitnessStoreError> {
    let previous_by_service = previous
        .iter()
        .map(|watermark| (watermark.service, watermark))
        .collect::<BTreeMap<_, _>>();
    let next_by_service = next
        .iter()
        .map(|watermark| (watermark.service, watermark))
        .collect::<BTreeMap<_, _>>();
    if previous_by_service.keys().collect::<BTreeSet<_>>()
        != transition
            .source_generations
            .keys()
            .collect::<BTreeSet<_>>()
        || previous_by_service.keys().collect::<BTreeSet<_>>()
            != next_by_service.keys().collect::<BTreeSet<_>>()
    {
        return Err(WitnessStoreError::NonMonotonic);
    }
    for (service, previous_watermark) in previous_by_service {
        let source_generation = transition
            .source_generations
            .get(&service)
            .ok_or(WitnessStoreError::NonMonotonic)?;
        let next_watermark = next_by_service
            .get(&service)
            .ok_or(WitnessStoreError::NonMonotonic)?;
        if previous_watermark.recovery_generation != *source_generation
            || next_watermark.recovery_generation != transition.recovery_generation
            || next_watermark.recovery_generation == previous_watermark.recovery_generation
        {
            return Err(WitnessStoreError::NonMonotonic);
        }
    }
    Ok(())
}

fn verify_monotonic(
    previous: &[crate::ServiceSecurityWatermark],
    next: &[crate::ServiceSecurityWatermark],
) -> Result<(), WitnessStoreError> {
    let previous_by_service = previous
        .iter()
        .map(|watermark| (watermark.service, watermark))
        .collect::<BTreeMap<_, _>>();
    let next_by_service = next
        .iter()
        .map(|watermark| (watermark.service, watermark))
        .collect::<BTreeMap<_, _>>();
    if previous_by_service.keys().collect::<BTreeSet<_>>()
        != next_by_service.keys().collect::<BTreeSet<_>>()
    {
        return Err(WitnessStoreError::NonMonotonic);
    }
    for (service, previous_watermark) in previous_by_service {
        let next_watermark = next_by_service
            .get(&service)
            .ok_or(WitnessStoreError::NonMonotonic)?;
        if next_watermark.recovery_generation != previous_watermark.recovery_generation
            || next_watermark.security_sequence < previous_watermark.security_sequence
            || (next_watermark.security_sequence == previous_watermark.security_sequence
                && next_watermark.security_state_sha256 != previous_watermark.security_state_sha256)
        {
            return Err(WitnessStoreError::NonMonotonic);
        }
    }
    Ok(())
}

fn validate_record(
    record: &WitnessJournalRecord,
    deployment_id: Uuid,
) -> Result<(), WitnessStoreError> {
    if record.schema_version != WITNESS_RECORD_SCHEMA_VERSION
        || record.deployment_id != deployment_id
        || record.ordinal == 0
        || record.recovery_set_id.is_nil()
        || record.consistency_proof_id.is_nil()
        || record.recorded_at_unix == 0
        || record
            .previous_record_sha256
            .as_ref()
            .is_some_and(|digest| !valid_sha256(digest))
        || !valid_sha256(&record.witness_sha256)
        || record
            .generation_transition
            .as_ref()
            .is_some_and(|transition| {
                transition.source_recovery_set_id.is_nil()
                    || transition.target_environment_id.is_nil()
                    || transition.approval_id.is_nil()
                    || !valid_sha256(&transition.recovery_seal_report_sha256)
                    || !valid_sha256(&transition.admission_record_sha256)
            })
        || (record.ordinal == 1 && record.generation_transition.is_some())
    {
        return Err(WitnessStoreError::Corrupt);
    }
    Ok(())
}

fn journal_path(root: &Path, ordinal: u64, recovery_set_id: Uuid) -> PathBuf {
    root.join("journal")
        .join(format!("{ordinal:020}-{recovery_set_id}.json"))
}

fn replace_current(root: &Path, current: &CurrentWitnessRecord) -> Result<(), WitnessStoreError> {
    let next_path = root.join("current.next");
    let current_path = root.join("current.json");
    let previous_path = root.join("current.previous");
    if next_path.exists() || previous_path.exists() {
        return Err(WitnessStoreError::Corrupt);
    }
    persist_private(&next_path, current)?;
    if current_path.exists() {
        fs::rename(&current_path, &previous_path).map_err(|_| WitnessStoreError::Unavailable)?;
    }
    fs::rename(&next_path, &current_path).map_err(|_| WitnessStoreError::Unavailable)?;
    if previous_path.exists() {
        fs::remove_file(&previous_path).map_err(|_| WitnessStoreError::Unavailable)?;
    }
    Ok(())
}

fn recover_current(root: &Path) -> Result<(), WitnessStoreError> {
    let next_path = root.join("current.next");
    let current_path = root.join("current.json");
    let previous_path = root.join("current.previous");
    match (
        current_path.exists(),
        previous_path.exists(),
        next_path.exists(),
    ) {
        (false, false, false) | (true, false, false) => Ok(()),
        (false, false, true) => {
            load_private::<CurrentWitnessRecord>(&next_path)?;
            fs::rename(&next_path, &current_path).map_err(|_| WitnessStoreError::Unavailable)?;
            Ok(())
        }
        (false, true, false) => {
            load_private::<CurrentWitnessRecord>(&previous_path)?;
            fs::rename(&previous_path, &current_path)
                .map_err(|_| WitnessStoreError::Unavailable)?;
            Ok(())
        }
        (true, true, false) => {
            load_private::<CurrentWitnessRecord>(&current_path)?;
            fs::remove_file(&previous_path).map_err(|_| WitnessStoreError::Unavailable)?;
            Ok(())
        }
        (false, true, true) => {
            load_private::<CurrentWitnessRecord>(&next_path)?;
            fs::rename(&next_path, &current_path).map_err(|_| WitnessStoreError::Unavailable)?;
            fs::remove_file(&previous_path).map_err(|_| WitnessStoreError::Unavailable)?;
            Ok(())
        }
        (true, false, true) => {
            load_private::<CurrentWitnessRecord>(&next_path)?;
            fs::rename(&current_path, &previous_path)
                .map_err(|_| WitnessStoreError::Unavailable)?;
            fs::rename(&next_path, &current_path).map_err(|_| WitnessStoreError::Unavailable)?;
            fs::remove_file(&previous_path).map_err(|_| WitnessStoreError::Unavailable)?;
            Ok(())
        }
        _ => Err(WitnessStoreError::Corrupt),
    }
}

fn persist_private<T: Serialize>(path: &Path, value: &T) -> Result<(), WitnessStoreError> {
    let bytes = serde_json::to_vec_pretty(value).map_err(|_| WitnessStoreError::Corrupt)?;
    px_private_files::private::create_private(path, &bytes)
        .map_err(|_| WitnessStoreError::Unavailable)
}

fn load_private<T: for<'de> Deserialize<'de>>(path: &Path) -> Result<T, WitnessStoreError> {
    let bytes = read_private_bytes(path)?;
    serde_json::from_slice(&bytes).map_err(|_| WitnessStoreError::Corrupt)
}

fn read_private_bytes(path: &Path) -> Result<Vec<u8>, WitnessStoreError> {
    let bytes = px_private_files::private::read_private(path)
        .map_err(|_| WitnessStoreError::Unavailable)?;
    if bytes.is_empty() || bytes.len() > MAX_PRIVATE_STATE_BYTES {
        return Err(WitnessStoreError::Corrupt);
    }
    Ok(bytes.to_vec())
}

fn sha256_bytes(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}

fn current_unix_time() -> Result<u64, WitnessStoreError> {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|_| WitnessStoreError::Clock)
        .map(|duration| duration.as_secs())
}

fn make_private_directory(path: &Path) -> Result<(), WitnessStoreError> {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o700))
            .map_err(|_| WitnessStoreError::Unavailable)?;
    }
    #[cfg(windows)]
    {
        use std::{os::windows::process::CommandExt, process::Command};
        let identity = Command::new("whoami")
            .creation_flags(0x08000000)
            .output()
            .map_err(|_| WitnessStoreError::Unavailable)?;
        if !identity.status.success() {
            return Err(WitnessStoreError::Unavailable);
        }
        let access_grant = format!(
            "{}:(OI)(CI)F",
            String::from_utf8(identity.stdout)
                .map_err(|_| WitnessStoreError::Unavailable)?
                .trim()
        );
        let result = Command::new("icacls")
            .arg(path)
            .args(["/inheritance:r", "/grant:r", &access_grant])
            .creation_flags(0x08000000)
            .output()
            .map_err(|_| WitnessStoreError::Unavailable)?;
        if !result.status.success() {
            return Err(WitnessStoreError::Unavailable);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        BackupMember, BackupMemberState, BackupService, RecoverySealServiceReport, RecoverySetKind,
        RecoverySetStatus, RestoreAdmissionApproval, RetentionClass, ServiceSecurityWatermark,
        MANIFEST_SCHEMA_VERSION, RECOVERY_SEAL_REPORT_SCHEMA_VERSION,
        RESTORE_ADMISSION_RECORD_SCHEMA_VERSION,
    };

    fn manifest(
        deployment_id: Uuid,
        recovery_set_id: Uuid,
        generation: Uuid,
        sequence: u64,
    ) -> RecoverySetManifest {
        let services = [
            BackupService::Console,
            BackupService::Auth,
            BackupService::Desk,
        ];
        RecoverySetManifest {
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
            members: services
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
                .collect(),
            security_evidence: RecoverySecurityEvidence::Captured {
                consistency_proof_id: Uuid::new_v4(),
                external_key_ids: BTreeSet::new(),
                watermarks: services
                    .iter()
                    .map(|service| ServiceSecurityWatermark {
                        service: *service,
                        recovery_generation: generation,
                        security_sequence: sequence,
                        security_state_sha256: format!("{:064x}", sequence),
                    })
                    .collect(),
            },
            failure_code: None,
        }
    }

    #[test]
    fn journal_is_monotonic_hash_chained_and_idempotent() {
        let temporary_directory = tempfile::tempdir().unwrap();
        make_private_directory(temporary_directory.path()).unwrap();
        let deployment_id = Uuid::new_v4();
        let generation = Uuid::new_v4();
        let first_manifest = manifest(deployment_id, Uuid::new_v4(), generation, 10);
        let first_store =
            RecoveryWitnessStore::open(temporary_directory.path(), deployment_id).unwrap();
        let first = first_store.record_manifest(&first_manifest).unwrap();
        assert_eq!(first.ordinal, 1);
        let repeated = first_store.record_manifest(&first_manifest).unwrap();
        assert_eq!(repeated, first);
        drop(first_store);

        let second_manifest = manifest(deployment_id, Uuid::new_v4(), generation, 11);
        let second_store =
            RecoveryWitnessStore::open(temporary_directory.path(), deployment_id).unwrap();
        let second = second_store.record_manifest(&second_manifest).unwrap();
        assert_eq!(second.ordinal, 2);
        assert_ne!(second.record_sha256, first.record_sha256);
        drop(second_store);
        fs::remove_file(temporary_directory.path().join("current.json")).unwrap();
        let orphan_path = temporary_directory
            .path()
            .join("witnesses")
            .join(format!("{}.json", Uuid::new_v4()));
        px_private_files::private::create_private(&orphan_path, b"incomplete-witness").unwrap();
        RecoveryWitnessStore::open(temporary_directory.path(), deployment_id).unwrap();
        assert!(temporary_directory.path().join("current.json").is_file());
        assert!(!orphan_path.exists());
    }

    #[test]
    fn sequence_rollback_generation_change_and_tampering_fail_closed() {
        let temporary_directory = tempfile::tempdir().unwrap();
        make_private_directory(temporary_directory.path()).unwrap();
        let deployment_id = Uuid::new_v4();
        let generation = Uuid::new_v4();
        let store = RecoveryWitnessStore::open(temporary_directory.path(), deployment_id).unwrap();
        store
            .record_manifest(&manifest(deployment_id, Uuid::new_v4(), generation, 10))
            .unwrap();
        assert_eq!(
            store.record_manifest(&manifest(deployment_id, Uuid::new_v4(), generation, 9)),
            Err(WitnessStoreError::NonMonotonic)
        );
        assert_eq!(
            store.record_manifest(&manifest(deployment_id, Uuid::new_v4(), Uuid::new_v4(), 11)),
            Err(WitnessStoreError::NonMonotonic)
        );
        drop(store);
        fs::write(temporary_directory.path().join("unknown"), b"tamper").unwrap();
        assert!(matches!(
            RecoveryWitnessStore::open(temporary_directory.path(), deployment_id),
            Err(WitnessStoreError::Corrupt)
        ));
    }

    #[test]
    fn approved_recovery_seal_is_required_to_advance_generation() {
        let temporary_directory = tempfile::tempdir().unwrap();
        make_private_directory(temporary_directory.path()).unwrap();
        let deployment_id = Uuid::new_v4();
        let source_generation = Uuid::new_v4();
        let source_manifest = manifest(deployment_id, Uuid::new_v4(), source_generation, 10);
        let store = RecoveryWitnessStore::open(temporary_directory.path(), deployment_id).unwrap();
        store.record_manifest(&source_manifest).unwrap();

        let recovery_generation = Uuid::new_v4();
        let next_manifest = manifest(deployment_id, Uuid::new_v4(), recovery_generation, 12);
        assert_eq!(
            store.record_manifest(&next_manifest),
            Err(WitnessStoreError::NonMonotonic)
        );
        let source_watermarks = match &source_manifest.security_evidence {
            RecoverySecurityEvidence::Captured { watermarks, .. } => watermarks,
            RecoverySecurityEvidence::Unavailable { .. } => unreachable!(),
        };
        let recovery_seal_report = RecoverySealReport {
            schema_version: RECOVERY_SEAL_REPORT_SCHEMA_VERSION,
            deployment_id,
            recovery_set_id: source_manifest.recovery_set_id,
            target_environment_id: Uuid::new_v4(),
            recovery_generation,
            started_at_unix: 200,
            completed_at_unix: 201,
            source_security_evidence_sha256: sha256_bytes(
                &serde_json::to_vec(&source_manifest.security_evidence).unwrap(),
            ),
            services: source_watermarks
                .iter()
                .map(|watermark| {
                    let sealed_security_sequence = watermark.security_sequence + 1;
                    RecoverySealServiceReport {
                        service: watermark.service,
                        source_recovery_generation: watermark.recovery_generation,
                        source_security_sequence: watermark.security_sequence,
                        source_security_state_sha256: watermark.security_state_sha256.clone(),
                        sealed_security_sequence,
                        sealed_security_state_sha256: sha256_bytes(
                            format!(
                                "{}|{}|{}|{}",
                                deployment_id,
                                service_name(watermark.service),
                                recovery_generation,
                                sealed_security_sequence
                            )
                            .as_bytes(),
                        ),
                        revoked_session_records: 1,
                        invalidated_grant_records: 1,
                        invalidated_control_records: 1,
                    }
                })
                .collect(),
            old_sessions_revoked: true,
            old_grants_invalidated: true,
            pending_control_invalidated: true,
            admission_required: true,
        };
        let evidence_sha256 = "e".repeat(64);
        let approval_id = Uuid::new_v4();
        let admission_record = RestoreAdmissionRecord {
            schema_version: RESTORE_ADMISSION_RECORD_SCHEMA_VERSION,
            deployment_id,
            recovery_set_id: source_manifest.recovery_set_id,
            target_environment_id: recovery_seal_report.target_environment_id,
            state: RestoreAdmissionState::Admitted,
            revision: 3,
            created_at_unix: 202,
            updated_at_unix: 204,
            evidence_sha256: Some(evidence_sha256.clone()),
            blockers: BTreeSet::new(),
            approval: Some(RestoreAdmissionApproval {
                approval_id,
                administrator_id: Uuid::new_v4(),
                approved_at_unix: 204,
                evidence_sha256,
            }),
        };
        let recorded = store
            .record_manifest_after_recovery(
                &next_manifest,
                &source_manifest,
                &recovery_seal_report,
                &admission_record,
            )
            .unwrap();
        assert_eq!(recorded.ordinal, 2);
    }

    fn service_name(service: BackupService) -> &'static str {
        match service {
            BackupService::Console => "console",
            BackupService::Auth => "auth",
            BackupService::Desk => "desk",
        }
    }
}
