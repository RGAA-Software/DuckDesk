use crate::{
    copy_new_file, directory_url, load_root_chain, read_bounded, reject_symbolic_link, sha256_file,
    AuthorityResult, MAXIMUM_TARGET_BYTES, MAXIMUM_TARGET_FILES,
};
use jiff::Timestamp;
use px_release_catalog::ReleaseSpec;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
#[cfg(unix)]
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Write;
use std::path::{Path, PathBuf};
use tough::{ExpirationEnforcement, Prefix, RepositoryLoader, TargetName};

const MAXIMUM_PUBLICATION_BYTES: u64 = 1024 * 1024;
const MAXIMUM_TARGETS_METADATA_BYTES: u64 = 16 * 1024 * 1024;
const MAXIMUM_SNAPSHOT_METADATA_BYTES: u64 = 4 * 1024 * 1024;
const MAXIMUM_TIMESTAMP_METADATA_BYTES: u64 = 1024 * 1024;
const PROMOTION_JOURNAL_NAME: &str = "promotion.pending.json";

#[derive(Debug, Clone)]
pub struct RepositoryPromotion {
    pub candidate_repository_path: PathBuf,
    pub live_repository_path: PathBuf,
    pub approved_publication_sha256: String,
}

#[derive(Debug, Clone)]
pub struct ConsoleRegistrationPreparation {
    pub live_repository_path: PathBuf,
    pub request_id: uuid::Uuid,
    pub output_path: PathBuf,
}

