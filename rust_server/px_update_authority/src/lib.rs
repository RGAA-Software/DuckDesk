use async_trait::async_trait;
use aws_lc_rs::rand::SystemRandom;
use jiff::Timestamp;
use px_release_catalog::ReleaseSpec;
use serde::Serialize;
use sha2::{Digest, Sha256};
use std::collections::{HashMap, HashSet};
use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::num::NonZeroU64;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tough::editor::signed::SignedRole;
use tough::editor::RepositoryEditor;
use tough::key_source::KeySource;
use tough::schema::{KeyHolder, RoleKeys, RoleType, Root, Signed, Target};
use tough::{ExpirationEnforcement, Prefix, RepositoryLoader, TargetName};
use url::Url;
use zeroize::Zeroizing;

mod promotion;
pub use promotion::{
    prepare_console_registration, promote_repository, ConsoleRegistrationPreparation,
    RepositoryPromotion,
};

pub type AuthorityResult<T> = Result<T, Box<dyn std::error::Error + Send + Sync>>;

pub(crate) const MAXIMUM_ROOT_BYTES: u64 = 1024 * 1024;
const MAXIMUM_RELEASE_SPEC_BYTES: u64 = 1024 * 1024;
pub(crate) const MAXIMUM_TARGET_FILES: usize = 10_000;
pub(crate) const MAXIMUM_TARGET_BYTES: u64 = 1_u64 << 40;
const MAXIMUM_ROOT_CHAIN_LENGTH: usize = 64;

#[derive(Debug, Clone)]
pub struct RootCreation {
    pub root_signing_key_paths: Vec<PathBuf>,
    pub root_signature_threshold: u64,
    pub targets_signing_key_path: PathBuf,
    pub snapshot_signing_key_path: PathBuf,
    pub timestamp_signing_key_path: PathBuf,
    pub version: u64,
    pub expires_at: Timestamp,
    pub output_path: PathBuf,
}

#[derive(Debug, Clone)]
pub struct RootRotation {
    pub current_root_path: PathBuf,
    pub current_root_signing_key_paths: Vec<PathBuf>,
    pub new_root_signing_key_paths: Vec<PathBuf>,
    pub new_root_signature_threshold: u64,
    pub new_targets_signing_key_path: PathBuf,
    pub new_snapshot_signing_key_path: PathBuf,
    pub new_timestamp_signing_key_path: PathBuf,
    pub expires_at: Timestamp,
    pub output_path: PathBuf,
}

#[derive(Debug, Clone)]
pub struct RepositoryPublication {
    pub root_path: PathBuf,
    pub targets_signing_key_path: PathBuf,
    pub snapshot_signing_key_path: PathBuf,
    pub timestamp_signing_key_path: PathBuf,
    pub release_spec_path: PathBuf,
    pub artifact_path: PathBuf,
    pub previous_repository_path: Option<PathBuf>,
    pub output_path: PathBuf,
    pub targets_expires_at: Timestamp,
    pub snapshot_expires_at: Timestamp,
    pub timestamp_expires_at: Timestamp,
}

struct PrivateKeySource {
    key_material: Arc<Zeroizing<Vec<u8>>>,
}

pub(crate) struct RootChainEntry {
    pub(crate) version: u64,
    pub(crate) bytes: Vec<u8>,
    pub(crate) signed_root: Signed<Root>,
}

impl std::fmt::Debug for PrivateKeySource {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.write_str("PrivateKeySource(<redacted>)")
    }
}

#[async_trait]
impl KeySource for PrivateKeySource {
    async fn as_sign(
        &self,
    ) -> Result<Box<dyn tough::sign::Sign>, Box<dyn std::error::Error + Send + Sync + 'static>>
    {
        Ok(Box::new(tough::sign::parse_keypair(
            self.key_material.as_slice(),
        )?))
    }

    async fn write(
        &self,
        _key_document: &str,
        _key_id_hex: &str,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync + 'static>> {
        Err("in-memory private signing keys are read-only".into())
    }
}

#[derive(Debug, Serialize)]
struct PublicationManifest<'a> {
    schema_version: u32,
    release_digest_sha256: String,
    root_sha256: String,
    targets_version: u64,
    snapshot_version: u64,
    timestamp_version: u64,
    created_at: String,
    release: &'a ReleaseSpec,
}

pub fn generate_signing_key(output_path: &Path) -> AuthorityResult<()> {
    let key_document = Zeroizing::new(
        ring::signature::Ed25519KeyPair::generate_pkcs8(&ring::rand::SystemRandom::new())
            .map_err(|_| "TUF signing key generation failed")?
            .as_ref()
            .to_vec(),
    );
    px_private_files::private::create_private(output_path, &key_document)
        .map_err(|error| error.into())
}

pub async fn create_initial_root(configuration: &RootCreation) -> AuthorityResult<()> {
    validate_root_creation(configuration)?;
    let root_sources = load_private_key_sources(&configuration.root_signing_key_paths)?;
    let targets_source = load_private_key_source(&configuration.targets_signing_key_path)?;
    let snapshot_source = load_private_key_source(&configuration.snapshot_signing_key_path)?;
    let timestamp_source = load_private_key_source(&configuration.timestamp_signing_key_path)?;
    let root = build_root(
        &root_sources,
        configuration.root_signature_threshold,
        targets_source.as_ref(),
        snapshot_source.as_ref(),
        timestamp_source.as_ref(),
        NonZeroU64::new(configuration.version).ok_or("TUF root version must be positive")?,
        configuration.expires_at,
    )
    .await?;
    let unsigned_root = Signed {
        signed: root.clone(),
        signatures: Vec::new(),
    };
    let signed_root = SignedRole::new(
        root,
        &KeyHolder::Root(unsigned_root.signed),
        &root_sources,
        &SystemRandom::new(),
    )
    .await?;
    let parsed_root: Signed<Root> = serde_json::from_slice(signed_root.buffer())?;
    parsed_root.signed.verify_role(&parsed_root)?;
    write_new_public_file(&configuration.output_path, signed_root.buffer())?;
    Ok(())
}

