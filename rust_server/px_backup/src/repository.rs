use crate::{retained_set_ids, RecoverySetManifest, RetentionPolicy};
use fs2::FileExt;
use sha2::{Digest, Sha256};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
};
use uuid::Uuid;

const MAX_MANIFEST_BYTES: u64 = 256 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum RepositoryError {
    #[error("invalid backup repository input")]
    InvalidInput,
    #[error("backup repository permission or type rejected")]
    Permission,
    #[error("backup repository is busy")]
    Busy,
    #[error("backup repository is unavailable")]
    Unavailable,
    #[error("backup repository requires reconciliation")]
    Corrupt,
    #[error("recovery set already exists")]
    Exists,
}

pub struct BackupRepository {
    root: PathBuf,
    deployment_id: Uuid,
    lock: File,
}

impl BackupRepository {
    pub fn open(root: &Path, deployment_id: Uuid) -> Result<Self, RepositoryError> {
        if !root.is_absolute() || deployment_id.is_nil() {
            return Err(RepositoryError::InvalidInput);
        }
        px_private_files::private::verify_private_directory(root)
            .map_err(|_| RepositoryError::Permission)?;
        let lock_path = root.join("repository.lock");
        let lock = private_lock_file(&lock_path)?;
        lock.try_lock_exclusive().map_err(|error| {
            if error.kind() == std::io::ErrorKind::WouldBlock || error.raw_os_error() == Some(33) {
                RepositoryError::Busy
            } else {
                RepositoryError::Unavailable
            }
        })?;
        Ok(Self {
            root: root.to_path_buf(),
            deployment_id,
            lock,
        })
    }