#[derive(Debug, Serialize)]
struct ConsoleRegistration {
    request_id: String,
    repository_publication_sha256: String,
    repository_root_version: u64,
    artifact: ReleaseSpec,
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct PublicationManifest {
    schema_version: u32,
    release_digest_sha256: String,
    root_sha256: String,
    targets_version: u64,
    snapshot_version: u64,
    timestamp_version: u64,
    created_at: String,
    release: ReleaseSpec,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct PromotionJournal {
    schema_version: u32,
    approved_publication_sha256: String,
    created_at: String,
}

struct VerifiedPublication {
    publication_bytes: Vec<u8>,
    publication_sha256: String,
    root_versions_and_sha256: Vec<(u64, String)>,
    targets: BTreeMap<String, (u64, String)>,
    targets_version: u64,
    snapshot_version: u64,
    timestamp_version: u64,
    release: ReleaseSpec,
}

pub async fn prepare_console_registration(
    configuration: &ConsoleRegistrationPreparation,
) -> AuthorityResult<()> {
    if !configuration.live_repository_path.is_absolute()
        || !configuration.live_repository_path.is_dir()
        || !configuration.output_path.is_absolute()
        || configuration.request_id.is_nil()
        || configuration
            .output_path
            .starts_with(&configuration.live_repository_path)
    {
        return Err("Console registration preparation requires a live repository, non-nil request ID, and separate absolute output path".into());
    }
    reject_symbolic_link(&configuration.live_repository_path)?;
    if configuration
        .live_repository_path
        .join(PROMOTION_JOURNAL_NAME)
        .exists()
    {
        return Err("cannot prepare Console registration while TUF promotion is pending".into());
    }
    let output_parent = configuration
        .output_path
        .parent()
        .ok_or("Console registration output does not have a parent")?;
    if !output_parent.is_dir() {
        return Err("Console registration output parent does not exist".into());
    }
    reject_symbolic_link(output_parent)?;
    if configuration.output_path.exists() {
        return Err(
            "Console registration output already exists and will not be overwritten".into(),
        );
    }

    let live_publication = verify_publication(&configuration.live_repository_path).await?;
    let repository_root_version = live_publication
        .root_versions_and_sha256
        .last()
        .ok_or("verified TUF repository does not contain a root")?
        .0;
    let registration = ConsoleRegistration {
        request_id: configuration.request_id.to_string(),
        repository_publication_sha256: live_publication.publication_sha256,
        repository_root_version,
        artifact: live_publication.release,
    };
    let mut registration_bytes = serde_json::to_vec_pretty(&registration)?;
    registration_bytes.push(b'\n');
    write_new_file(&configuration.output_path, &registration_bytes)?;
    sync_parent_directory(&configuration.output_path)
}

pub async fn promote_repository(configuration: &RepositoryPromotion) -> AuthorityResult<()> {
    validate_configuration(configuration)?;
    let candidate = verify_publication(&configuration.candidate_repository_path).await?;
    if candidate.publication_sha256 != configuration.approved_publication_sha256 {
        return Err("candidate publication does not match the externally approved SHA-256".into());
    }

    if !configuration.live_repository_path.exists() {
        return promote_initial_repository(configuration, &candidate).await;
    }

    reject_symbolic_link(&configuration.live_repository_path)?;
    if !configuration.live_repository_path.is_dir() {
        return Err("live TUF repository path is not a directory".into());
    }
    let journal_path = configuration
        .live_repository_path
        .join(PROMOTION_JOURNAL_NAME);
    if journal_path.exists() {
        validate_existing_journal(&journal_path, &candidate.publication_sha256)?;
    } else {
        let live_publication = verify_publication(&configuration.live_repository_path).await?;
        validate_progression(&live_publication, &candidate)?;
        create_journal(&journal_path, &candidate.publication_sha256)?;
    }

    apply_candidate(configuration, &candidate)?;
    let promoted_publication = verify_publication(&configuration.live_repository_path).await?;
    if promoted_publication.publication_sha256 != candidate.publication_sha256 {
        return Err("live TUF repository does not match the promoted publication".into());
    }
    std::fs::remove_file(&journal_path)?;
    sync_parent_directory(&journal_path)?;
    Ok(())
}

async fn promote_initial_repository(
    configuration: &RepositoryPromotion,
    candidate: &VerifiedPublication,
) -> AuthorityResult<()> {
    if candidate.targets_version != 1
        || candidate.snapshot_version != 1
        || candidate.timestamp_version != 1
    {
        return Err("initial live TUF publication must start all online roles at version 1".into());
    }
    let live_parent = configuration
        .live_repository_path
        .parent()
        .ok_or("live TUF repository path does not have a parent")?;
    let live_name = configuration
        .live_repository_path
        .file_name()
        .ok_or("live TUF repository path does not have a name")?
        .to_string_lossy();
    let staging_path = live_parent.join(format!(".{live_name}.{}.promotion", uuid::Uuid::new_v4()));
    std::fs::create_dir(&staging_path)?;
    let staging_result = copy_complete_candidate(
        &configuration.candidate_repository_path,
        &staging_path,
        candidate,
    );
    if let Err(error) = staging_result {
        let _ = std::fs::remove_dir_all(&staging_path);
        return Err(error);
    }
    let staged_publication = match verify_publication(&staging_path).await {
        Ok(staged_publication) => staged_publication,
        Err(error) => {
            let _ = std::fs::remove_dir_all(&staging_path);
            return Err(error);
        }
    };
    if staged_publication.publication_sha256 != candidate.publication_sha256 {
        let _ = std::fs::remove_dir_all(&staging_path);
        return Err("staged initial TUF publication changed during copy".into());
    }
    if let Err(error) = std::fs::rename(&staging_path, &configuration.live_repository_path) {
        let _ = std::fs::remove_dir_all(&staging_path);
        return Err(error.into());
    }
    sync_parent_directory(&configuration.live_repository_path)?;
    Ok(())
}

fn validate_configuration(configuration: &RepositoryPromotion) -> AuthorityResult<()> {
    if !configuration.candidate_repository_path.is_absolute()
        || !configuration.live_repository_path.is_absolute()
        || configuration.candidate_repository_path == configuration.live_repository_path
        || !configuration.candidate_repository_path.is_dir()
    {
        return Err("TUF promotion requires distinct absolute candidate and live paths".into());
    }
    let live_parent = configuration
        .live_repository_path
        .parent()
        .ok_or("live TUF repository path does not have a parent")?;
    if !live_parent.is_dir() {
        return Err("live TUF repository parent does not exist".into());
    }
    reject_symbolic_link(&configuration.candidate_repository_path)?;
    reject_symbolic_link(live_parent)?;
    if !is_lowercase_sha256(&configuration.approved_publication_sha256) {
        return Err(
            "approved publication SHA-256 must be 64 lowercase hexadecimal characters".into(),
        );
    }
    Ok(())
}

async fn verify_publication(repository_path: &Path) -> AuthorityResult<VerifiedPublication> {
    let metadata_path = repository_path.join("metadata");
    let targets_path = repository_path.join("targets");
    let publication_path = repository_path.join("publication.json");
    reject_symbolic_link(&metadata_path)?;
    reject_symbolic_link(&targets_path)?;
    reject_symbolic_link(&publication_path)?;
    if !metadata_path.is_dir() || !targets_path.is_dir() {
        return Err("TUF publication must contain metadata and targets directories".into());
    }

    let root_chain = load_root_chain(&metadata_path)?;
    let bootstrap_root_bytes = &root_chain
        .first()
        .ok_or("TUF publication root chain is empty")?
        .bytes;
    let repository = RepositoryLoader::new(
        bootstrap_root_bytes,
        directory_url(&metadata_path)?,
        directory_url(&targets_path)?,
    )
    .expiration_enforcement(ExpirationEnforcement::Safe)
    .load()
    .await?;
    let target_names: Vec<TargetName> = repository
        .all_targets()
        .map(|(target_name, _)| target_name.clone())
        .collect();
    if target_names.len() > MAXIMUM_TARGET_FILES {
        return Err("TUF publication exceeds the target count limit".into());
    }
    let declared_target_bytes = repository
        .all_targets()
        .try_fold(0_u64, |total_bytes, (_, target)| {
            total_bytes.checked_add(target.length)
        })
        .ok_or("TUF publication target size overflow")?;
    if declared_target_bytes > MAXIMUM_TARGET_BYTES {
        return Err("TUF publication exceeds the target size limit".into());
    }
    let verification_directory = tempfile::Builder::new()
        .prefix("pixels-tuf-promotion-")
        .tempdir()?;
    let mut targets = BTreeMap::new();
    let mut total_target_bytes = 0_u64;
    for target_name in &target_names {
        if let Err(error) = repository
            .save_target(target_name, verification_directory.path(), Prefix::None)
            .await
        {
            return Err(format!(
                "TUF publication target verification failed for {}: {error}",
                target_name.raw()
            )
            .into());
        }
        let verified_target_path = verification_directory.path().join(target_name.resolved());
        let target_size = std::fs::metadata(&verified_target_path)?.len();
        total_target_bytes = total_target_bytes
            .checked_add(target_size)
            .ok_or("TUF publication target size overflow")?;
        targets.insert(
            target_name.raw().to_owned(),
            (target_size, sha256_file(&verified_target_path)?),
        );
    }
    if total_target_bytes > MAXIMUM_TARGET_BYTES {
        return Err("TUF publication exceeds the target size limit".into());
    }

    let publication_bytes = read_bounded(&publication_path, MAXIMUM_PUBLICATION_BYTES)?;
    let publication_sha256 = hex::encode(Sha256::digest(&publication_bytes));
    let manifest: PublicationManifest = serde_json::from_slice(&publication_bytes)?;
    validate_manifest(&manifest, &root_chain, &repository, &targets)?;
    let release = manifest.release.clone();
    let root_versions_and_sha256 = root_chain
        .iter()
        .map(|root_entry| {
            (
                root_entry.version,
                hex::encode(Sha256::digest(&root_entry.bytes)),
            )
        })
        .collect();
    Ok(VerifiedPublication {
        publication_bytes,
        publication_sha256,
        root_versions_and_sha256,
        targets,
        targets_version: repository.targets().signed.version.get(),
        snapshot_version: repository.snapshot().signed.version.get(),
        timestamp_version: repository.timestamp().signed.version.get(),
        release,
    })
}

fn validate_manifest(
    manifest: &PublicationManifest,
    root_chain: &[crate::RootChainEntry],
    repository: &tough::Repository,
    targets: &BTreeMap<String, (u64, String)>,
) -> AuthorityResult<()> {
    manifest
        .release
        .validate()
        .map_err(|_| "TUF publication manifest contains an invalid release")?;
    crate::validate_immutable_target_name(&manifest.release)?;
    crate::validate_repository_release_domain(repository, &manifest.release.target)?;
    manifest
        .created_at
        .parse::<Timestamp>()
        .map_err(|_| "TUF publication manifest has an invalid creation time")?;
    let current_root = root_chain
        .last()
        .ok_or("TUF publication root chain is empty")?;
    let release_digest = hex::encode(
        manifest
            .release
            .digest()
            .map_err(|_| "TUF publication release digest is invalid")?,
    );
    let expected_target = targets
        .get(&manifest.release.target_name)
        .ok_or("TUF publication release target is absent")?;
    if manifest.schema_version != 1
        || manifest.release_digest_sha256 != release_digest
        || manifest.root_sha256 != hex::encode(Sha256::digest(&current_root.bytes))
        || manifest.targets_version != repository.targets().signed.version.get()
        || manifest.snapshot_version != repository.snapshot().signed.version.get()
        || manifest.timestamp_version != repository.timestamp().signed.version.get()
        || expected_target.0 != u64::try_from(manifest.release.size_bytes)?
        || expected_target.1 != manifest.release.sha256
    {
        return Err("TUF publication manifest does not match verified repository content".into());
    }
    Ok(())
}

fn validate_progression(
    live: &VerifiedPublication,
    candidate: &VerifiedPublication,
) -> AuthorityResult<()> {
    if live.targets_version.checked_add(1) != Some(candidate.targets_version)
        || live.snapshot_version.checked_add(1) != Some(candidate.snapshot_version)
        || live.timestamp_version.checked_add(1) != Some(candidate.timestamp_version)
        || candidate.targets.len() != live.targets.len() + 1
        || candidate.root_versions_and_sha256.len() < live.root_versions_and_sha256.len()
        || candidate.root_versions_and_sha256.len() > live.root_versions_and_sha256.len() + 1
        || !candidate
            .root_versions_and_sha256
            .starts_with(&live.root_versions_and_sha256)
    {
        return Err("candidate TUF publication is not the exact next live generation".into());
    }
    for (target_name, live_description) in &live.targets {
        if candidate.targets.get(target_name) != Some(live_description) {
            return Err("candidate TUF publication changed a historical target".into());
        }
    }
    Ok(())
}

fn create_journal(path: &Path, approved_publication_sha256: &str) -> AuthorityResult<()> {
    let journal = PromotionJournal {
        schema_version: 1,
        approved_publication_sha256: approved_publication_sha256.to_owned(),
        created_at: Timestamp::now().to_string(),
    };
    write_new_file(path, &serde_json::to_vec_pretty(&journal)?)?;
    sync_parent_directory(path)
}

fn validate_existing_journal(
    path: &Path,
    approved_publication_sha256: &str,
) -> AuthorityResult<()> {
    let journal_bytes = read_bounded(path, MAXIMUM_PUBLICATION_BYTES)?;
    let journal: PromotionJournal = serde_json::from_slice(&journal_bytes)?;
    journal
        .created_at
        .parse::<Timestamp>()
        .map_err(|_| "TUF promotion journal has an invalid creation time")?;
    if journal.schema_version != 1
        || journal.approved_publication_sha256 != approved_publication_sha256
    {
        return Err("another TUF publication promotion is already pending".into());
    }
    Ok(())
}

fn apply_candidate(
    configuration: &RepositoryPromotion,
    candidate: &VerifiedPublication,
) -> AuthorityResult<()> {
    let candidate_metadata = configuration.candidate_repository_path.join("metadata");
    let live_metadata = configuration.live_repository_path.join("metadata");
    let candidate_targets = configuration.candidate_repository_path.join("targets");
    let live_targets = configuration.live_repository_path.join("targets");
    create_directories_without_links(&live_metadata)?;
    create_directories_without_links(&live_targets)?;

    for (root_version, _) in &candidate.root_versions_and_sha256 {
        copy_immutable_file(
            &candidate_metadata.join(format!("{root_version}.root.json")),
            &live_metadata.join(format!("{root_version}.root.json")),
        )?;
    }
    for target_name in candidate.targets.keys() {
        let resolved_target = TargetName::new(target_name.clone())?;
        copy_immutable_file(
            &candidate_targets.join(resolved_target.resolved()),
            &live_targets.join(resolved_target.resolved()),
        )?;
    }
    atomic_replace_file(
        &candidate_metadata.join("targets.json"),
        &live_metadata.join("targets.json"),
        MAXIMUM_TARGETS_METADATA_BYTES,
    )?;
    atomic_replace_file(
        &candidate_metadata.join("snapshot.json"),
        &live_metadata.join("snapshot.json"),
        MAXIMUM_SNAPSHOT_METADATA_BYTES,
    )?;
    atomic_replace_file(
        &candidate_metadata.join("timestamp.json"),
        &live_metadata.join("timestamp.json"),
        MAXIMUM_TIMESTAMP_METADATA_BYTES,
    )?;
    atomic_replace_bytes(
        &configuration.live_repository_path.join("publication.json"),
        &candidate.publication_bytes,
    )?;
    Ok(())
}

fn copy_complete_candidate(
    candidate_path: &Path,
    destination_path: &Path,
    candidate: &VerifiedPublication,
) -> AuthorityResult<()> {
    let candidate_metadata = candidate_path.join("metadata");
    let destination_metadata = destination_path.join("metadata");
    let candidate_targets = candidate_path.join("targets");
    let destination_targets = destination_path.join("targets");
    std::fs::create_dir(&destination_metadata)?;
    std::fs::create_dir(&destination_targets)?;
    for (root_version, _) in &candidate.root_versions_and_sha256 {
        copy_immutable_file(
            &candidate_metadata.join(format!("{root_version}.root.json")),
            &destination_metadata.join(format!("{root_version}.root.json")),
        )?;
    }
    for target_name in candidate.targets.keys() {
        let resolved_target = TargetName::new(target_name.clone())?;
        copy_immutable_file(
            &candidate_targets.join(resolved_target.resolved()),
            &destination_targets.join(resolved_target.resolved()),
        )?;
    }
    for role_name in ["targets.json", "snapshot.json", "timestamp.json"] {
        copy_immutable_file(
            &candidate_metadata.join(role_name),
            &destination_metadata.join(role_name),
        )?;
    }
    write_new_file(
        &destination_path.join("publication.json"),
        &candidate.publication_bytes,
    )?;
    Ok(())
}

fn copy_immutable_file(source: &Path, destination: &Path) -> AuthorityResult<()> {
    reject_symbolic_link(source)?;
    if let Some(destination_parent) = destination.parent() {
        create_directories_without_links(destination_parent)?;
    }
    if destination.exists() {
        reject_symbolic_link(destination)?;
        if std::fs::metadata(source)?.len() != std::fs::metadata(destination)?.len()
            || sha256_file(source)? != sha256_file(destination)?
        {
            return Err(
                "immutable TUF publication object already exists with different bytes".into(),
            );
        }
        return Ok(());
    }
    copy_new_file(source, destination)?;
    if sha256_file(source)? != sha256_file(destination)? {
        return Err("copied TUF publication object failed SHA-256 verification".into());
    }
    sync_parent_directory(destination)
}

fn create_directories_without_links(path: &Path) -> AuthorityResult<()> {
    let mut missing_directories = Vec::new();
    let mut existing_ancestor = path;
    while !existing_ancestor.exists() {
        missing_directories.push(existing_ancestor.to_path_buf());
        existing_ancestor = existing_ancestor
            .parent()
            .ok_or("TUF publication directory does not have an existing ancestor")?;
    }
    reject_symbolic_link(existing_ancestor)?;
    if !existing_ancestor.is_dir() {
        return Err("TUF publication directory ancestor is not a directory".into());
    }
    for directory in missing_directories.into_iter().rev() {
        std::fs::create_dir(&directory)?;
        reject_symbolic_link(&directory)?;
    }
    reject_symbolic_link(path)?;
    Ok(())
}

fn atomic_replace_file(
    source: &Path,
    destination: &Path,
    maximum_bytes: u64,
) -> AuthorityResult<()> {
    let source_bytes = read_bounded(source, maximum_bytes)?;
    atomic_replace_bytes(destination, &source_bytes)
}

fn atomic_replace_bytes(destination: &Path, bytes: &[u8]) -> AuthorityResult<()> {
    let destination_parent = destination
        .parent()
        .ok_or("TUF publication destination does not have a parent")?;
    reject_symbolic_link(destination_parent)?;
    if destination.exists() {
        reject_symbolic_link(destination)?;
    }
    let destination_name = destination
        .file_name()
        .ok_or("TUF publication destination does not have a name")?
        .to_string_lossy();
    let pending_path = destination_parent.join(format!(
        ".{destination_name}.{}.pending",
        uuid::Uuid::new_v4()
    ));
    write_new_file(&pending_path, bytes)?;
    replace_file(&pending_path, destination)?;
    sync_parent_directory(destination)
}

fn write_new_file(path: &Path, bytes: &[u8]) -> AuthorityResult<()> {
    let mut output = OpenOptions::new().write(true).create_new(true).open(path)?;
    output.write_all(bytes)?;
    output.sync_all()?;
    Ok(())
}

#[cfg(windows)]
fn replace_file(source: &Path, destination: &Path) -> AuthorityResult<()> {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Storage::FileSystem::{
        MoveFileExW, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH,
    };
    let source_wide: Vec<u16> = source.as_os_str().encode_wide().chain(Some(0)).collect();
    let destination_wide: Vec<u16> = destination
        .as_os_str()
        .encode_wide()
        .chain(Some(0))
        .collect();
    if unsafe {
        MoveFileExW(
            source_wide.as_ptr(),
            destination_wide.as_ptr(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
        )
    } == 0
    {
        return Err(std::io::Error::last_os_error().into());
    }
    Ok(())
}

#[cfg(not(windows))]
fn replace_file(source: &Path, destination: &Path) -> AuthorityResult<()> {
    std::fs::rename(source, destination)?;
    Ok(())
}

#[cfg(unix)]
fn sync_parent_directory(path: &Path) -> AuthorityResult<()> {
    let parent = path
        .parent()
        .ok_or("TUF publication path does not have a parent")?;
    File::open(parent)?.sync_all()?;
    Ok(())
}

#[cfg(not(unix))]
fn sync_parent_directory(_path: &Path) -> AuthorityResult<()> {
    Ok(())
}

fn is_lowercase_sha256(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte_value| byte_value.is_ascii_digit() || (b'a'..=b'f').contains(&byte_value))
}