pub async fn rotate_root(configuration: &RootRotation) -> AuthorityResult<()> {
    validate_root_rotation(configuration)?;
    let current_root_bytes = read_bounded(&configuration.current_root_path, MAXIMUM_ROOT_BYTES)?;
    let current_root: Signed<Root> = serde_json::from_slice(&current_root_bytes)?;
    current_root.signed.verify_role(&current_root)?;
    if current_root.signed.consistent_snapshot {
        return Err("Pixels update repositories require non-prefixed target paths".into());
    }
    let current_root_threshold = current_root
        .signed
        .roles
        .get(&RoleType::Root)
        .ok_or("current TUF root role is missing")?
        .threshold
        .get();
    if u64::try_from(configuration.current_root_signing_key_paths.len())? < current_root_threshold {
        return Err("insufficient current root signing keys for rotation".into());
    }
    if current_root.signed.expires <= Timestamp::now() {
        return Err("current TUF root metadata is expired".into());
    }
    if configuration.expires_at <= current_root.signed.expires {
        return Err("rotated TUF root expiration must advance".into());
    }
    let next_root_version = next_version(current_root.signed.version)?;
    let expected_output_name = format!("{}.root.json", next_root_version.get());
    if configuration
        .output_path
        .file_name()
        .and_then(|name| name.to_str())
        != Some(expected_output_name.as_str())
    {
        return Err(format!("rotated TUF root output must be named {expected_output_name}").into());
    }
    let current_root_sources =
        load_private_key_sources(&configuration.current_root_signing_key_paths)?;
    let new_root_sources = load_private_key_sources(&configuration.new_root_signing_key_paths)?;
    let new_targets_source = load_private_key_source(&configuration.new_targets_signing_key_path)?;
    let new_snapshot_source =
        load_private_key_source(&configuration.new_snapshot_signing_key_path)?;
    let new_timestamp_source =
        load_private_key_source(&configuration.new_timestamp_signing_key_path)?;
    let new_root = build_root(
        &new_root_sources,
        configuration.new_root_signature_threshold,
        new_targets_source.as_ref(),
        new_snapshot_source.as_ref(),
        new_timestamp_source.as_ref(),
        next_root_version,
        configuration.expires_at,
    )
    .await?;
    let current_authorized_root = SignedRole::new(
        new_root.clone(),
        &KeyHolder::Root(current_root.signed.clone()),
        &current_root_sources,
        &SystemRandom::new(),
    )
    .await?;
    let rotated_root = SignedRole::new(
        new_root.clone(),
        &KeyHolder::Root(new_root),
        &new_root_sources,
        &SystemRandom::new(),
    )
    .await?
    .add_old_signatures(current_authorized_root.signed().signatures.clone())?;
    let parsed_rotated_root: Signed<Root> = serde_json::from_slice(rotated_root.buffer())?;
    current_root.signed.verify_role(&parsed_rotated_root)?;
    parsed_rotated_root
        .signed
        .verify_role(&parsed_rotated_root)?;
    write_new_public_file(&configuration.output_path, rotated_root.buffer())?;
    Ok(())
}

async fn build_root(
    root_sources: &[Box<dyn KeySource>],
    root_signature_threshold: u64,
    targets_source: &dyn KeySource,
    snapshot_source: &dyn KeySource,
    timestamp_source: &dyn KeySource,
    version: NonZeroU64,
    expires_at: Timestamp,
) -> AuthorityResult<Root> {
    let mut keys = HashMap::new();
    let mut root_key_ids = Vec::new();
    for root_source in root_sources {
        let signing_key = root_source.as_sign().await?;
        let public_key = signing_key.tuf_key();
        let key_id = public_key.key_id()?;
        keys.insert(key_id.clone(), public_key);
        root_key_ids.push(key_id);
    }
    let targets_key_id = insert_role_key(&mut keys, targets_source).await?;
    let snapshot_key_id = insert_role_key(&mut keys, snapshot_source).await?;
    let timestamp_key_id = insert_role_key(&mut keys, timestamp_source).await?;
    if keys.len() != root_sources.len() + 3 {
        return Err("TUF root, targets, snapshot, and timestamp keys must all be distinct".into());
    }
    let mut roles = HashMap::new();
    roles.insert(
        RoleType::Root,
        RoleKeys {
            keyids: root_key_ids,
            threshold: NonZeroU64::new(root_signature_threshold)
                .ok_or("TUF root threshold must be positive")?,
            _extra: HashMap::new(),
        },
    );
    for (role, key_id) in [
        (RoleType::Targets, targets_key_id),
        (RoleType::Snapshot, snapshot_key_id),
        (RoleType::Timestamp, timestamp_key_id),
    ] {
        roles.insert(
            role,
            RoleKeys {
                keyids: vec![key_id],
                threshold: NonZeroU64::new(1).expect("one is nonzero"),
                _extra: HashMap::new(),
            },
        );
    }
    Ok(Root {
        spec_version: "1.0.0".into(),
        consistent_snapshot: false,
        version,
        expires: expires_at,
        keys,
        roles,
        _extra: HashMap::new(),
    })
}

pub async fn publish_repository(configuration: &RepositoryPublication) -> AuthorityResult<()> {
    validate_publication_configuration(configuration)?;
    let root_bytes = read_bounded(&configuration.root_path, MAXIMUM_ROOT_BYTES)?;
    let trusted_root: Signed<Root> = serde_json::from_slice(&root_bytes)?;
    trusted_root.signed.verify_role(&trusted_root)?;
    if trusted_root.signed.consistent_snapshot {
        return Err("Pixels update repositories require non-prefixed target paths".into());
    }
    if trusted_root.signed.expires <= Timestamp::now() {
        return Err("TUF root metadata is expired".into());
    }
    if configuration.targets_expires_at > trusted_root.signed.expires
        || configuration.snapshot_expires_at > configuration.targets_expires_at
        || configuration.timestamp_expires_at > configuration.snapshot_expires_at
    {
        return Err("TUF expiration order must be timestamp <= snapshot <= targets <= root".into());
    }

    let release_bytes = read_bounded(&configuration.release_spec_path, MAXIMUM_RELEASE_SPEC_BYTES)?;
    let release: ReleaseSpec = serde_json::from_slice(&release_bytes)?;
    release
        .validate()
        .map_err(|_| "release specification is invalid")?;
    verify_artifact(&configuration.artifact_path, &release)?;

    let targets_source = load_private_key_source(&configuration.targets_signing_key_path)?;
    let snapshot_source = load_private_key_source(&configuration.snapshot_signing_key_path)?;
    let timestamp_source = load_private_key_source(&configuration.timestamp_signing_key_path)?;
    ensure_role_sources_are_distinct([
        targets_source.as_ref(),
        snapshot_source.as_ref(),
        timestamp_source.as_ref(),
    ])
    .await?;
    let signing_sources = vec![targets_source, snapshot_source, timestamp_source];

    let target_name = TargetName::new(release.target_name.clone())?;
    let staging_path = publication_staging_path(&configuration.output_path)?;
    if staging_path.exists() {
        return Err("unexpected TUF publication staging directory already exists".into());
    }
    std::fs::create_dir(&staging_path)?;
    let publication_result = publish_into_staging(
        configuration,
        &release,
        &root_bytes,
        &signing_sources,
        &target_name,
        &staging_path,
    )
    .await;
    if let Err(error) = publication_result {
        let _ = std::fs::remove_dir_all(&staging_path);
        return Err(error);
    }
    std::fs::rename(&staging_path, &configuration.output_path).map_err(|error| {
        format!(
            "cannot commit TUF publication {} to {}: {error}",
            staging_path.display(),
            configuration.output_path.display()
        )
    })?;
    Ok(())
}

