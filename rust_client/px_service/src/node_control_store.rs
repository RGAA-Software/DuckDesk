use px_deployment_identity::{DeploymentIdentityVerifier, DeploymentKind, DeploymentTrustStore};
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use uuid::Uuid;
use zeroize::{Zeroize, Zeroizing};

const CONFIG_DIRECTORY: &str = "node-control";
const CONFIG_FILE: &str = "configuration.dpapi";
const TELEMETRY_BACKLOG_FILE: &str = "telemetry-backlog.dpapi";
const FILE_TRANSFER_OUTBOX_FILE: &str = "file-transfer-outbox.dpapi";
const DEPLOYMENT_IDENTITY_WATERMARK_FILE: &str = "deployment-identity-watermark.dpapi";
const MAX_INPUT_BYTES: u64 = 16 * 1024;
const MAX_TELEMETRY_BACKLOG_BYTES: u64 = 4 * 1024 * 1024;
const MAX_TELEMETRY_BACKLOG_SAMPLES: usize = 240;
const MAX_FILE_TRANSFER_OUTBOX_BYTES: u64 = 8 * 1024 * 1024;
const MAX_FILE_TRANSFER_OUTBOX_REPORTS: usize = 4096;

#[derive(Debug)]
pub struct NodeControlConfiguration {
    pub endpoint: String,
    pub node_token: Zeroizing<String>,
    pub public_host: String,
    pub deployment_id: Uuid,
    pub deployment_kind: DeploymentKind,
    pub deployment_trust_store: DeploymentTrustStore,
    pub minimum_certificate_version: u64,
    pub minimum_descriptor_revision: u64,
    pub minimum_trust_epoch: u64,
}

impl Clone for NodeControlConfiguration {
    fn clone(&self) -> Self {
        Self {
            endpoint: self.endpoint.clone(),
            node_token: Zeroizing::new(self.node_token.to_string()),
            public_host: self.public_host.clone(),
            deployment_id: self.deployment_id,
            deployment_kind: self.deployment_kind,
            deployment_trust_store: self.deployment_trust_store.clone(),
            minimum_certificate_version: self.minimum_certificate_version,
            minimum_descriptor_revision: self.minimum_descriptor_revision,
            minimum_trust_epoch: self.minimum_trust_epoch,
        }
    }
}

impl PartialEq for NodeControlConfiguration {
    fn eq(&self, other: &Self) -> bool {
        self.endpoint == other.endpoint
            && self.node_token.as_str() == other.node_token.as_str()
            && self.public_host == other.public_host
            && self.deployment_id == other.deployment_id
            && self.deployment_kind == other.deployment_kind
            && self.deployment_trust_store == other.deployment_trust_store
            && self.minimum_certificate_version == other.minimum_certificate_version
            && self.minimum_descriptor_revision == other.minimum_descriptor_revision
            && self.minimum_trust_epoch == other.minimum_trust_epoch
    }
}

impl Eq for NodeControlConfiguration {}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct StoredConfiguration {
    schema_version: u32,
    endpoint: String,
    node_token: String,
    public_host: String,
    deployment_id: Uuid,
    deployment_kind: DeploymentKind,
    deployment_trust_store: DeploymentTrustStore,
    minimum_certificate_version: u64,
    minimum_descriptor_revision: u64,
    minimum_trust_epoch: u64,
}

impl Drop for StoredConfiguration {
    fn drop(&mut self) {
        self.node_token.zeroize();
    }
}

#[derive(Serialize)]
struct StoredConfigurationRef<'a> {
    schema_version: u32,
    endpoint: &'a str,
    node_token: &'a str,
    public_host: &'a str,
    deployment_id: Uuid,
    deployment_kind: DeploymentKind,
    deployment_trust_store: &'a DeploymentTrustStore,
    minimum_certificate_version: u64,
    minimum_descriptor_revision: u64,
    minimum_trust_epoch: u64,
}

impl StoredConfiguration {
    fn into_runtime(mut self) -> Result<NodeControlConfiguration, String> {
        if self.schema_version != 2 {
            return Err("unsupported node-control configuration schema".into());
        }
        let runtime = NodeControlConfiguration {
            endpoint: std::mem::take(&mut self.endpoint),
            node_token: Zeroizing::new(std::mem::take(&mut self.node_token)),
            public_host: std::mem::take(&mut self.public_host),
            deployment_id: self.deployment_id,
            deployment_kind: self.deployment_kind,
            deployment_trust_store: self.deployment_trust_store.clone(),
            minimum_certificate_version: self.minimum_certificate_version,
            minimum_descriptor_revision: self.minimum_descriptor_revision,
            minimum_trust_epoch: self.minimum_trust_epoch,
        };
        runtime.validate()?;
        Ok(runtime)
    }
}