    pub fn begin_set(
        &self,
        recovery_set_id: Uuid,
    ) -> Result<StagedRecoverySet<'_>, RepositoryError> {
        if recovery_set_id.is_nil() {
            return Err(RepositoryError::InvalidInput);
        }
        if self.root.join(recovery_set_id.to_string()).exists() {
            return Err(RepositoryError::Exists);
        }
        let directory = self.root.join(format!(".partial-{recovery_set_id}"));
        create_private_directory(&directory)?;
        Ok(StagedRecoverySet {
            repository: self,
            recovery_set_id,
            directory,
        })
    }

    pub fn discard_incomplete_sets(&self) -> Result<BTreeSet<Uuid>, RepositoryError> {
        let mut incomplete_sets = Vec::new();
        for directory_entry in fs::read_dir(&self.root).map_err(|_| RepositoryError::Unavailable)? {
            let directory_entry = directory_entry.map_err(|_| RepositoryError::Unavailable)?;
            let file_name = directory_entry.file_name();
            let file_name = file_name.to_str().ok_or(RepositoryError::Corrupt)?;
            let Some(recovery_set_text) = file_name.strip_prefix(".partial-") else {
                continue;
            };
            let recovery_set_id =
                Uuid::parse_str(recovery_set_text).map_err(|_| RepositoryError::Corrupt)?;
            let file_type = directory_entry
                .file_type()
                .map_err(|_| RepositoryError::Unavailable)?;
            if !file_type.is_dir() || file_type.is_symlink() {
                return Err(RepositoryError::Corrupt);
            }
            px_private_files::private::verify_private_directory(&directory_entry.path())
                .map_err(|_| RepositoryError::Permission)?;
            let mut files = Vec::new();
            for partial_entry in
                fs::read_dir(directory_entry.path()).map_err(|_| RepositoryError::Unavailable)?
            {
                let partial_entry = partial_entry.map_err(|_| RepositoryError::Unavailable)?;
                let partial_name = partial_entry
                    .file_name()
                    .to_str()
                    .ok_or(RepositoryError::Corrupt)?
                    .to_string();
                if !matches!(
                    partial_name.as_str(),
                    "console.dump" | "auth.dump" | "desk.dump" | "manifest.json"
                ) {
                    return Err(RepositoryError::Corrupt);
                }
                let partial_type = partial_entry
                    .file_type()
                    .map_err(|_| RepositoryError::Unavailable)?;
                if !partial_type.is_file() || partial_type.is_symlink() {
                    return Err(RepositoryError::Corrupt);
                }
                verify_regular_private_file(
                    &File::open(partial_entry.path()).map_err(|_| RepositoryError::Unavailable)?,
                )?;
                files.push(partial_entry.path());
            }
            incomplete_sets.push((recovery_set_id, directory_entry.path(), files));
        }
        let mut discarded = BTreeSet::new();
        for (recovery_set_id, directory, files) in incomplete_sets {
            for file in files {
                fs::remove_file(file).map_err(|_| RepositoryError::Unavailable)?;
            }
            fs::remove_dir(directory).map_err(|_| RepositoryError::Unavailable)?;
            discarded.insert(recovery_set_id);
        }
        if !discarded.is_empty() {
            sync_directory(&self.root)?;
        }
        Ok(discarded)
    }

    pub fn replicate_verified_to(
        &self,
        destination: &BackupRepository,
        recovery_set_id: Uuid,
    ) -> Result<RecoverySetManifest, RepositoryError> {
        if recovery_set_id.is_nil()
            || self.deployment_id != destination.deployment_id
            || self.root == destination.root
        {
            return Err(RepositoryError::InvalidInput);
        }
        let source_manifests = self.manifests()?;
        let manifests_by_id = source_manifests
            .iter()
            .map(|manifest| (manifest.recovery_set_id, manifest))
            .collect::<BTreeMap<_, _>>();
        let mut dependency_chain = Vec::new();
        let mut next_recovery_set_id = Some(recovery_set_id);
        while let Some(current_recovery_set_id) = next_recovery_set_id {
            let source_manifest = manifests_by_id
                .get(&current_recovery_set_id)
                .copied()
                .filter(|manifest| manifest.status.is_verified())
                .ok_or(RepositoryError::InvalidInput)?;
            dependency_chain.push(source_manifest);
            next_recovery_set_id = source_manifest.previous_recovery_set_id;
        }
        dependency_chain.reverse();
        for source_manifest in dependency_chain {
            replicate_manifest(self, destination, source_manifest)?;
        }
        destination
            .manifests()?
            .into_iter()
            .find(|manifest| manifest.recovery_set_id == recovery_set_id)
            .ok_or(RepositoryError::Corrupt)
    }

    pub fn manifests(&self) -> Result<Vec<RecoverySetManifest>, RepositoryError> {
        let mut manifests = Vec::new();
        for entry in fs::read_dir(&self.root).map_err(|_| RepositoryError::Unavailable)? {
            let entry = entry.map_err(|_| RepositoryError::Unavailable)?;
            let name = entry.file_name();
            if name == "repository.lock" {
                continue;
            }
            let name = name.to_str().ok_or(RepositoryError::Corrupt)?;
            let id = Uuid::parse_str(name).map_err(|_| RepositoryError::Corrupt)?;
            let kind = entry
                .file_type()
                .map_err(|_| RepositoryError::Unavailable)?;
            if !kind.is_dir() || kind.is_symlink() {
                return Err(RepositoryError::Corrupt);
            }
            px_private_files::private::verify_private_directory(&entry.path())
                .map_err(|_| RepositoryError::Permission)?;
            let manifest = read_manifest(&entry.path().join("manifest.json"))?;
            if manifest.recovery_set_id != id || manifest.deployment_id != self.deployment_id {
                return Err(RepositoryError::Corrupt);
            }
            verify_recovery_set(&entry.path(), &manifest)?;
            manifests.push(manifest);
        }
        manifests.sort_by_key(|manifest| (manifest.created_at_unix, manifest.recovery_set_id));
        verify_dependency_graph(&manifests)?;
        Ok(manifests)
    }

    pub fn expired_candidates(
        &self,
        policy: RetentionPolicy,
        now_unix: u64,
    ) -> Result<BTreeSet<Uuid>, RepositoryError> {
        let manifests = self.manifests()?;
        let retained = retained_set_ids(&manifests, policy, now_unix);
        Ok(manifests
            .into_iter()
            .filter(|manifest| {
                manifest.status.is_verified()
                    && !manifest.locked
                    && !manifest.restoring
                    && !retained.contains(&manifest.recovery_set_id)
            })
            .map(|manifest| manifest.recovery_set_id)
            .collect())
    }

    pub fn prune_after_verified(
        &self,
        newest_recovery_set_id: Uuid,
        policy: RetentionPolicy,
        now_unix: u64,
    ) -> Result<BTreeSet<Uuid>, RepositoryError> {
        let manifests = self.manifests()?;
        let newest = manifests
            .iter()
            .filter(|manifest| manifest.status.is_verified())
            .max_by_key(|manifest| {
                (
                    manifest.completed_at_unix,
                    manifest.created_at_unix,
                    manifest.recovery_set_id,
                )
            })
            .ok_or(RepositoryError::InvalidInput)?;
        if newest.recovery_set_id != newest_recovery_set_id {
            return Err(RepositoryError::InvalidInput);
        }
        let retained = retained_set_ids(&manifests, policy, now_unix);
        let candidates = manifests
            .iter()
            .filter(|manifest| {
                manifest.status.is_verified()
                    && !manifest.locked
                    && !manifest.restoring
                    && !retained.contains(&manifest.recovery_set_id)
            })
            .map(|manifest| manifest.recovery_set_id)
            .collect::<BTreeSet<_>>();
        for recovery_set_id in &candidates {
            let manifest = manifests
                .iter()
                .find(|manifest| manifest.recovery_set_id == *recovery_set_id)
                .ok_or(RepositoryError::Corrupt)?;
            delete_recovery_set(&self.root, manifest)?;
        }
        sync_directory(&self.root)?;
        Ok(candidates)
    }

    pub fn deployment_id(&self) -> Uuid {
        self.deployment_id
    }
}