async fn publish_into_staging(
    configuration: &RepositoryPublication,
    release: &ReleaseSpec,
    root_bytes: &[u8],
    signing_sources: &[Box<dyn KeySource>],
    target_name: &TargetName,
    staging_path: &Path,
) -> AuthorityResult<()> {
    let metadata_path = staging_path.join("metadata");
    let targets_path = staging_path.join("targets");
    std::fs::create_dir(&metadata_path)?;
    std::fs::create_dir(&targets_path)?;
    let (mut editor, targets_version, snapshot_version, timestamp_version) =
        load_repository_editor(configuration, root_bytes, &metadata_path, &targets_path)
            .await
            .map_err(|error| format!("cannot prepare prior TUF repository state: {error}"))?;
    editor.targets_version(targets_version)?;
    editor.targets_expires(configuration.targets_expires_at)?;
    editor
        .snapshot_version(snapshot_version)
        .snapshot_expires(configuration.snapshot_expires_at)
        .timestamp_version(timestamp_version)
        .timestamp_expires(configuration.timestamp_expires_at);
    let target_path = targets_path.join(target_name.resolved());
    if target_path.exists() {
        return Err("TUF target names are immutable and must not be reused".into());
    }
    if let Some(target_parent) = target_path.parent() {
        std::fs::create_dir_all(target_parent)
            .map_err(|error| format!("cannot create TUF target parent: {error}"))?;
    }
    copy_new_file(&configuration.artifact_path, &target_path)
        .map_err(|error| format!("cannot copy new TUF target: {error}"))?;
    verify_artifact(&target_path, release)
        .map_err(|error| format!("copied TUF target verification failed: {error}"))?;

    let mut target = Target::from_path(&target_path)
        .await
        .map_err(|error| format!("cannot describe new TUF target: {error}"))?;
    target.custom.insert(
        "pixels".into(),
        serde_json::json!({
            "schema_version": 1,
            "target": release.target,
            "build_number": release.build_number,
            "version": release.version,
            "platform_signer_sha256": release.platform_signer_sha256,
        }),
    );
    editor
        .add_target(target_name.clone(), target)
        .map_err(|error| format!("cannot add new TUF target: {error}"))?;
    let signed_repository = editor
        .sign(signing_sources)
        .await
        .map_err(|error| format!("cannot sign TUF repository: {error}"))?;
    signed_repository
        .write(&metadata_path)
        .await
        .map_err(|error| format!("cannot write TUF repository metadata: {error}"))?;
    sync_regular_files(&metadata_path)?;
    sync_regular_files(&targets_path)?;

    let release_digest = release.digest().map_err(|_| "invalid release digest")?;
    let manifest = PublicationManifest {
        schema_version: 1,
        release_digest_sha256: hex::encode(release_digest),
        root_sha256: hex::encode(Sha256::digest(root_bytes)),
        targets_version: targets_version.get(),
        snapshot_version: snapshot_version.get(),
        timestamp_version: timestamp_version.get(),
        created_at: Timestamp::now().to_string(),
        release,
    };
    write_new_public_file(
        &staging_path.join("publication.json"),
        &(serde_json::to_vec_pretty(&manifest)?),
    )?;
    let bootstrap_root_bytes = oldest_repository_root(&metadata_path)?;
    verify_staged_repository(
        &bootstrap_root_bytes,
        &metadata_path,
        &targets_path,
        target_name,
    )
    .await
    .map_err(|error| format!("staged TUF repository self-verification failed: {error}").into())
}

async fn verify_staged_repository(
    root_bytes: &[u8],
    metadata_path: &Path,
    targets_path: &Path,
    target_name: &TargetName,
) -> AuthorityResult<()> {
    let repository = RepositoryLoader::new(
        &root_bytes,
        directory_url(metadata_path)?,
        directory_url(targets_path)?,
    )
    .expiration_enforcement(ExpirationEnforcement::Safe)
    .load()
    .await?;
    let verification_directory = tempfile_directory_next_to(metadata_path)?;
    std::fs::create_dir(&verification_directory)?;
    let verification_result = repository
        .save_target(target_name, &verification_directory, Prefix::None)
        .await;
    std::fs::remove_dir_all(&verification_directory)?;
    verification_result?;
    Ok(())
}

async fn load_repository_editor(
    configuration: &RepositoryPublication,
    root_bytes: &[u8],
    metadata_output: &Path,
    verified_targets_output: &Path,
) -> AuthorityResult<(RepositoryEditor, NonZeroU64, NonZeroU64, NonZeroU64)> {
    let Some(previous_repository_path) = &configuration.previous_repository_path else {
        return Ok((
            RepositoryEditor::new(&configuration.root_path).await?,
            NonZeroU64::new(1).expect("one is nonzero"),
            NonZeroU64::new(1).expect("one is nonzero"),
            NonZeroU64::new(1).expect("one is nonzero"),
        ));
    };
    reject_symbolic_link(&previous_repository_path.join("metadata"))?;
    reject_symbolic_link(&previous_repository_path.join("targets"))?;
    let root_chain = load_root_chain(&previous_repository_path.join("metadata"))?;
    let previous_root_entry = root_chain
        .last()
        .ok_or("previous repository does not contain TUF root metadata")?;
    validate_next_publication_root(
        &previous_root_entry.bytes,
        &previous_root_entry.signed_root,
        root_bytes,
    )?;
    let repository = RepositoryLoader::new(
        &previous_root_entry.bytes,
        directory_url(&previous_repository_path.join("metadata"))?,
        directory_url(&previous_repository_path.join("targets"))?,
    )
    .expiration_enforcement(ExpirationEnforcement::Safe)
    .load()
    .await
    .map_err(|error| format!("previous TUF repository verification failed: {error}"))?;
    let targets_version = next_version(repository.targets().signed.version)?;
    let snapshot_version = next_version(repository.snapshot().signed.version)?;
    let timestamp_version = next_version(repository.timestamp().signed.version)?;
    let previous_target_names: Vec<TargetName> = repository
        .all_targets()
        .map(|(target_name, _)| target_name.clone())
        .collect();
    if previous_target_names.len() > MAXIMUM_TARGET_FILES {
        return Err("previous TUF repository exceeds the target count limit".into());
    }
    let total_target_bytes = repository
        .all_targets()
        .try_fold(0_u64, |total_bytes, (_, target)| {
            total_bytes.checked_add(target.length)
        })
        .ok_or("previous TUF repository target size overflow")?;
    if total_target_bytes > MAXIMUM_TARGET_BYTES {
        return Err("previous TUF repository exceeds the target size limit".into());
    }
    for target_name in &previous_target_names {
        repository
            .save_target(target_name, verified_targets_output, Prefix::None)
            .await
            .map_err(|error| {
                format!(
                    "previous TUF target verification failed for {}: {error}",
                    target_name.raw()
                )
            })?;
    }
    for root_entry in &root_chain {
        copy_new_file(
            &previous_repository_path
                .join("metadata")
                .join(format!("{}.root.json", root_entry.version)),
            &metadata_output.join(format!("{}.root.json", root_entry.version)),
        )?;
        let copied_root_bytes = read_bounded(
            &metadata_output.join(format!("{}.root.json", root_entry.version)),
            MAXIMUM_ROOT_BYTES,
        )?;
        if copied_root_bytes != root_entry.bytes {
            return Err("copied TUF root metadata changed during publication".into());
        }
    }
    Ok((
        RepositoryEditor::from_repo(&configuration.root_path, repository)
            .await
            .map_err(|error| format!("previous TUF repository import failed: {error}"))?,
        targets_version,
        snapshot_version,
        timestamp_version,
    ))
}