impl NodeControlConfiguration {
    pub fn validate(&self) -> Result<(), String> {
        validate_endpoint(&self.endpoint)?;
        validate_token(&self.node_token)?;
        validate_public_host(&self.public_host)?;
        if self.deployment_id.is_nil()
            || self.minimum_certificate_version == 0
            || self.minimum_descriptor_revision == 0
            || self.minimum_trust_epoch == 0
            || self.deployment_trust_store.trust_epoch != self.minimum_trust_epoch
            || DeploymentIdentityVerifier::new(&self.deployment_trust_store).is_err()
        {
            return Err("node-control deployment identity configuration is invalid".into());
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct DeploymentIdentityWatermark {
    schema_version: u32,
    pub deployment_id: Uuid,
    pub deployment_kind: DeploymentKind,
    pub certificate_version: u64,
    pub descriptor_revision: u64,
    pub trust_epoch: u64,
}

impl DeploymentIdentityWatermark {
    pub(crate) fn new(
        deployment_id: Uuid,
        deployment_kind: DeploymentKind,
        certificate_version: u64,
        descriptor_revision: u64,
        trust_epoch: u64,
    ) -> Result<Self, String> {
        let watermark = Self {
            schema_version: 1,
            deployment_id,
            deployment_kind,
            certificate_version,
            descriptor_revision,
            trust_epoch,
        };
        watermark.validate()?;
        Ok(watermark)
    }

    fn validate(&self) -> Result<(), String> {
        if self.schema_version != 1
            || self.deployment_id.is_nil()
            || self.certificate_version == 0
            || self.descriptor_revision == 0
            || self.trust_epoch == 0
        {
            return Err("protected deployment identity watermark is invalid".into());
        }
        Ok(())
    }

    pub(crate) fn allows(&self, candidate: &Self) -> bool {
        self.deployment_id == candidate.deployment_id
            && self.deployment_kind == candidate.deployment_kind
            && candidate.certificate_version >= self.certificate_version
            && candidate.descriptor_revision >= self.descriptor_revision
            && candidate.trust_epoch >= self.trust_epoch
    }
}

#[derive(Clone, Debug)]
pub struct NodeControlStore {
    directory: PathBuf,
    file_path: PathBuf,
    identity_watermark_path: PathBuf,
}

impl NodeControlStore {
    pub fn new(data_root: PathBuf) -> Self {
        let directory = data_root.join(CONFIG_DIRECTORY);
        Self {
            file_path: directory.join(CONFIG_FILE),
            identity_watermark_path: directory.join(DEPLOYMENT_IDENTITY_WATERMARK_FILE),
            directory,
        }
    }

    pub fn load(&self) -> Result<Option<NodeControlConfiguration>, String> {
        if !self.file_path.exists() {
            return Ok(None);
        }
        platform::ensure_private_directory(&self.directory)?;
        reject_reparse_point(&self.file_path)?;
        let encrypted = std::fs::read(&self.file_path)
            .map_err(|_| "cannot read protected node-control configuration".to_string())?;
        let mut plaintext = platform::unseal(&encrypted)?;
        let decoded = serde_json::from_slice::<StoredConfiguration>(&plaintext);
        plaintext.zeroize();
        decoded
            .map_err(|_| "invalid protected node-control configuration".to_string())?
            .into_runtime()
            .map(Some)
    }

    pub fn save(&self, configuration: &NodeControlConfiguration) -> Result<(), String> {
        configuration.validate()?;
        platform::ensure_private_directory(&self.directory)?;
        let stored = StoredConfigurationRef {
            schema_version: 2,
            endpoint: &configuration.endpoint,
            node_token: configuration.node_token.as_str(),
            public_host: &configuration.public_host,
            deployment_id: configuration.deployment_id,
            deployment_kind: configuration.deployment_kind,
            deployment_trust_store: &configuration.deployment_trust_store,
            minimum_certificate_version: configuration.minimum_certificate_version,
            minimum_descriptor_revision: configuration.minimum_descriptor_revision,
            minimum_trust_epoch: configuration.minimum_trust_epoch,
        };
        let mut plaintext = serde_json::to_vec(&stored)
            .map_err(|_| "cannot serialize node-control configuration".to_string())?;
        let encrypted = platform::seal(&plaintext);
        plaintext.zeroize();
        let encrypted = encrypted?;
        let pending = self.file_path.with_extension("dpapi.pending");
        std::fs::write(&pending, encrypted)
            .map_err(|_| "cannot write protected node-control configuration".to_string())?;
        platform::replace_file(&pending, &self.file_path)
    }

    pub fn clear(&self) -> Result<(), String> {
        if !self.directory.exists() {
            return Ok(());
        }
        platform::ensure_private_directory(&self.directory)?;
        for path in [&self.file_path, &self.identity_watermark_path] {
            match std::fs::remove_file(path) {
                Ok(()) => {}
                Err(error) if error.kind() == io::ErrorKind::NotFound => {}
                Err(_) => return Err("cannot remove protected node-control state".into()),
            }
        }
        Ok(())
    }

    pub(crate) fn load_identity_watermark(
        &self,
    ) -> Result<Option<DeploymentIdentityWatermark>, String> {
        if !self.identity_watermark_path.exists() {
            return Ok(None);
        }
        platform::ensure_private_directory(&self.directory)?;
        reject_reparse_point(&self.identity_watermark_path)?;
        let encrypted = std::fs::read(&self.identity_watermark_path)
            .map_err(|_| "cannot read protected deployment identity watermark".to_string())?;
        let mut plaintext = platform::unseal(&encrypted)?;
        let decoded = serde_json::from_slice::<DeploymentIdentityWatermark>(&plaintext);
        plaintext.zeroize();
        let watermark =
            decoded.map_err(|_| "invalid protected deployment identity watermark".to_string())?;
        watermark.validate()?;
        Ok(Some(watermark))
    }

    pub(crate) fn save_identity_watermark(
        &self,
        watermark: &DeploymentIdentityWatermark,
    ) -> Result<(), String> {
        watermark.validate()?;
        platform::ensure_private_directory(&self.directory)?;
        let mut plaintext = serde_json::to_vec(watermark)
            .map_err(|_| "cannot serialize deployment identity watermark".to_string())?;
        let encrypted = platform::seal(&plaintext);
        plaintext.zeroize();
        let encrypted = encrypted?;
        let pending = self.identity_watermark_path.with_extension("dpapi.pending");
        std::fs::write(&pending, encrypted)
            .map_err(|_| "cannot write protected deployment identity watermark".to_string())?;
        platform::replace_file(&pending, &self.identity_watermark_path)
    }

    #[cfg(test)]
    fn file_path(&self) -> &Path {
        &self.file_path
    }

    #[cfg(test)]
    fn identity_watermark_path(&self) -> &Path {
        &self.identity_watermark_path
    }
}

#[derive(Clone, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct StoredTelemetrySample {
    pub sample_id: Uuid,
    pub telemetry: px_node_protocol::NodeTelemetry,
}

#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct StoredTelemetryBacklog {
    schema_version: u32,
    samples: Vec<StoredTelemetrySample>,
}

#[derive(Clone, Debug)]
pub(crate) struct TelemetryBacklogStore {
    directory: PathBuf,
    file_path: PathBuf,
}

impl TelemetryBacklogStore {
    pub fn new(data_root: PathBuf) -> Self {
        let directory = data_root.join(CONFIG_DIRECTORY);
        Self {
            file_path: directory.join(TELEMETRY_BACKLOG_FILE),
            directory,
        }
    }