fn replicate_manifest(
    source: &BackupRepository,
    destination: &BackupRepository,
    source_manifest: &RecoverySetManifest,
) -> Result<(), RepositoryError> {
    let mut offsite_manifest = source_manifest.clone();
    offsite_manifest.status = crate::RecoverySetStatus::OffsiteVerified;
    if let Some(existing_manifest) = destination
        .manifests()?
        .into_iter()
        .find(|manifest| manifest.recovery_set_id == source_manifest.recovery_set_id)
    {
        return if existing_manifest == offsite_manifest {
            Ok(())
        } else {
            Err(RepositoryError::Corrupt)
        };
    }
    let staged = destination.begin_set(source_manifest.recovery_set_id)?;
    let source_directory = source
        .root
        .join(source_manifest.recovery_set_id.to_string());
    for member in &source_manifest.members {
        if let crate::BackupMemberState::Required { archive_file, .. } = &member.member {
            let destination_archive = staged.prepare_archive(member.service)?;
            let copied_bytes = fs::copy(source_directory.join(archive_file), &destination_archive)
                .map_err(|_| RepositoryError::Unavailable)?;
            if copied_bytes == 0 {
                return Err(RepositoryError::Corrupt);
            }
            OpenOptions::new()
                .write(true)
                .open(&destination_archive)
                .and_then(|archive| archive.sync_all())
                .map_err(|_| RepositoryError::Unavailable)?;
        }
    }
    staged.publish(&offsite_manifest)?;
    let verified_manifest = destination
        .manifests()?
        .into_iter()
        .find(|manifest| manifest.recovery_set_id == source_manifest.recovery_set_id)
        .ok_or(RepositoryError::Corrupt)?;
    if verified_manifest != offsite_manifest {
        return Err(RepositoryError::Corrupt);
    }
    Ok(())
}

fn verify_recovery_set(
    directory: &Path,
    manifest: &RecoverySetManifest,
) -> Result<(), RepositoryError> {
    let mut expected = BTreeMap::from([("manifest.json".to_string(), None)]);
    for member in &manifest.members {
        if let crate::BackupMemberState::Required {
            archive_file,
            archive_sha256,
            ..
        } = &member.member
        {
            if expected
                .insert(archive_file.clone(), Some(archive_sha256.as_str()))
                .is_some()
            {
                return Err(RepositoryError::Corrupt);
            }
        }
    }
    for entry in fs::read_dir(directory).map_err(|_| RepositoryError::Unavailable)? {
        let entry = entry.map_err(|_| RepositoryError::Unavailable)?;
        let name = entry
            .file_name()
            .to_str()
            .ok_or(RepositoryError::Corrupt)?
            .to_string();
        let expected_hash = expected.remove(&name).ok_or(RepositoryError::Corrupt)?;
        let kind = entry
            .file_type()
            .map_err(|_| RepositoryError::Unavailable)?;
        if !kind.is_file() || kind.is_symlink() {
            return Err(RepositoryError::Corrupt);
        }
        if let Some(expected_hash) = expected_hash {
            if hash_private_file(&entry.path())? != expected_hash {
                return Err(RepositoryError::Corrupt);
            }
        }
    }
    if expected.is_empty() {
        Ok(())
    } else {
        Err(RepositoryError::Corrupt)
    }
}