pub(crate) fn load_root_chain(metadata_path: &Path) -> AuthorityResult<Vec<RootChainEntry>> {
    let mut root_paths = Vec::new();
    for directory_entry in std::fs::read_dir(metadata_path)? {
        let directory_entry = directory_entry?;
        let file_name = directory_entry
            .file_name()
            .into_string()
            .map_err(|_| "TUF metadata file name is not valid UTF-8")?;
        let Some(version_text) = file_name.strip_suffix(".root.json") else {
            continue;
        };
        let root_version: u64 = version_text
            .parse()
            .map_err(|_| "TUF root metadata file has an invalid version")?;
        if root_version == 0 {
            return Err("TUF root metadata version must be positive".into());
        }
        root_paths.push((root_version, directory_entry.path()));
    }
    root_paths.sort_by_key(|(root_version, _)| *root_version);
    if root_paths.is_empty() || root_paths.len() > MAXIMUM_ROOT_CHAIN_LENGTH {
        return Err("TUF root chain has an invalid length".into());
    }

    let mut root_chain: Vec<RootChainEntry> = Vec::with_capacity(root_paths.len());
    for (root_version, root_path) in root_paths {
        let root_bytes = read_bounded(&root_path, MAXIMUM_ROOT_BYTES)?;
        let signed_root: Signed<Root> = serde_json::from_slice(&root_bytes)?;
        if signed_root.signed.version.get() != root_version {
            return Err("TUF root metadata file name does not match its version".into());
        }
        signed_root.signed.verify_role(&signed_root)?;
        if let Some(previous_root_entry) = root_chain.last() {
            if previous_root_entry.version.checked_add(1) != Some(root_version) {
                return Err("TUF root metadata chain is not contiguous".into());
            }
            previous_root_entry
                .signed_root
                .signed
                .verify_role(&signed_root)?;
        }
        root_chain.push(RootChainEntry {
            version: root_version,
            bytes: root_bytes,
            signed_root,
        });
    }
    Ok(root_chain)
}

fn validate_next_publication_root(
    previous_root_bytes: &[u8],
    previous_root: &Signed<Root>,
    requested_root_bytes: &[u8],
) -> AuthorityResult<()> {
    if requested_root_bytes == previous_root_bytes {
        return Ok(());
    }
    let requested_root: Signed<Root> = serde_json::from_slice(requested_root_bytes)?;
    if previous_root.signed.version.get().checked_add(1)
        != Some(requested_root.signed.version.get())
    {
        return Err(
            "new publication root must equal the current root or advance exactly one version"
                .into(),
        );
    }
    previous_root.signed.verify_role(&requested_root)?;
    requested_root.signed.verify_role(&requested_root)?;
    Ok(())
}

fn oldest_repository_root(metadata_path: &Path) -> AuthorityResult<Vec<u8>> {
    load_root_chain(metadata_path)?
        .into_iter()
        .next()
        .map(|root_entry| root_entry.bytes)
        .ok_or_else(|| "TUF repository does not contain root metadata".into())
}

fn validate_root_creation(configuration: &RootCreation) -> AuthorityResult<()> {
    if configuration.output_path.exists() {
        return Err("TUF root output already exists and will not be overwritten".into());
    }
    if configuration.root_signing_key_paths.len() < 2
        || configuration.root_signing_key_paths.len() > 5
        || configuration.root_signature_threshold < 2
        || configuration.root_signature_threshold
            > u64::try_from(configuration.root_signing_key_paths.len())?
        || configuration.version == 0
        || configuration.expires_at <= Timestamp::now()
    {
        return Err("TUF root requires 2-5 keys, threshold >= 2, a positive version, and a future expiration".into());
    }
    let output_parent = require_explicit_parent(&configuration.output_path)?;
    reject_symbolic_link(output_parent)?;
    if !output_parent.is_dir() {
        return Err("TUF root output parent does not exist".into());
    }
    Ok(())
}

fn validate_root_rotation(configuration: &RootRotation) -> AuthorityResult<()> {
    if configuration.output_path.exists() {
        return Err("rotated TUF root output already exists and will not be overwritten".into());
    }
    if configuration.current_root_signing_key_paths.is_empty()
        || configuration.current_root_signing_key_paths.len() > 5
        || configuration.new_root_signing_key_paths.len() < 2
        || configuration.new_root_signing_key_paths.len() > 5
        || configuration.new_root_signature_threshold < 2
        || configuration.new_root_signature_threshold
            > u64::try_from(configuration.new_root_signing_key_paths.len())?
        || configuration.expires_at <= Timestamp::now()
    {
        return Err("TUF root rotation requires current keys, 2-5 new keys, a new threshold >= 2, and a future expiration".into());
    }
    let output_parent = require_explicit_parent(&configuration.output_path)?;
    reject_symbolic_link(output_parent)?;
    if !output_parent.is_dir() {
        return Err("rotated TUF root output parent does not exist".into());
    }
    Ok(())
}

fn validate_publication_configuration(
    configuration: &RepositoryPublication,
) -> AuthorityResult<()> {
    if configuration.output_path.exists() {
        return Err("TUF publication output already exists and will not be overwritten".into());
    }
    let output_parent = require_explicit_parent(&configuration.output_path)?;
    reject_symbolic_link(output_parent)?;
    if !output_parent.is_dir()
        || configuration.timestamp_expires_at <= Timestamp::now()
        || configuration.snapshot_expires_at <= Timestamp::now()
        || configuration.targets_expires_at <= Timestamp::now()
    {
        return Err("TUF publication requires an existing parent and future expirations".into());
    }
    if let Some(previous_repository_path) = &configuration.previous_repository_path {
        if !previous_repository_path.is_dir()
            || previous_repository_path == &configuration.output_path
        {
            return Err("previous TUF repository path is invalid".into());
        }
    }
    Ok(())
}

fn require_explicit_parent(path: &Path) -> AuthorityResult<&Path> {
    if !path.is_absolute() || path.file_name().is_none() {
        return Err("output path must be an absolute child path".into());
    }
    path.parent()
        .filter(|parent| parent != &path)
        .ok_or_else(|| "output path requires an explicit parent".into())
}

fn load_private_key_sources(paths: &[PathBuf]) -> AuthorityResult<Vec<Box<dyn KeySource>>> {
    paths
        .iter()
        .map(|path| load_private_key_source(path))
        .collect()
}

fn load_private_key_source(path: &Path) -> AuthorityResult<Box<dyn KeySource>> {
    let key_material = px_private_files::private::read_private(path)
        .map_err(|error| -> Box<dyn std::error::Error + Send + Sync> { error.into() })?;
    tough::sign::parse_keypair(&key_material)?;
    Ok(Box::new(PrivateKeySource {
        key_material: Arc::new(key_material),
    }))
}

async fn insert_role_key(
    keys: &mut HashMap<
        tough::schema::decoded::Decoded<tough::schema::decoded::Hex>,
        tough::schema::key::Key,
    >,
    source: &dyn KeySource,
) -> AuthorityResult<tough::schema::decoded::Decoded<tough::schema::decoded::Hex>> {
    let public_key = source.as_sign().await?.tuf_key();
    let key_id = public_key.key_id()?;
    keys.insert(key_id.clone(), public_key);
    Ok(key_id)
}

async fn ensure_role_sources_are_distinct(sources: [&dyn KeySource; 3]) -> AuthorityResult<()> {
    let mut key_ids = HashSet::new();
    for source in sources {
        key_ids.insert(source.as_sign().await?.tuf_key().key_id()?);
    }
    if key_ids.len() != 3 {
        return Err("TUF targets, snapshot, and timestamp signing keys must be distinct".into());
    }
    Ok(())
}