    pub fn append(&self, telemetry: px_node_protocol::NodeTelemetry) -> Result<(), String> {
        let mut samples = self.load_all()?;
        let retention_start = chrono::Utc::now() - chrono::TimeDelta::days(7);
        samples.retain(|sample| sample.telemetry.sampled_at >= retention_start);
        if samples
            .last()
            .is_some_and(|sample| sample.telemetry.sampled_at >= telemetry.sampled_at)
        {
            return Err("node telemetry backlog samples must be strictly ordered".into());
        }
        if samples.len() == MAX_TELEMETRY_BACKLOG_SAMPLES {
            samples.remove(0);
        }
        samples.push(StoredTelemetrySample {
            sample_id: Uuid::new_v4(),
            telemetry,
        });
        self.save_all(&samples)
    }

    pub fn pending(&self, limit: usize) -> Result<Vec<StoredTelemetrySample>, String> {
        if limit == 0 || limit > 4 {
            return Err("node telemetry backlog batch limit is invalid".into());
        }
        let retention_start = chrono::Utc::now() - chrono::TimeDelta::days(7);
        Ok(self
            .load_all()?
            .into_iter()
            .filter(|sample| sample.telemetry.sampled_at >= retention_start)
            .take(limit)
            .collect())
    }

    pub fn acknowledge(&self, sample_ids: &[Uuid]) -> Result<(), String> {
        if sample_ids.is_empty() || sample_ids.len() > 4 {
            return Err("node telemetry acknowledgement is invalid".into());
        }
        let acknowledged = sample_ids.iter().copied().collect::<HashSet<_>>();
        if acknowledged.len() != sample_ids.len() {
            return Err("node telemetry acknowledgement contains duplicates".into());
        }
        let mut samples = self.load_all()?;
        if !acknowledged
            .iter()
            .all(|sample_id| samples.iter().any(|sample| sample.sample_id == *sample_id))
        {
            return Err("node telemetry acknowledgement does not match the backlog".into());
        }
        samples.retain(|sample| !acknowledged.contains(&sample.sample_id));
        self.save_all(&samples)
    }

    fn load_all(&self) -> Result<Vec<StoredTelemetrySample>, String> {
        if !self.file_path.exists() {
            return Ok(Vec::new());
        }
        platform::ensure_private_directory(&self.directory)?;
        reject_reparse_point(&self.file_path)?;
        let metadata = std::fs::metadata(&self.file_path)
            .map_err(|_| "cannot inspect protected node telemetry backlog".to_string())?;
        if metadata.len() == 0 || metadata.len() > MAX_TELEMETRY_BACKLOG_BYTES {
            return Err("protected node telemetry backlog size is invalid".into());
        }
        let encrypted = std::fs::read(&self.file_path)
            .map_err(|_| "cannot read protected node telemetry backlog".to_string())?;
        let mut plaintext = platform::unseal(&encrypted)?;
        let decoded = serde_json::from_slice::<StoredTelemetryBacklog>(&plaintext);
        plaintext.zeroize();
        let backlog =
            decoded.map_err(|_| "invalid protected node telemetry backlog".to_string())?;
        if backlog.schema_version != 1 || backlog.samples.len() > MAX_TELEMETRY_BACKLOG_SAMPLES {
            return Err("unsupported protected node telemetry backlog".into());
        }
        let mut sample_ids = HashSet::with_capacity(backlog.samples.len());
        let valid = backlog.samples.iter().enumerate().all(|(index, sample)| {
            !sample.sample_id.is_nil()
                && sample_ids.insert(sample.sample_id)
                && index.checked_sub(1).is_none_or(|previous_index| {
                    backlog.samples[previous_index].telemetry.sampled_at
                        < sample.telemetry.sampled_at
                })
        });
        if !valid {
            return Err("protected node telemetry backlog ordering is invalid".into());
        }
        Ok(backlog.samples)
    }

    fn save_all(&self, samples: &[StoredTelemetrySample]) -> Result<(), String> {
        platform::ensure_private_directory(&self.directory)?;
        let mut plaintext = serde_json::to_vec(&StoredTelemetryBacklog {
            schema_version: 1,
            samples: samples.to_vec(),
        })
        .map_err(|_| "cannot serialize node telemetry backlog".to_string())?;
        let encrypted = platform::seal(&plaintext);
        plaintext.zeroize();
        let encrypted = encrypted?;
        let pending = self.file_path.with_extension("dpapi.pending");
        std::fs::write(&pending, encrypted)
            .map_err(|_| "cannot write protected node telemetry backlog".to_string())?;
        platform::replace_file(&pending, &self.file_path)
    }