fn verify_dependency_graph(manifests: &[RecoverySetManifest]) -> Result<(), RepositoryError> {
    let dependencies = manifests
        .iter()
        .map(|manifest| (manifest.recovery_set_id, manifest.previous_recovery_set_id))
        .collect::<BTreeMap<_, _>>();
    for id in dependencies.keys() {
        let mut visited = BTreeSet::new();
        let mut current = Some(*id);
        while let Some(candidate) = current {
            if !visited.insert(candidate) {
                return Err(RepositoryError::Corrupt);
            }
            current = match dependencies.get(&candidate) {
                Some(previous) => *previous,
                None => return Err(RepositoryError::Corrupt),
            };
        }
    }
    Ok(())
}

fn delete_recovery_set(root: &Path, manifest: &RecoverySetManifest) -> Result<(), RepositoryError> {
    let directory = root.join(manifest.recovery_set_id.to_string());
    verify_recovery_set(&directory, manifest)?;
    for member in &manifest.members {
        if let crate::BackupMemberState::Required { archive_file, .. } = &member.member {
            fs::remove_file(directory.join(archive_file))
                .map_err(|_| RepositoryError::Unavailable)?;
        }
    }
    fs::remove_file(directory.join("manifest.json")).map_err(|_| RepositoryError::Unavailable)?;
    fs::remove_dir(&directory).map_err(|_| RepositoryError::Unavailable)
}

pub struct StagedRecoverySet<'a> {
    repository: &'a BackupRepository,
    recovery_set_id: Uuid,
    directory: PathBuf,
}

impl StagedRecoverySet<'_> {
    pub fn archive_path(&self, service: crate::BackupService) -> PathBuf {
        self.directory.join(match service {
            crate::BackupService::Console => "console.dump",
            crate::BackupService::Auth => "auth.dump",
            crate::BackupService::Desk => "desk.dump",
        })
    }

    pub fn prepare_archive(
        &self,
        service: crate::BackupService,
    ) -> Result<PathBuf, RepositoryError> {
        let path = self.archive_path(service);
        drop(private_new_file(&path)?);
        Ok(path)
    }

    pub fn publish(self, manifest: &RecoverySetManifest) -> Result<(), RepositoryError> {
        manifest
            .validate()
            .map_err(|_| RepositoryError::InvalidInput)?;
        if manifest.deployment_id != self.repository.deployment_id
            || manifest.recovery_set_id != self.recovery_set_id
            || !manifest.status.is_verified()
        {
            return Err(RepositoryError::InvalidInput);
        }
        for member in &manifest.members {
            if let crate::BackupMemberState::Required {
                archive_file,
                archive_sha256,
                ..
            } = &member.member
            {
                let expected = self.archive_path(member.service);
                if expected.file_name().and_then(|name| name.to_str()) != Some(archive_file) {
                    return Err(RepositoryError::InvalidInput);
                }
                if hash_private_file(&expected)? != *archive_sha256 {
                    return Err(RepositoryError::Corrupt);
                }
            }
        }
        let bytes =
            serde_json::to_vec_pretty(manifest).map_err(|_| RepositoryError::InvalidInput)?;
        if bytes.len() as u64 > MAX_MANIFEST_BYTES {
            return Err(RepositoryError::InvalidInput);
        }
        let path = self.directory.join("manifest.json");
        let mut file = private_new_file(&path)?;
        file.write_all(&bytes)
            .map_err(|_| RepositoryError::Unavailable)?;
        file.sync_all().map_err(|_| RepositoryError::Unavailable)?;
        drop(file);
        sync_directory(&self.directory)?;
        let destination = self.repository.root.join(self.recovery_set_id.to_string());
        fs::rename(&self.directory, &destination).map_err(|error| match error.kind() {
            std::io::ErrorKind::AlreadyExists => RepositoryError::Exists,
            _ => RepositoryError::Unavailable,
        })?;
        sync_directory(&self.repository.root)
    }

    pub fn path(&self) -> &Path {
        &self.directory
    }
}