fn verify_artifact(path: &Path, release: &ReleaseSpec) -> AuthorityResult<()> {
    reject_symbolic_link(path)?;
    let metadata = std::fs::metadata(path)?;
    if !metadata.is_file() || i64::try_from(metadata.len())? != release.size_bytes {
        return Err("release artifact size does not match the approved specification".into());
    }
    let actual_sha256 = sha256_file(path)?;
    if actual_sha256 != release.sha256 {
        return Err("release artifact SHA-256 does not match the approved specification".into());
    }
    Ok(())
}

pub(crate) fn sha256_file(path: &Path) -> AuthorityResult<String> {
    let mut artifact = File::open(path)?;
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 128 * 1024];
    loop {
        let bytes_read = artifact.read(&mut buffer)?;
        if bytes_read == 0 {
            break;
        }
        digest.update(&buffer[..bytes_read]);
    }
    Ok(hex::encode(digest.finalize()))
}

pub(crate) fn read_bounded(path: &Path, maximum_bytes: u64) -> AuthorityResult<Vec<u8>> {
    reject_symbolic_link(path)?;
    let file = File::open(path)?;
    let metadata = file.metadata()?;
    if !metadata.is_file() || metadata.len() == 0 || metadata.len() > maximum_bytes {
        return Err("bounded input file has an invalid size".into());
    }
    let mut bytes = Vec::new();
    file.take(maximum_bytes + 1).read_to_end(&mut bytes)?;
    if bytes.len() as u64 > maximum_bytes {
        return Err("bounded input file changed while being read".into());
    }
    Ok(bytes)
}

pub(crate) fn reject_symbolic_link(path: &Path) -> AuthorityResult<()> {
    for existing_path in path.ancestors().take_while(|ancestor| ancestor.exists()) {
        let metadata = std::fs::symlink_metadata(existing_path)?;
        #[cfg(windows)]
        let is_reparse_point = {
            use std::os::windows::fs::MetadataExt;
            metadata.file_attributes() & 0x400 != 0
        };
        #[cfg(not(windows))]
        let is_reparse_point = false;
        if metadata.file_type().is_symlink() || is_reparse_point {
            return Err("symbolic links are not permitted in TUF authority paths".into());
        }
    }
    Ok(())
}

pub(crate) fn copy_new_file(source: &Path, destination: &Path) -> AuthorityResult<()> {
    let mut source_file = File::open(source)?;
    let mut destination_file = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(destination)?;
    std::io::copy(&mut source_file, &mut destination_file)?;
    destination_file.sync_all()?;
    Ok(())
}

fn sync_regular_files(root: &Path) -> AuthorityResult<()> {
    let mut pending_directories = vec![root.to_path_buf()];
    while let Some(directory) = pending_directories.pop() {
        for directory_entry in std::fs::read_dir(directory)? {
            let directory_entry = directory_entry?;
            let file_type = directory_entry.file_type()?;
            if file_type.is_symlink() {
                return Err("TUF publication output contains a symbolic link".into());
            }
            if file_type.is_dir() {
                pending_directories.push(directory_entry.path());
            } else if file_type.is_file() {
                OpenOptions::new()
                    .read(true)
                    .write(true)
                    .open(directory_entry.path())?
                    .sync_all()?;
            } else {
                return Err("TUF publication output contains an unsupported entry".into());
            }
        }
    }
    Ok(())
}

fn write_new_public_file(path: &Path, bytes: &[u8]) -> AuthorityResult<()> {
    let mut output = OpenOptions::new().write(true).create_new(true).open(path)?;
    output.write_all(bytes)?;
    if !bytes.ends_with(b"\n") {
        output.write_all(b"\n")?;
    }
    output.sync_all()?;
    Ok(())
}

pub(crate) fn directory_url(path: &Path) -> AuthorityResult<Url> {
    let canonical_path = path.canonicalize()?;
    Url::from_directory_path(&canonical_path).map_err(|_| {
        format!(
            "cannot convert directory to URL: {}",
            canonical_path.display()
        )
        .into()
    })
}

fn publication_staging_path(output_path: &Path) -> AuthorityResult<PathBuf> {
    let parent = require_explicit_parent(output_path)?;
    let output_name = output_path
        .file_name()
        .ok_or("TUF publication output lacks a directory name")?
        .to_string_lossy();
    Ok(parent.join(format!(".{output_name}.{}.pending", uuid::Uuid::new_v4())))
}

fn tempfile_directory_next_to(path: &Path) -> AuthorityResult<PathBuf> {
    let parent = path.parent().ok_or("verification path lacks a parent")?;
    Ok(parent.join(format!(".verify-{}", uuid::Uuid::new_v4())))
}