    #[cfg(test)]
    fn file_path(&self) -> &Path {
        &self.file_path
    }
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
enum StoredFileTransferOutcome {
    Progress,
    Completed { received_sha256: [u8; 32] },
    Failed { reason: StoredFileTransferFailure },
    Cancelled,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(rename_all = "snake_case")]
enum StoredFileTransferFailure {
    TransportLost,
    HashMismatch,
    PolicyRevoked,
    IoError,
    SourceChanged,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct StoredFileTransferReport {
    pub transfer_id: Uuid,
    pub sequence: u64,
    pub transferred_bytes: u64,
    outcome: StoredFileTransferOutcome,
}

impl StoredFileTransferReport {
    fn new(transfer_id: Uuid, progress: &px_node_protocol::TransferProgress) -> Self {
        let outcome = match progress.outcome {
            px_node_protocol::TransferOutcome::Progress => StoredFileTransferOutcome::Progress,
            px_node_protocol::TransferOutcome::Completed { received_sha256 } => {
                StoredFileTransferOutcome::Completed { received_sha256 }
            }
            px_node_protocol::TransferOutcome::Failed { reason } => {
                StoredFileTransferOutcome::Failed {
                    reason: match reason {
                        px_node_protocol::TransferFailure::TransportLost => {
                            StoredFileTransferFailure::TransportLost
                        }
                        px_node_protocol::TransferFailure::HashMismatch => {
                            StoredFileTransferFailure::HashMismatch
                        }
                        px_node_protocol::TransferFailure::PolicyRevoked => {
                            StoredFileTransferFailure::PolicyRevoked
                        }
                        px_node_protocol::TransferFailure::IoError => {
                            StoredFileTransferFailure::IoError
                        }
                        px_node_protocol::TransferFailure::SourceChanged => {
                            StoredFileTransferFailure::SourceChanged
                        }
                    },
                }
            }
            px_node_protocol::TransferOutcome::Cancelled => StoredFileTransferOutcome::Cancelled,
        };
        Self {
            transfer_id,
            sequence: progress.sequence,
            transferred_bytes: progress.transferred_bytes,
            outcome,
        }
    }

    pub fn progress(&self) -> px_node_protocol::TransferProgress {
        let outcome = match self.outcome {
            StoredFileTransferOutcome::Progress => px_node_protocol::TransferOutcome::Progress,
            StoredFileTransferOutcome::Completed { received_sha256 } => {
                px_node_protocol::TransferOutcome::Completed { received_sha256 }
            }
            StoredFileTransferOutcome::Failed { reason } => {
                px_node_protocol::TransferOutcome::Failed {
                    reason: match reason {
                        StoredFileTransferFailure::TransportLost => {
                            px_node_protocol::TransferFailure::TransportLost
                        }
                        StoredFileTransferFailure::HashMismatch => {
                            px_node_protocol::TransferFailure::HashMismatch
                        }
                        StoredFileTransferFailure::PolicyRevoked => {
                            px_node_protocol::TransferFailure::PolicyRevoked
                        }
                        StoredFileTransferFailure::IoError => {
                            px_node_protocol::TransferFailure::IoError
                        }
                        StoredFileTransferFailure::SourceChanged => {
                            px_node_protocol::TransferFailure::SourceChanged
                        }
                    },
                }
            }
            StoredFileTransferOutcome::Cancelled => px_node_protocol::TransferOutcome::Cancelled,
        };
        px_node_protocol::TransferProgress {
            sequence: self.sequence,
            transferred_bytes: self.transferred_bytes,
            outcome,
        }
    }

    fn terminal(&self) -> bool {
        !matches!(self.outcome, StoredFileTransferOutcome::Progress)
    }
}

#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
struct StoredFileTransferOutbox {
    schema_version: u32,
    reports: Vec<StoredFileTransferReport>,
}

#[derive(Clone, Debug)]
pub(crate) struct FileTransferOutboxStore {
    directory: PathBuf,
    file_path: PathBuf,
}

impl FileTransferOutboxStore {
    pub fn new(data_root: PathBuf) -> Self {
        let directory = data_root.join(CONFIG_DIRECTORY);
        Self {
            file_path: directory.join(FILE_TRANSFER_OUTBOX_FILE),
            directory,
        }
    }

    pub fn append(
        &self,
        transfer_id: Uuid,
        progress: &px_node_protocol::TransferProgress,
    ) -> Result<(), String> {
        if transfer_id.is_nil()
            || progress.sequence == 0
            || i64::try_from(progress.sequence).is_err()
        {
            return Err("file transfer outbox report is invalid".into());
        }
        let candidate = StoredFileTransferReport::new(transfer_id, progress);
        let mut reports = self.load_all()?;
        if reports.iter().any(|report| report == &candidate) {
            return Ok(());
        }
        if reports.len() >= MAX_FILE_TRANSFER_OUTBOX_REPORTS {
            return Err("file transfer outbox capacity is exhausted".into());
        }
        if let Some(previous) = reports
            .iter()
            .rev()
            .find(|report| report.transfer_id == transfer_id)
        {
            if previous.terminal()
                || candidate.sequence <= previous.sequence
                || candidate.transferred_bytes < previous.transferred_bytes
            {
                return Err("file transfer outbox report is not monotonic".into());
            }
        }
        reports.push(candidate);
        self.save_all(&reports)
    }

    pub fn pending(&self, limit: usize) -> Result<Vec<StoredFileTransferReport>, String> {
        if limit == 0 || limit > 32 {
            return Err("file transfer outbox batch limit is invalid".into());
        }
        Ok(self.load_all()?.into_iter().take(limit).collect())
    }

    pub fn acknowledge(&self, transfer_id: Uuid, sequence: u64) -> Result<(), String> {
        let mut reports = self.load_all()?;
        let original_length = reports.len();
        reports.retain(|report| report.transfer_id != transfer_id || report.sequence != sequence);
        if reports.len() == original_length {
            return Ok(());
        }
        self.save_all(&reports)
    }

    pub fn reject_transfer(&self, transfer_id: Uuid) -> Result<usize, String> {
        let mut reports = self.load_all()?;
        let original_length = reports.len();
        reports.retain(|report| report.transfer_id != transfer_id);
        let removed = original_length - reports.len();
        if removed != 0 {
            self.save_all(&reports)?;
        }
        Ok(removed)
    }

    fn load_all(&self) -> Result<Vec<StoredFileTransferReport>, String> {
        if !self.file_path.exists() {
            return Ok(Vec::new());
        }
        platform::ensure_private_directory(&self.directory)?;
        reject_reparse_point(&self.file_path)?;
        let metadata = std::fs::metadata(&self.file_path)
            .map_err(|_| "cannot inspect protected file transfer outbox".to_string())?;
        if metadata.len() == 0 || metadata.len() > MAX_FILE_TRANSFER_OUTBOX_BYTES {
            return Err("protected file transfer outbox size is invalid".into());
        }
        let encrypted = std::fs::read(&self.file_path)
            .map_err(|_| "cannot read protected file transfer outbox".to_string())?;
        let mut plaintext = platform::unseal(&encrypted)?;
        let decoded = serde_json::from_slice::<StoredFileTransferOutbox>(&plaintext);
        plaintext.zeroize();
        let outbox = decoded.map_err(|_| "invalid protected file transfer outbox".to_string())?;
        if outbox.schema_version != 1 || outbox.reports.len() > MAX_FILE_TRANSFER_OUTBOX_REPORTS {
            return Err("unsupported protected file transfer outbox".into());
        }
        validate_file_transfer_reports(&outbox.reports)?;
        Ok(outbox.reports)
    }