impl Drop for BackupRepository {
    fn drop(&mut self) {
        let _ = FileExt::unlock(&self.lock);
    }
}

fn create_private_directory(path: &Path) -> Result<(), RepositoryError> {
    fs::create_dir(path).map_err(|error| match error.kind() {
        std::io::ErrorKind::AlreadyExists => RepositoryError::Exists,
        _ => RepositoryError::Unavailable,
    })?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o700))
            .map_err(|_| RepositoryError::Unavailable)?;
    }
    px_private_files::private::verify_private_directory(path)
        .map_err(|_| RepositoryError::Permission)
}

fn private_lock_file(path: &Path) -> Result<File, RepositoryError> {
    let mut options = OpenOptions::new();
    options.read(true).write(true).create(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
    }
    let file = options
        .open(path)
        .map_err(|_| RepositoryError::Unavailable)?;
    verify_regular_private_file(&file)?;
    Ok(file)
}

fn private_new_file(path: &Path) -> Result<File, RepositoryError> {
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
    }
    let file = options.open(path).map_err(|error| match error.kind() {
        std::io::ErrorKind::AlreadyExists => RepositoryError::Exists,
        _ => RepositoryError::Unavailable,
    })?;
    verify_regular_private_file(&file)?;
    Ok(file)
}

fn verify_regular_private_file(file: &File) -> Result<(), RepositoryError> {
    if !file
        .metadata()
        .map_err(|_| RepositoryError::Unavailable)?
        .is_file()
    {
        return Err(RepositoryError::Permission);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if file
            .metadata()
            .map_err(|_| RepositoryError::Unavailable)?
            .permissions()
            .mode()
            & 0o077
            != 0
        {
            return Err(RepositoryError::Permission);
        }
    }
    Ok(())
}

fn read_manifest(path: &Path) -> Result<RecoverySetManifest, RepositoryError> {
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW);
    }
    let file = options.open(path).map_err(|_| RepositoryError::Corrupt)?;
    verify_regular_private_file(&file)?;
    let size = file
        .metadata()
        .map_err(|_| RepositoryError::Unavailable)?
        .len();
    if size == 0 || size > MAX_MANIFEST_BYTES {
        return Err(RepositoryError::Corrupt);
    }
    let mut bytes = Vec::with_capacity(size as usize);
    file.take(MAX_MANIFEST_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| RepositoryError::Unavailable)?;
    if bytes.len() as u64 != size {
        return Err(RepositoryError::Corrupt);
    }
    let manifest: RecoverySetManifest =
        serde_json::from_slice(&bytes).map_err(|_| RepositoryError::Corrupt)?;
    manifest.validate().map_err(|_| RepositoryError::Corrupt)?;
    Ok(manifest)
}

fn hash_private_file(path: &Path) -> Result<String, RepositoryError> {
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW);
    }
    let mut file = options.open(path).map_err(|_| RepositoryError::Corrupt)?;
    verify_regular_private_file(&file)?;
    if file
        .metadata()
        .map_err(|_| RepositoryError::Unavailable)?
        .len()
        == 0
    {
        return Err(RepositoryError::Corrupt);
    }
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = file
            .read(&mut buffer)
            .map_err(|_| RepositoryError::Unavailable)?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