fn next_version(current: NonZeroU64) -> AuthorityResult<NonZeroU64> {
    NonZeroU64::new(
        current
            .get()
            .checked_add(1)
            .ok_or("TUF role version exhausted")?,
    )
    .ok_or_else(|| "TUF role version exhausted".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use jiff::SignedDuration;
    use px_release_catalog::{
        Architecture, Channel, Distribution, OperatingSystem, Product, ReleaseQuery,
    };

    struct AuthorityFixture {
        _temporary_directory: tempfile::TempDir,
        root_path: PathBuf,
        root_key_paths: Vec<PathBuf>,
        targets_key_path: PathBuf,
        snapshot_key_path: PathBuf,
        timestamp_key_path: PathBuf,
    }

    impl AuthorityFixture {
        async fn new() -> Self {
            let temporary_directory = tempfile::Builder::new()
                .prefix("pixels-update-authority-")
                .tempdir()
                .unwrap();
            make_private_directory(temporary_directory.path());
            let key_directory = temporary_directory.path().join("keys");
            std::fs::create_dir(&key_directory).unwrap();
            make_private_directory(&key_directory);
            let root_key_paths = vec![
                key_directory.join("root-one.pk8"),
                key_directory.join("root-two.pk8"),
                key_directory.join("root-three.pk8"),
            ];
            let targets_key_path = key_directory.join("targets.pk8");
            let snapshot_key_path = key_directory.join("snapshot.pk8");
            let timestamp_key_path = key_directory.join("timestamp.pk8");
            for key_path in root_key_paths.iter().chain([
                &targets_key_path,
                &snapshot_key_path,
                &timestamp_key_path,
            ]) {
                generate_signing_key(key_path).unwrap();
            }
            let root_path = temporary_directory.path().join("root.json");
            create_initial_root(&RootCreation {
                root_signing_key_paths: root_key_paths.clone(),
                root_signature_threshold: 2,
                targets_signing_key_path: targets_key_path.clone(),
                snapshot_signing_key_path: snapshot_key_path.clone(),
                timestamp_signing_key_path: timestamp_key_path.clone(),
                version: 1,
                expires_at: Timestamp::now() + SignedDuration::from_hours(24 * 365),
                output_path: root_path.clone(),
            })
            .await
            .unwrap();
            Self {
                _temporary_directory: temporary_directory,
                root_path,
                root_key_paths,
                targets_key_path,
                snapshot_key_path,
                timestamp_key_path,
            }
        }

        fn directory(&self) -> &Path {
            self._temporary_directory.path()
        }

        fn release(&self, build_number: i64, target_name: &str, artifact: &[u8]) -> ReleaseSpec {
            ReleaseSpec {
                target: ReleaseQuery {
                    product: Product::CloudNode,
                    distribution: Distribution::Official,
                    channel: Channel::Stable,
                    os: OperatingSystem::Windows,
                    architecture: Architecture::X86_64,
                },
                build_number,
                version: format!("3.3.{build_number}"),
                metadata_base_url: "https://updates.example.test/metadata/".into(),
                targets_base_url: "https://updates.example.test/targets/".into(),
                target_name: target_name.into(),
                sha256: hex::encode(Sha256::digest(artifact)),
                platform_signer_sha256: Some("b".repeat(64)),
                size_bytes: i64::try_from(artifact.len()).unwrap(),
            }
        }

        fn publication(
            &self,
            release_spec_path: PathBuf,
            artifact_path: PathBuf,
            previous_repository_path: Option<PathBuf>,
            output_path: PathBuf,
        ) -> RepositoryPublication {
            RepositoryPublication {
                root_path: self.root_path.clone(),
                targets_signing_key_path: self.targets_key_path.clone(),
                snapshot_signing_key_path: self.snapshot_key_path.clone(),
                timestamp_signing_key_path: self.timestamp_key_path.clone(),
                release_spec_path,
                artifact_path,
                previous_repository_path,
                output_path,
                targets_expires_at: Timestamp::now() + SignedDuration::from_hours(24 * 90),
                snapshot_expires_at: Timestamp::now() + SignedDuration::from_hours(24 * 7),
                timestamp_expires_at: Timestamp::now() + SignedDuration::from_hours(24),
            }
        }
    }

    #[tokio::test]
    async fn root_uses_threshold_and_is_never_overwritten() {
        let fixture = AuthorityFixture::new().await;
        let root_bytes = std::fs::read(&fixture.root_path).unwrap();
        let signed_root: Signed<Root> = serde_json::from_slice(&root_bytes).unwrap();
        signed_root.signed.verify_role(&signed_root).unwrap();
        assert_eq!(signed_root.signed.roles[&RoleType::Root].threshold.get(), 2);
        assert_eq!(signed_root.signed.roles[&RoleType::Root].keyids.len(), 3);
        assert_eq!(signed_root.signed.keys.len(), 6);

        let duplicate_creation = RootCreation {
            root_signing_key_paths: fixture.root_key_paths.clone(),
            root_signature_threshold: 2,
            targets_signing_key_path: fixture.targets_key_path.clone(),
            snapshot_signing_key_path: fixture.snapshot_key_path.clone(),
            timestamp_signing_key_path: fixture.timestamp_key_path.clone(),
            version: 1,
            expires_at: Timestamp::now() + SignedDuration::from_hours(24 * 365),
            output_path: fixture.root_path.clone(),
        };
        assert!(create_initial_root(&duplicate_creation).await.is_err());
        assert_eq!(std::fs::read(&fixture.root_path).unwrap(), root_bytes);
    }

    #[tokio::test]
    async fn root_rotation_requires_old_and_new_thresholds() {
        let fixture = AuthorityFixture::new().await;
        let next_key_directory = fixture.directory().join("next-keys");
        std::fs::create_dir(&next_key_directory).unwrap();
        make_private_directory(&next_key_directory);
        let next_root_key_paths = vec![
            next_key_directory.join("root-one.pk8"),
            next_key_directory.join("root-two.pk8"),
            next_key_directory.join("root-three.pk8"),
        ];
        let next_targets_key_path = next_key_directory.join("targets.pk8");
        let next_snapshot_key_path = next_key_directory.join("snapshot.pk8");
        let next_timestamp_key_path = next_key_directory.join("timestamp.pk8");
        for key_path in next_root_key_paths.iter().chain([
            &next_targets_key_path,
            &next_snapshot_key_path,
            &next_timestamp_key_path,
        ]) {
            generate_signing_key(key_path).unwrap();
        }
        let rotated_root_path = fixture.directory().join("2.root.json");
        let rotation = RootRotation {
            current_root_path: fixture.root_path.clone(),
            current_root_signing_key_paths: fixture.root_key_paths.clone(),
            new_root_signing_key_paths: next_root_key_paths,
            new_root_signature_threshold: 2,
            new_targets_signing_key_path: next_targets_key_path,
            new_snapshot_signing_key_path: next_snapshot_key_path,
            new_timestamp_signing_key_path: next_timestamp_key_path,
            expires_at: Timestamp::now() + SignedDuration::from_hours(24 * 730),
            output_path: rotated_root_path.clone(),
        };
        rotate_root(&rotation).await.unwrap();
        let current_root: Signed<Root> =
            serde_json::from_slice(&std::fs::read(&fixture.root_path).unwrap()).unwrap();
        let rotated_root: Signed<Root> =
            serde_json::from_slice(&std::fs::read(&rotated_root_path).unwrap()).unwrap();
        current_root.signed.verify_role(&rotated_root).unwrap();
        rotated_root.signed.verify_role(&rotated_root).unwrap();
        assert_eq!(rotated_root.signed.version.get(), 2);
        assert!(rotated_root.signatures.len() >= 4);

        let insufficient_current_keys = RootRotation {
            current_root_signing_key_paths: vec![fixture.root_key_paths[0].clone()],
            output_path: fixture.directory().join("rejected-root.json"),
            ..rotation
        };
        assert!(rotate_root(&insufficient_current_keys).await.is_err());
        assert!(!insufficient_current_keys.output_path.exists());
    }

    #[tokio::test]
    async fn publication_after_root_rotation_preserves_and_verifies_the_root_chain() {
        let fixture = AuthorityFixture::new().await;
        let first_artifact = b"installer signed by the first authority";
        let first_artifact_path = fixture.directory().join("first-authority-installer.exe");
        std::fs::write(&first_artifact_path, first_artifact).unwrap();
        let first_release = fixture.release(
            30380,
            "cloud_node/official/stable/windows/x86_64/30380/installer.exe",
            first_artifact,
        );
        let first_release_path = fixture.directory().join("first-authority-release.json");
        std::fs::write(
            &first_release_path,
            serde_json::to_vec_pretty(&first_release).unwrap(),
        )
        .unwrap();
        let first_repository_path = fixture.directory().join("first-authority-repository");
        publish_repository(&fixture.publication(
            first_release_path,
            first_artifact_path,
            None,
            first_repository_path.clone(),
        ))
        .await
        .unwrap();

        let rotated_key_directory = fixture.directory().join("rotated-authority-keys");
        std::fs::create_dir(&rotated_key_directory).unwrap();
        make_private_directory(&rotated_key_directory);
        let rotated_root_key_paths = vec![
            rotated_key_directory.join("root-one.pk8"),
            rotated_key_directory.join("root-two.pk8"),
            rotated_key_directory.join("root-three.pk8"),
        ];
        let rotated_targets_key_path = rotated_key_directory.join("targets.pk8");
        let rotated_snapshot_key_path = rotated_key_directory.join("snapshot.pk8");
        let rotated_timestamp_key_path = rotated_key_directory.join("timestamp.pk8");
        for key_path in rotated_root_key_paths.iter().chain([
            &rotated_targets_key_path,
            &rotated_snapshot_key_path,
            &rotated_timestamp_key_path,
        ]) {
            generate_signing_key(key_path).unwrap();
        }
        let rotated_root_path = fixture.directory().join("2.root.json");
        rotate_root(&RootRotation {
            current_root_path: fixture.root_path.clone(),
            current_root_signing_key_paths: fixture.root_key_paths.clone(),
            new_root_signing_key_paths: rotated_root_key_paths,
            new_root_signature_threshold: 2,
            new_targets_signing_key_path: rotated_targets_key_path.clone(),
            new_snapshot_signing_key_path: rotated_snapshot_key_path.clone(),
            new_timestamp_signing_key_path: rotated_timestamp_key_path.clone(),
            expires_at: Timestamp::now() + SignedDuration::from_hours(24 * 730),
            output_path: rotated_root_path.clone(),
        })
        .await
        .unwrap();

        let second_artifact = b"installer signed by the rotated authority";
        let second_artifact_path = fixture.directory().join("rotated-authority-installer.exe");
        std::fs::write(&second_artifact_path, second_artifact).unwrap();
        let second_release = fixture.release(
            30381,
            "cloud_node/official/stable/windows/x86_64/30381/installer.exe",
            second_artifact,
        );
        let second_release_path = fixture.directory().join("rotated-authority-release.json");
        std::fs::write(
            &second_release_path,
            serde_json::to_vec_pretty(&second_release).unwrap(),
        )
        .unwrap();
        let rotated_repository_path = fixture.directory().join("rotated-authority-repository");
        publish_repository(&RepositoryPublication {
            root_path: rotated_root_path,
            targets_signing_key_path: rotated_targets_key_path,
            snapshot_signing_key_path: rotated_snapshot_key_path,
            timestamp_signing_key_path: rotated_timestamp_key_path,
            release_spec_path: second_release_path,
            artifact_path: second_artifact_path,
            previous_repository_path: Some(first_repository_path),
            output_path: rotated_repository_path.clone(),
            targets_expires_at: Timestamp::now() + SignedDuration::from_hours(24 * 90),
            snapshot_expires_at: Timestamp::now() + SignedDuration::from_hours(24 * 7),
            timestamp_expires_at: Timestamp::now() + SignedDuration::from_hours(24),
        })
        .await
        .unwrap();

        assert!(rotated_repository_path
            .join("metadata/1.root.json")
            .is_file());
        assert!(rotated_repository_path
            .join("metadata/2.root.json")
            .is_file());
        let repository = load_test_repository(&fixture.root_path, &rotated_repository_path).await;
        assert_eq!(repository.root().signed.version.get(), 2);
        assert_eq!(repository.targets().signed.version.get(), 2);
        assert_eq!(repository.targets().signed.targets.len(), 2);
    }

    #[tokio::test]
    async fn publication_is_verified_immutable_and_preserves_history() {
        let fixture = AuthorityFixture::new().await;
        let first_artifact = b"first signed installer";
        let first_artifact_path = fixture.directory().join("first-installer.exe");
        std::fs::write(&first_artifact_path, first_artifact).unwrap();
        let first_release = fixture.release(
            30380,
            "cloud_node/official/stable/windows/x86_64/30380/installer.exe",
            first_artifact,
        );
        let first_spec_path = fixture.directory().join("first-release.json");
        std::fs::write(
            &first_spec_path,
            serde_json::to_vec_pretty(&first_release).unwrap(),
        )
        .unwrap();
        let first_repository_path = fixture.directory().join("repository-one");
        publish_repository(&fixture.publication(
            first_spec_path,
            first_artifact_path,
            None,
            first_repository_path.clone(),
        ))
        .await
        .unwrap();
        let first_repository =
            load_test_repository(&fixture.root_path, &first_repository_path).await;
        assert_eq!(first_repository.targets().signed.version.get(), 1);
        assert!(first_repository
            .targets()
            .signed
            .targets
            .contains_key(&TargetName::new(first_release.target_name.clone()).unwrap()));
        let first_target = &first_repository.targets().signed.targets
            [&TargetName::new(first_release.target_name.clone()).unwrap()];
        assert_eq!(
            first_target.custom["pixels"]["platform_signer_sha256"],
            "b".repeat(64)
        );

        let second_artifact = b"second signed installer";
        let second_artifact_path = fixture.directory().join("second-installer.exe");
        std::fs::write(&second_artifact_path, second_artifact).unwrap();
        let second_release = fixture.release(
            30381,
            "cloud_node/official/stable/windows/x86_64/30381/installer.exe",
            second_artifact,
        );
        let second_spec_path = fixture.directory().join("second-release.json");
        std::fs::write(
            &second_spec_path,
            serde_json::to_vec_pretty(&second_release).unwrap(),
        )
        .unwrap();
        let first_published_target_path = first_repository_path
            .join("targets")
            .join(&first_release.target_name);
        std::fs::write(&first_published_target_path, b"corrupted historical target").unwrap();
        let corrupted_history_output = fixture.directory().join("repository-corrupted-history");
        assert!(publish_repository(&fixture.publication(
            second_spec_path.clone(),
            second_artifact_path.clone(),
            Some(first_repository_path.clone()),
            corrupted_history_output.clone(),
        ))
        .await
        .is_err());
        assert!(!corrupted_history_output.exists());
        std::fs::write(&first_published_target_path, first_artifact).unwrap();
        let second_repository_path = fixture.directory().join("repository-two");
        publish_repository(&fixture.publication(
            second_spec_path,
            second_artifact_path,
            Some(first_repository_path.clone()),
            second_repository_path.clone(),
        ))
        .await
        .unwrap();
        let second_repository =
            load_test_repository(&fixture.root_path, &second_repository_path).await;
        assert_eq!(second_repository.targets().signed.version.get(), 2);
        assert_eq!(second_repository.targets().signed.targets.len(), 2);

        let reused_artifact_path = fixture.directory().join("reused-installer.exe");
        std::fs::write(&reused_artifact_path, b"reused name bytes").unwrap();
        let reused_release =
            fixture.release(30382, &first_release.target_name, b"reused name bytes");
        let reused_spec_path = fixture.directory().join("reused-release.json");
        std::fs::write(
            &reused_spec_path,
            serde_json::to_vec_pretty(&reused_release).unwrap(),
        )
        .unwrap();
        let rejected_output = fixture.directory().join("repository-rejected");
        assert!(publish_repository(&fixture.publication(
            reused_spec_path,
            reused_artifact_path,
            Some(second_repository_path),
            rejected_output.clone(),
        ))
        .await
        .is_err());
        assert!(!rejected_output.exists());
    }

    #[tokio::test]
    async fn filesystem_promotion_is_approved_monotonic_and_resumable() {
        let fixture = AuthorityFixture::new().await;
        let first_artifact = b"first promoted signed installer";
        let first_artifact_path = fixture.directory().join("first-promoted-installer.exe");
        std::fs::write(&first_artifact_path, first_artifact).unwrap();
        let first_release = fixture.release(
            30390,
            "cloud_node/official/stable/windows/x86_64/30390/installer.exe",
            first_artifact,
        );
        let first_release_path = fixture.directory().join("first-promoted-release.json");
        std::fs::write(
            &first_release_path,
            serde_json::to_vec_pretty(&first_release).unwrap(),
        )
        .unwrap();
        let first_candidate_path = fixture.directory().join("first-promotion-candidate");
        publish_repository(&fixture.publication(
            first_release_path,
            first_artifact_path,
            None,
            first_candidate_path.clone(),
        ))
        .await
        .unwrap();
        let live_repository_path = fixture.directory().join("live-repository");
        let first_publication_sha256 =
            sha256_file(&first_candidate_path.join("publication.json")).unwrap();
        promote_repository(&RepositoryPromotion {
            candidate_repository_path: first_candidate_path.clone(),
            live_repository_path: live_repository_path.clone(),
            approved_publication_sha256: first_publication_sha256,
        })
        .await
        .unwrap();
        let first_live_repository =
            load_test_repository(&fixture.root_path, &live_repository_path).await;
        assert_eq!(first_live_repository.timestamp().signed.version.get(), 1);

        let second_artifact = b"second promoted signed installer";
        let second_artifact_path = fixture.directory().join("second-promoted-installer.exe");
        std::fs::write(&second_artifact_path, second_artifact).unwrap();
        let second_release = fixture.release(
            30391,
            "cloud_node/official/stable/windows/x86_64/30391/installer.exe",
            second_artifact,
        );
        let second_release_path = fixture.directory().join("second-promoted-release.json");
        std::fs::write(
            &second_release_path,
            serde_json::to_vec_pretty(&second_release).unwrap(),
        )
        .unwrap();
        let second_candidate_path = fixture.directory().join("second-promotion-candidate");
        publish_repository(&fixture.publication(
            second_release_path,
            second_artifact_path,
            Some(first_candidate_path),
            second_candidate_path.clone(),
        ))
        .await
        .unwrap();
        let second_publication_sha256 =
            sha256_file(&second_candidate_path.join("publication.json")).unwrap();
        let wrong_approval = RepositoryPromotion {
            candidate_repository_path: second_candidate_path.clone(),
            live_repository_path: live_repository_path.clone(),
            approved_publication_sha256: "f".repeat(64),
        };
        assert!(promote_repository(&wrong_approval).await.is_err());
        let unchanged_live_repository =
            load_test_repository(&fixture.root_path, &live_repository_path).await;
        assert_eq!(
            unchanged_live_repository.timestamp().signed.version.get(),
            1
        );

        let promotion_journal = serde_json::json!({
            "schema_version": 1,
            "approved_publication_sha256": second_publication_sha256.clone(),
            "created_at": Timestamp::now().to_string(),
        });
        std::fs::write(
            live_repository_path.join("promotion.pending.json"),
            serde_json::to_vec_pretty(&promotion_journal).unwrap(),
        )
        .unwrap();
        std::fs::copy(
            second_candidate_path.join("metadata/targets.json"),
            live_repository_path.join("metadata/targets.json"),
        )
        .unwrap();
        std::fs::copy(
            second_candidate_path.join("metadata/snapshot.json"),
            live_repository_path.join("metadata/snapshot.json"),
        )
        .unwrap();
        let second_live_target_path = live_repository_path
            .join("targets")
            .join(&second_release.target_name);
        std::fs::create_dir_all(second_live_target_path.parent().unwrap()).unwrap();
        std::fs::copy(
            second_candidate_path
                .join("targets")
                .join(&second_release.target_name),
            &second_live_target_path,
        )
        .unwrap();

        let console_request_id = uuid::Uuid::new_v4();
        let console_registration_path = fixture.directory().join("console-registration.json");
        let registration_preparation = ConsoleRegistrationPreparation {
            live_repository_path: live_repository_path.clone(),
            request_id: console_request_id,
            output_path: console_registration_path.clone(),
        };
        assert!(prepare_console_registration(&registration_preparation)
            .await
            .is_err());
        assert!(!console_registration_path.exists());

        promote_repository(&RepositoryPromotion {
            candidate_repository_path: second_candidate_path,
            live_repository_path: live_repository_path.clone(),
            approved_publication_sha256: second_publication_sha256.clone(),
        })
        .await
        .unwrap();
        assert!(!live_repository_path.join("promotion.pending.json").exists());
        let promoted_repository =
            load_test_repository(&fixture.root_path, &live_repository_path).await;
        assert_eq!(promoted_repository.timestamp().signed.version.get(), 2);
        assert_eq!(promoted_repository.targets().signed.targets.len(), 2);
        prepare_console_registration(&registration_preparation)
            .await
            .unwrap();
        let console_registration: serde_json::Value =
            serde_json::from_slice(&std::fs::read(&console_registration_path).unwrap()).unwrap();
        assert_eq!(
            console_registration["request_id"],
            console_request_id.to_string()
        );
        assert_eq!(
            console_registration["repository_publication_sha256"],
            second_publication_sha256
        );
        assert_eq!(
            console_registration["artifact"],
            serde_json::to_value(second_release).unwrap()
        );
        assert_eq!(console_registration["repository_root_version"], 1);
        assert!(prepare_console_registration(&registration_preparation)
            .await
            .is_err());
    }

    #[tokio::test]
    async fn publication_rejects_artifact_or_role_key_drift() {
        let fixture = AuthorityFixture::new().await;
        let approved_bytes = b"approved installer bytes";
        let artifact_path = fixture.directory().join("candidate.exe");
        std::fs::write(&artifact_path, approved_bytes).unwrap();
        let release = fixture.release(
            30382,
            "cloud_node/official/stable/windows/x86_64/30382/installer.exe",
            approved_bytes,
        );
        let release_spec_path = fixture.directory().join("candidate.json");
        std::fs::write(
            &release_spec_path,
            serde_json::to_vec_pretty(&release).unwrap(),
        )
        .unwrap();
        std::fs::write(&artifact_path, b"tampered installer bytes").unwrap();
        let output_path = fixture.directory().join("tampered-output");
        assert!(publish_repository(&fixture.publication(
            release_spec_path.clone(),
            artifact_path.clone(),
            None,
            output_path.clone(),
        ))
        .await
        .is_err());
        assert!(!output_path.exists());

        std::fs::write(&artifact_path, approved_bytes).unwrap();
        let wrong_role_configuration = RepositoryPublication {
            targets_signing_key_path: fixture.snapshot_key_path.clone(),
            ..fixture.publication(release_spec_path, artifact_path, None, output_path.clone())
        };
        assert!(publish_repository(&wrong_role_configuration).await.is_err());
        assert!(!output_path.exists());
    }

    async fn load_test_repository(root_path: &Path, repository_path: &Path) -> tough::Repository {
        let root_bytes = std::fs::read(root_path).unwrap();
        RepositoryLoader::new(
            &root_bytes,
            directory_url(&repository_path.join("metadata")).unwrap(),
            directory_url(&repository_path.join("targets")).unwrap(),
        )
        .expiration_enforcement(ExpirationEnforcement::Safe)
        .load()
        .await
        .unwrap()
    }

    fn make_private_directory(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o700)).unwrap();
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            use std::process::Command;
            let identity = Command::new("whoami")
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(identity.status.success());
            let access_rule = format!(
                "{}:(OI)(CI)F",
                String::from_utf8(identity.stdout).unwrap().trim()
            );
            let result = Command::new("icacls")
                .arg(path)
                .args([
                    "/inheritance:r",
                    "/grant:r",
                    &access_rule,
                    "*S-1-5-18:(OI)(CI)F",
                ])
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(result.status.success());
        }
    }
}