    fn save_all(&self, reports: &[StoredFileTransferReport]) -> Result<(), String> {
        platform::ensure_private_directory(&self.directory)?;
        let mut plaintext = serde_json::to_vec(&StoredFileTransferOutbox {
            schema_version: 1,
            reports: reports.to_vec(),
        })
        .map_err(|_| "cannot serialize file transfer outbox".to_string())?;
        if plaintext.len() as u64 > MAX_FILE_TRANSFER_OUTBOX_BYTES {
            plaintext.zeroize();
            return Err("file transfer outbox capacity is exhausted".into());
        }
        let encrypted = platform::seal(&plaintext);
        plaintext.zeroize();
        let encrypted = encrypted?;
        let pending = self.file_path.with_extension("dpapi.pending");
        std::fs::write(&pending, encrypted)
            .map_err(|_| "cannot write protected file transfer outbox".to_string())?;
        platform::replace_file(&pending, &self.file_path)
    }

    #[cfg(test)]
    fn file_path(&self) -> &Path {
        &self.file_path
    }
}

fn validate_file_transfer_reports(reports: &[StoredFileTransferReport]) -> Result<(), String> {
    let mut latest = std::collections::HashMap::<Uuid, (u64, u64, bool)>::new();
    let valid = reports.iter().all(|report| {
        if report.transfer_id.is_nil()
            || report.sequence == 0
            || i64::try_from(report.sequence).is_err()
        {
            return false;
        }
        let previous = latest.entry(report.transfer_id).or_insert((0, 0, false));
        if previous.2 || report.sequence <= previous.0 || report.transferred_bytes < previous.1 {
            return false;
        }
        *previous = (report.sequence, report.transferred_bytes, report.terminal());
        true
    });
    valid
        .then_some(())
        .ok_or_else(|| "protected file transfer outbox ordering is invalid".to_string())
}

pub fn configure_from_stdin() -> Result<(), String> {
    let mut plaintext = Vec::new();
    std::io::stdin()
        .take(MAX_INPUT_BYTES + 1)
        .read_to_end(&mut plaintext)
        .map_err(|_| "cannot read node-control configuration from stdin".to_string())?;
    if plaintext.is_empty() || plaintext.len() as u64 > MAX_INPUT_BYTES {
        plaintext.zeroize();
        return Err("node-control configuration input must be between 1 byte and 16 KiB".into());
    }
    let decoded = serde_json::from_slice::<StoredConfiguration>(&plaintext);
    plaintext.zeroize();
    let configuration = decoded
        .map_err(|_| "invalid node-control configuration input".to_string())?
        .into_runtime()?;
    installed_store().save(&configuration)
}

pub fn clear_installed_configuration() -> Result<(), String> {
    installed_store().clear()
}

fn installed_store() -> NodeControlStore {
    NodeControlStore::new(service_core::windows_util::default_service_data_root())
}

fn validate_endpoint(value: &str) -> Result<(), String> {
    if value.trim() != value || value.len() > 2048 {
        return Err("node-control endpoint is invalid".into());
    }
    let endpoint = url::Url::parse(value).map_err(|_| "node-control endpoint is invalid")?;
    let loopback_development = endpoint.scheme() == "ws"
        && endpoint
            .host_str()
            .and_then(|host| host.parse::<std::net::IpAddr>().ok())
            .is_some_and(|address| address.is_loopback());
    if endpoint.scheme() != "wss" && !loopback_development {
        return Err(
            "node-control endpoint must use wss (ws is limited to loopback development)".into(),
        );
    }
    if endpoint.host().is_none()
        || !endpoint.username().is_empty()
        || endpoint.password().is_some()
        || endpoint.query().is_some()
        || endpoint.fragment().is_some()
        || endpoint.path() != "/api/console/node-control"
    {
        return Err("node-control endpoint must be an exact Console node-control URL".into());
    }
    Ok(())
}

fn validate_token(value: &str) -> Result<(), String> {
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err("node token must be 64 lowercase hexadecimal characters".into());
    }
    Ok(())
}

fn validate_public_host(value: &str) -> Result<(), String> {
    if value.is_empty()
        || value.trim() != value
        || value.contains(['/', '?', '#', '@'])
        || url::Host::parse(value).is_err()
    {
        return Err("node public host must be a hostname or IP address without a port".into());
    }
    match url::Host::parse(value).map_err(|_| "node public host is invalid")? {
        url::Host::Ipv4(address)
            if address.is_unspecified() || address.is_multicast() || address.is_broadcast() =>
        {
            Err("node public host must be a usable destination".into())
        }
        url::Host::Ipv6(address) if address.is_unspecified() || address.is_multicast() => {
            Err("node public host must be a usable destination".into())
        }
        _ => Ok(()),
    }
}

fn reject_reparse_point(path: &Path) -> Result<(), String> {
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        let metadata = std::fs::symlink_metadata(path)
            .map_err(|_| "node-control configuration is unavailable".to_string())?;
        if !metadata.is_file() || metadata.file_attributes() & 0x400 != 0 {
            return Err("node-control configuration reparse point refused".into());
        }
    }
    #[cfg(not(windows))]
    if !std::fs::symlink_metadata(path)
        .map_err(|_| "node-control configuration is unavailable".to_string())?
        .is_file()
    {
        return Err("node-control configuration must be a regular file".into());
    }
    Ok(())
}

#[cfg(windows)]
mod platform {
    use super::*;
    use std::os::windows::ffi::OsStrExt;
    use windows::core::{PCWSTR, PWSTR};
    use windows::Win32::Foundation::{LocalFree, HLOCAL};
    use windows::Win32::Security::Authorization::{
        ConvertSecurityDescriptorToStringSecurityDescriptorW,
        ConvertStringSecurityDescriptorToSecurityDescriptorW,
    };
    use windows::Win32::Security::Cryptography::{
        CryptProtectData, CryptUnprotectData, CRYPTPROTECT_LOCAL_MACHINE,
        CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
    };
    use windows::Win32::Security::{
        GetFileSecurityW, GetSecurityDescriptorControl, DACL_SECURITY_INFORMATION,
        PSECURITY_DESCRIPTOR, SECURITY_ATTRIBUTES, SE_DACL_PROTECTED,
    };
    use windows::Win32::Storage::FileSystem::{
        CreateDirectoryW, MoveFileExW, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH,
    };