fn sync_directory(path: &Path) -> Result<(), RepositoryError> {
    #[cfg(windows)]
    {
        let _ = path;
        Ok(())
    }
    #[cfg(unix)]
    {
        let directory = File::open(path).map_err(|_| RepositoryError::Unavailable)?;
        directory
            .sync_all()
            .map_err(|_| RepositoryError::Unavailable)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        BackupMember, BackupMemberState, BackupService, RecoverySetKind, RecoverySetStatus,
        RetentionClass, MANIFEST_SCHEMA_VERSION,
    };
    use std::fs;

    struct Fixture {
        _base: tempfile::TempDir,
        root: PathBuf,
        deployment_id: Uuid,
    }

    impl Fixture {
        fn new() -> Self {
            Self::with_deployment(Uuid::new_v4())
        }

        fn with_deployment(deployment_id: Uuid) -> Self {
            let base = tempfile::Builder::new()
                .prefix("pixels-backup-repository-")
                .tempdir()
                .unwrap();
            make_private(base.path());
            let root = base.path().join("sets");
            fs::create_dir(&root).unwrap();
            make_private(&root);
            Self {
                _base: base,
                root,
                deployment_id,
            }
        }
    }

    fn make_private(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(path, fs::Permissions::from_mode(0o700)).unwrap();
        }
        #[cfg(windows)]
        {
            use std::{os::windows::process::CommandExt, process::Command};
            let identity = Command::new("whoami")
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(identity.status.success());
            let grant = format!(
                "{}:(OI)(CI)F",
                String::from_utf8(identity.stdout).unwrap().trim()
            );
            let result = Command::new("icacls")
                .arg(path)
                .args(["/inheritance:r", "/grant:r", &grant, "*S-1-5-18:(OI)(CI)F"])
                .creation_flags(0x08000000)
                .output()
                .unwrap();
            assert!(result.status.success());
        }
    }

    fn manifest(deployment_id: Uuid, index: u64) -> RecoverySetManifest {
        let archive_sha256 = format!("{:x}", Sha256::digest(b"archive"));
        RecoverySetManifest {
            schema_version: MANIFEST_SCHEMA_VERSION,
            recovery_set_id: Uuid::from_u128(index as u128 + 1),
            deployment_id,
            kind: RecoverySetKind::WriteBarrier,
            status: RecoverySetStatus::Verified,
            created_at_unix: index + 1,
            completed_at_unix: Some(index + 2),
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
                    database: "pixels_test".into(),
                    schema_version: 1,
                    archive_file: format!("{service:?}.dump").to_ascii_lowercase(),
                    archive_sha256: archive_sha256.clone(),
                    started_at_unix: index + 1,
                    completed_at_unix: index + 2,
                },
            })
            .collect(),
            failure_code: None,
        }
    }

    fn publish(repository: &BackupRepository, value: &RecoverySetManifest) {
        let staged = repository.begin_set(value.recovery_set_id).unwrap();
        for service in [
            BackupService::Console,
            BackupService::Auth,
            BackupService::Desk,
        ] {
            let path = staged.archive_path(service);
            fs::write(&path, b"archive").unwrap();
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                fs::set_permissions(&path, fs::Permissions::from_mode(0o600)).unwrap();
            }
        }
        staged.publish(value).unwrap();
    }

    #[test]
    fn repository_is_exclusive_deployment_bound_and_never_overwrites() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        assert!(matches!(
            BackupRepository::open(&fixture.root, fixture.deployment_id),
            Err(RepositoryError::Busy)
        ));
        let value = manifest(fixture.deployment_id, 1);
        publish(&repository, &value);
        assert_eq!(repository.manifests().unwrap(), vec![value.clone()]);
        assert!(matches!(
            repository.begin_set(value.recovery_set_id),
            Err(RepositoryError::Exists)
        ));
        drop(repository);
        assert!(BackupRepository::open(&fixture.root, Uuid::new_v4())
            .unwrap()
            .manifests()
            .is_err());
    }

    #[test]
    fn offsite_replication_is_idempotent_hash_verified_and_copies_dependencies_first() {
        let deployment_id = Uuid::new_v4();
        let source_fixture = Fixture::with_deployment(deployment_id);
        let destination_fixture = Fixture::with_deployment(deployment_id);
        let source = BackupRepository::open(&source_fixture.root, deployment_id).unwrap();
        let destination = BackupRepository::open(&destination_fixture.root, deployment_id).unwrap();
        let first = manifest(deployment_id, 70);
        let mut second = manifest(deployment_id, 71);
        second.previous_recovery_set_id = Some(first.recovery_set_id);
        publish(&source, &first);
        publish(&source, &second);
        let replicated = source
            .replicate_verified_to(&destination, second.recovery_set_id)
            .unwrap();
        assert_eq!(replicated.recovery_set_id, second.recovery_set_id);
        assert_eq!(replicated.status, RecoverySetStatus::OffsiteVerified);
        let offsite_manifests = destination.manifests().unwrap();
        assert_eq!(offsite_manifests.len(), 2);
        assert!(offsite_manifests
            .iter()
            .all(|manifest| manifest.status == RecoverySetStatus::OffsiteVerified));
        assert_eq!(
            source
                .replicate_verified_to(&destination, second.recovery_set_id)
                .unwrap(),
            replicated
        );
        fs::write(
            source_fixture
                .root
                .join(second.recovery_set_id.to_string())
                .join("console.dump"),
            b"tampered",
        )
        .unwrap();
        assert_eq!(
            source.replicate_verified_to(&destination, second.recovery_set_id),
            Err(RepositoryError::Corrupt)
        );
    }

    #[test]
    fn corrupt_or_unregistered_entries_fail_closed_and_expiry_is_read_only() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        for index in 1..=3 {
            publish(&repository, &manifest(fixture.deployment_id, index));
        }
        let expired = repository
            .expired_candidates(
                RetentionPolicy {
                    hourly: 1,
                    ..RetentionPolicy::default()
                },
                100,
            )
            .unwrap();
        assert_eq!(expired.len(), 2);
        for id in expired {
            assert!(fixture.root.join(id.to_string()).is_dir());
        }
        fs::write(fixture.root.join("unregistered"), b"do not delete").unwrap();
        assert_eq!(repository.manifests(), Err(RepositoryError::Corrupt));
        assert_eq!(
            fs::read(fixture.root.join("unregistered")).unwrap(),
            b"do not delete"
        );
    }

    #[test]
    fn staged_set_requires_exact_archive_names_and_hashes_before_atomic_publication() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        let value = manifest(fixture.deployment_id, 10);
        let staged = repository.begin_set(value.recovery_set_id).unwrap();
        for service in [
            BackupService::Console,
            BackupService::Auth,
            BackupService::Desk,
        ] {
            let path = staged.archive_path(service);
            fs::write(&path, b"wrong").unwrap();
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                fs::set_permissions(&path, fs::Permissions::from_mode(0o600)).unwrap();
            }
        }
        assert_eq!(staged.publish(&value), Err(RepositoryError::Corrupt));
        assert!(!fixture
            .root
            .join(value.recovery_set_id.to_string())
            .exists());
        assert!(fixture
            .root
            .join(format!(".partial-{}", value.recovery_set_id))
            .exists());
    }

    #[test]
    fn published_archives_are_reverified_and_dependency_gaps_fail_closed() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        let first = manifest(fixture.deployment_id, 20);
        publish(&repository, &first);
        assert_eq!(repository.manifests().unwrap(), vec![first.clone()]);
        fs::write(
            fixture
                .root
                .join(first.recovery_set_id.to_string())
                .join("console.dump"),
            b"tampered",
        )
        .unwrap();
        assert_eq!(repository.manifests(), Err(RepositoryError::Corrupt));

        let second_fixture = Fixture::new();
        let second_repository =
            BackupRepository::open(&second_fixture.root, second_fixture.deployment_id).unwrap();
        let mut dependent = manifest(second_fixture.deployment_id, 21);
        dependent.previous_recovery_set_id = Some(Uuid::new_v4());
        publish(&second_repository, &dependent);
        assert_eq!(second_repository.manifests(), Err(RepositoryError::Corrupt));
    }

    #[test]
    fn pruning_requires_the_newest_verified_set_and_deletes_only_registered_candidates() {
        let fixture = Fixture::new();
        let repository = BackupRepository::open(&fixture.root, fixture.deployment_id).unwrap();
        let values = (1..=3)
            .map(|index| manifest(fixture.deployment_id, index))
            .collect::<Vec<_>>();
        for value in &values {
            publish(&repository, value);
        }
        assert_eq!(
            repository.prune_after_verified(
                values[1].recovery_set_id,
                RetentionPolicy {
                    hourly: 1,
                    ..RetentionPolicy::default()
                },
                100,
            ),
            Err(RepositoryError::InvalidInput)
        );
        let deleted = repository
            .prune_after_verified(
                values[2].recovery_set_id,
                RetentionPolicy {
                    hourly: 1,
                    ..RetentionPolicy::default()
                },
                100,
            )
            .unwrap();
        assert_eq!(
            deleted,
            BTreeSet::from([values[0].recovery_set_id, values[1].recovery_set_id])
        );
        assert_eq!(repository.manifests().unwrap(), vec![values[2].clone()]);
    }
}