    struct ProtectedBlob(CRYPT_INTEGER_BLOB);

    impl Drop for ProtectedBlob {
        fn drop(&mut self) {
            if !self.0.pbData.is_null() {
                unsafe {
                    std::slice::from_raw_parts_mut(self.0.pbData, self.0.cbData as usize).zeroize();
                    LocalFree(Some(HLOCAL(self.0.pbData.cast())));
                }
            }
        }
    }

    struct Descriptor(PSECURITY_DESCRIPTOR);

    impl Drop for Descriptor {
        fn drop(&mut self) {
            if !self.0 .0.is_null() {
                unsafe {
                    LocalFree(Some(HLOCAL(self.0 .0)));
                }
            }
        }
    }

    struct LocalWideString(PWSTR);

    impl Drop for LocalWideString {
        fn drop(&mut self) {
            if !self.0.is_null() {
                unsafe {
                    LocalFree(Some(HLOCAL(self.0.as_ptr().cast())));
                }
            }
        }
    }

    fn canonical_dacl(descriptor: PSECURITY_DESCRIPTOR) -> Result<String, String> {
        let mut text = LocalWideString(PWSTR::null());
        unsafe {
            ConvertSecurityDescriptorToStringSecurityDescriptorW(
                descriptor,
                1,
                DACL_SECURITY_INFORMATION,
                &mut text.0,
                None,
            )
        }
        .map_err(|_| "node-control directory ACL is invalid".to_string())?;
        let text = unsafe { text.0.to_string() }
            .map_err(|_| "node-control directory ACL encoding is invalid".to_string())?;
        let ace_offset = text
            .find('(')
            .ok_or_else(|| "node-control directory has no restrictive ACL".to_string())?;
        let ace_text = &text[ace_offset..];
        let mut aces: Vec<&str> = ace_text.split_inclusive(')').collect();
        if !text.starts_with("D:")
            || aces.is_empty()
            || aces.iter().map(|ace| ace.len()).sum::<usize>() != ace_text.len()
            || aces
                .iter()
                .any(|ace| !ace.starts_with('(') || !ace.ends_with(')'))
        {
            return Err("node-control directory ACL is invalid".into());
        }
        aces.sort_unstable();
        Ok(aces.concat())
    }

    fn file_security_descriptor(name: &[u16]) -> Result<Vec<u8>, String> {
        let mut required = 0_u32;
        let _ = unsafe {
            GetFileSecurityW(
                PCWSTR(name.as_ptr()),
                DACL_SECURITY_INFORMATION.0,
                None,
                0,
                &mut required,
            )
        };
        if required == 0 {
            return Err("node-control directory ACL is unavailable".into());
        }
        let mut bytes = vec![0_u8; required as usize];
        let descriptor = PSECURITY_DESCRIPTOR(bytes.as_mut_ptr().cast());
        let loaded = unsafe {
            GetFileSecurityW(
                PCWSTR(name.as_ptr()),
                DACL_SECURITY_INFORMATION.0,
                Some(descriptor),
                required,
                &mut required,
            )
        };
        if !loaded.as_bool() {
            return Err("node-control directory ACL is unavailable".into());
        }
        Ok(bytes)
    }

    fn dacl_is_protected(descriptor: PSECURITY_DESCRIPTOR) -> Result<bool, String> {
        let mut control = 0_u16;
        let mut revision = 0_u32;
        unsafe { GetSecurityDescriptorControl(descriptor, &mut control, &mut revision) }
            .map_err(|_| "node-control directory ACL control is invalid".to_string())?;
        Ok(control & SE_DACL_PROTECTED.0 != 0)
    }

    pub fn seal(bytes: &[u8]) -> Result<Vec<u8>, String> {
        let input = CRYPT_INTEGER_BLOB {
            cbData: bytes.len() as u32,
            pbData: bytes.as_ptr().cast_mut(),
        };
        let mut output = ProtectedBlob(CRYPT_INTEGER_BLOB::default());
        unsafe {
            CryptProtectData(
                &input,
                PCWSTR::null(),
                None,
                None,
                None,
                CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN,
                &mut output.0,
            )
        }
        .map_err(|_| "cannot encrypt node-control configuration".to_string())?;
        if output.0.pbData.is_null() || output.0.cbData == 0 {
            return Err("node-control encryption returned no data".into());
        }
        Ok(
            unsafe { std::slice::from_raw_parts(output.0.pbData, output.0.cbData as usize) }
                .to_vec(),
        )
    }

    pub fn unseal(bytes: &[u8]) -> Result<Vec<u8>, String> {
        let input = CRYPT_INTEGER_BLOB {
            cbData: bytes.len() as u32,
            pbData: bytes.as_ptr().cast_mut(),
        };
        let mut output = ProtectedBlob(CRYPT_INTEGER_BLOB::default());
        unsafe {
            CryptUnprotectData(
                &input,
                None,
                None,
                None,
                None,
                CRYPTPROTECT_UI_FORBIDDEN,
                &mut output.0,
            )
        }
        .map_err(|_| "cannot decrypt node-control configuration".to_string())?;
        if output.0.pbData.is_null() || output.0.cbData == 0 {
            return Err("node-control decryption returned no data".into());
        }
        Ok(
            unsafe { std::slice::from_raw_parts(output.0.pbData, output.0.cbData as usize) }
                .to_vec(),
        )
    }

    pub fn ensure_private_directory(path: &Path) -> Result<(), String> {
        if !path.is_absolute() {
            return Err("node-control directory must be absolute".into());
        }
        let sddl: Vec<u16> = "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"
            .encode_utf16()
            .chain(Some(0))
            .collect();
        let mut descriptor = Descriptor(PSECURITY_DESCRIPTOR::default());
        unsafe {
            ConvertStringSecurityDescriptorToSecurityDescriptorW(
                PCWSTR(sddl.as_ptr()),
                1,
                &mut descriptor.0,
                None,
            )
        }
        .map_err(|_| "node-control security descriptor creation failed".to_string())?;
        let name: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        let security = SECURITY_ATTRIBUTES {
            nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: descriptor.0 .0,
            bInheritHandle: false.into(),
        };
        if !path.exists() {
            unsafe { CreateDirectoryW(PCWSTR(name.as_ptr()), Some(&security)) }
                .map_err(|_| "cannot create protected node-control directory".to_string())?;
        }
        use std::os::windows::fs::MetadataExt;
        let metadata = std::fs::symlink_metadata(path)
            .map_err(|_| "protected node-control directory is unavailable".to_string())?;
        if !metadata.is_dir() || metadata.file_attributes() & 0x400 != 0 {
            return Err("node-control directory reparse point refused".into());
        }
        let mut actual = file_security_descriptor(&name)?;
        let actual = PSECURITY_DESCRIPTOR(actual.as_mut_ptr().cast());
        if !dacl_is_protected(actual)? || canonical_dacl(actual)? != canonical_dacl(descriptor.0)? {
            return Err("node-control directory ACL changed; access refused".into());
        }
        Ok(())
    }

    pub fn replace_file(source: &Path, target: &Path) -> Result<(), String> {
        let source: Vec<u16> = source.as_os_str().encode_wide().chain(Some(0)).collect();
        let target: Vec<u16> = target.as_os_str().encode_wide().chain(Some(0)).collect();
        unsafe {
            MoveFileExW(
                PCWSTR(source.as_ptr()),
                PCWSTR(target.as_ptr()),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
            )
        }
        .map_err(|_| "cannot publish protected node-control configuration".to_string())
    }
}

#[cfg(not(windows))]
mod platform {
    use super::*;

    pub fn seal(_: &[u8]) -> Result<Vec<u8>, String> {
        Err("node-control storage requires Windows DPAPI".into())
    }

    pub fn unseal(_: &[u8]) -> Result<Vec<u8>, String> {
        Err("node-control storage requires Windows DPAPI".into())
    }

    pub fn ensure_private_directory(_: &Path) -> Result<(), String> {
        Err("node-control storage requires Windows DPAPI".into())
    }

    pub fn replace_file(_: &Path, _: &Path) -> Result<(), String> {
        Err("node-control storage requires Windows DPAPI".into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn configuration() -> NodeControlConfiguration {
        NodeControlConfiguration {
            endpoint: "wss://console.example.com/api/console/node-control".into(),
            node_token: Zeroizing::new("a".repeat(64)),
            public_host: "render.example.com".into(),
            deployment_id: Uuid::parse_str("9c08feb1-af71-4fab-a6b8-bbd99b3552ba").unwrap(),
            deployment_kind: DeploymentKind::Private,
            deployment_trust_store: DeploymentTrustStore::new(3, [[1_u8; 32]]).unwrap(),
            minimum_certificate_version: 2,
            minimum_descriptor_revision: 4,
            minimum_trust_epoch: 3,
        }
    }

    #[test]
    fn validation_accepts_exact_secure_and_loopback_development_endpoints() {
        configuration().validate().unwrap();
        let mut loopback = configuration();
        loopback.endpoint = "ws://127.0.0.1:8080/api/console/node-control".into();
        loopback.validate().unwrap();
    }

    #[test]
    fn validation_rejects_fallback_routes_queries_credentials_and_bad_tokens() {
        for endpoint in [
            "wss://console.example.com/console/service",
            "wss://console.example.com/cms/service",
            "wss://console.example.com/api/console/node-control?token=secret",
            "wss://user@console.example.com/api/console/node-control",
            "ws://console.example.com/api/console/node-control",
        ] {
            let mut value = configuration();
            value.endpoint = endpoint.into();
            assert!(value.validate().is_err(), "accepted {endpoint}");
        }
        let mut value = configuration();
        value.node_token = Zeroizing::new("A".repeat(64));
        assert!(value.validate().is_err());
    }

    #[test]
    fn validation_rejects_url_or_unusable_public_host() {
        for host in [
            "https://render.example.com",
            "render.example.com:4601",
            "0.0.0.0",
            "239.1.1.1",
            " user.example.com",
        ] {
            let mut value = configuration();
            value.public_host = host.into();
            assert!(value.validate().is_err(), "accepted {host}");
        }
    }

    #[test]
    fn configuration_rejects_retired_schema_and_inconsistent_trust_watermarks() {
        let configured = configuration();
        let retired = StoredConfiguration {
            schema_version: 1,
            endpoint: configured.endpoint.clone(),
            node_token: configured.node_token.to_string(),
            public_host: configured.public_host.clone(),
            deployment_id: configured.deployment_id,
            deployment_kind: configured.deployment_kind,
            deployment_trust_store: configured.deployment_trust_store.clone(),
            minimum_certificate_version: configured.minimum_certificate_version,
            minimum_descriptor_revision: configured.minimum_descriptor_revision,
            minimum_trust_epoch: configured.minimum_trust_epoch,
        };
        assert!(retired.into_runtime().is_err());

        let mut inconsistent = configured;
        inconsistent.minimum_trust_epoch += 1;
        assert!(inconsistent.validate().is_err());
    }

    #[test]
    fn deployment_watermark_rejects_identity_change_and_rollback() {
        let configured = configuration();
        let current = DeploymentIdentityWatermark::new(
            configured.deployment_id,
            configured.deployment_kind,
            2,
            4,
            3,
        )
        .unwrap();
        let advanced = DeploymentIdentityWatermark::new(
            configured.deployment_id,
            configured.deployment_kind,
            3,
            5,
            4,
        )
        .unwrap();
        assert!(current.allows(&advanced));

        let rolled_back = DeploymentIdentityWatermark::new(
            configured.deployment_id,
            configured.deployment_kind,
            2,
            3,
            3,
        )
        .unwrap();
        assert!(!current.allows(&rolled_back));

        let other_deployment =
            DeploymentIdentityWatermark::new(Uuid::new_v4(), configured.deployment_kind, 3, 5, 4)
                .unwrap();
        assert!(!current.allows(&other_deployment));
    }

    #[cfg(windows)]
    #[test]
    fn protected_configuration_round_trip_does_not_store_plain_token() {
        let nonce = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("pixels_node_control_{nonce}"));
        std::fs::create_dir(&directory).unwrap();
        let store = NodeControlStore::new(directory.clone());
        let value = configuration();
        store.save(&value).unwrap();
        assert_eq!(store.load().unwrap(), Some(value.clone()));
        let encrypted = std::fs::read(store.file_path()).unwrap();
        assert!(!encrypted
            .windows(64)
            .any(|bytes| bytes
                == b"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        let watermark =
            DeploymentIdentityWatermark::new(value.deployment_id, value.deployment_kind, 2, 4, 3)
                .unwrap();
        store.save_identity_watermark(&watermark).unwrap();
        assert_eq!(store.load_identity_watermark().unwrap(), Some(watermark));
        assert!(
            !String::from_utf8_lossy(&std::fs::read(store.identity_watermark_path()).unwrap())
                .contains("9c08feb1-af71-4fab-a6b8-bbd99b3552ba")
        );
        store.clear().unwrap();
        std::fs::remove_dir(directory.join(CONFIG_DIRECTORY)).unwrap();
        std::fs::remove_dir(directory).unwrap();
    }

    #[cfg(windows)]
    #[test]
    fn protected_telemetry_backlog_is_ordered_bounded_and_acknowledged_exactly() {
        let nonce = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("pixels_telemetry_backlog_{nonce}"));
        std::fs::create_dir(&directory).unwrap();
        let store = TelemetryBacklogStore::new(directory.clone());
        let mut first = crate::node_telemetry::unavailable();
        first.sampled_at = chrono::Utc::now() - chrono::TimeDelta::minutes(2);
        let mut second = crate::node_telemetry::unavailable();
        second.sampled_at = chrono::Utc::now() - chrono::TimeDelta::minutes(1);
        store.append(first).unwrap();
        store.append(second).unwrap();

        let pending = store.pending(4).unwrap();
        assert_eq!(pending.len(), 2);
        let encrypted = std::fs::read(store.file_path()).unwrap();
        assert!(!String::from_utf8_lossy(&encrypted).contains("sampled_at"));
        store.acknowledge(&[pending[0].sample_id]).unwrap();
        let remaining = store.pending(4).unwrap();
        assert_eq!(remaining.len(), 1);
        assert_eq!(remaining[0].sample_id, pending[1].sample_id);
        store.acknowledge(&[remaining[0].sample_id]).unwrap();
        assert!(store.pending(4).unwrap().is_empty());

        std::fs::remove_file(store.file_path()).unwrap();
        std::fs::remove_dir(directory.join(CONFIG_DIRECTORY)).unwrap();
        std::fs::remove_dir(directory).unwrap();
    }

    #[cfg(windows)]
    #[test]
    fn protected_file_transfer_outbox_survives_restart_and_enforces_order() {
        let nonce = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("pixels_file_transfer_outbox_{nonce}"));
        std::fs::create_dir(&directory).unwrap();
        let transfer_id = Uuid::new_v4();
        let store = FileTransferOutboxStore::new(directory.clone());
        let progress = px_node_protocol::TransferProgress {
            sequence: 1,
            transferred_bytes: 512,
            outcome: px_node_protocol::TransferOutcome::Progress,
        };
        store.append(transfer_id, &progress).unwrap();
        store.append(transfer_id, &progress).unwrap();

        let reopened = FileTransferOutboxStore::new(directory.clone());
        let pending = reopened.pending(32).unwrap();
        assert_eq!(pending.len(), 1);
        assert_eq!(pending[0].transfer_id, transfer_id);
        assert_eq!(pending[0].sequence, 1);
        assert!(
            !String::from_utf8_lossy(&std::fs::read(reopened.file_path()).unwrap())
                .contains(&transfer_id.to_string())
        );

        reopened
            .append(
                transfer_id,
                &px_node_protocol::TransferProgress {
                    sequence: 2,
                    transferred_bytes: 1024,
                    outcome: px_node_protocol::TransferOutcome::Completed {
                        received_sha256: [7_u8; 32],
                    },
                },
            )
            .unwrap();
        assert!(reopened
            .append(
                transfer_id,
                &px_node_protocol::TransferProgress {
                    sequence: 3,
                    transferred_bytes: 1024,
                    outcome: px_node_protocol::TransferOutcome::Progress,
                },
            )
            .is_err());
        reopened.acknowledge(transfer_id, 1).unwrap();
        assert_eq!(reopened.pending(32).unwrap().len(), 1);
        assert_eq!(reopened.reject_transfer(transfer_id).unwrap(), 1);
        assert!(reopened.pending(32).unwrap().is_empty());

        std::fs::remove_file(reopened.file_path()).unwrap();
        std::fs::remove_dir(directory.join(CONFIG_DIRECTORY)).unwrap();
        std::fs::remove_dir(directory).unwrap();
    }
}
