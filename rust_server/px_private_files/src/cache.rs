use crate::file_lock::FileLock;
use crate::{
    directory::{Access, Directory},
    FileError, Result,
};
use sha2::{Digest, Sha256};
use std::{
    fs::File,
    io::{Read, Seek, SeekFrom, Write},
    path::Path,
    sync::Arc,
};
use uuid::Uuid;

/// Immutable file version, not a client-supplied pathname.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ContentIdentity {
    size: u64,
    sha256: [u8; 32],
}
impl ContentIdentity {
    pub fn new(size: u64, sha256: [u8; 32]) -> Result<Self> {
        if size == 0 || size > 1_099_511_627_776 {
            return Err(FileError::InvalidInput);
        }
        Ok(Self { size, sha256 })
    }
    pub fn size(self) -> u64 {
        self.size
    }
    pub fn sha256(self) -> [u8; 32] {
        self.sha256
    }
}
/// A single active owner for a local cache root. Every child guard retains its owner.
/// Provisioned roots are private, deployment-bound and never discovered by filename suffix.
pub struct CacheRoot {
    directory: Directory,
    _owner: FileLock,
    id: Uuid,
    deployment: Uuid,
}
impl CacheRoot {
    /// Explicit provisioning of an existing, empty, private directory. Never overwrites
    /// an identity or repairs a partial provisioning attempt automatically.
    pub fn initialize(path: &Path, deployment: Uuid) -> Result<Arc<Self>> {
        if deployment.is_nil() {
            return Err(FileError::InvalidInput);
        }
        let directory = Directory::open(path)?;
        if !directory.is_empty()? {
            return Err(FileError::Exists);
        }
        let owner = directory.open_file("owner.lock", Access::Create)?;
        let owner = FileLock::exclusive(owner)?;
        let id = Uuid::new_v4();
        let mut marker = directory.open_file("root.identity", Access::Create)?;
        marker.write_all(Self::marker(deployment, id).as_bytes())?;
        marker.sync_all()?;
        directory.sync()?;
        Ok(Arc::new(Self {
            directory,
            _owner: owner,
            id,
            deployment,
        }))
    }
    pub fn open(path: &Path, deployment: Uuid) -> Result<Arc<Self>> {
        if deployment.is_nil() {
            return Err(FileError::InvalidInput);
        }
        let directory = Directory::open(path)?;
        // Do not recreate a missing owner lock: replacing a lock inode can split ownership.
        let owner = directory.open_file("owner.lock", Access::Read)?;
        let owner = FileLock::exclusive(owner)?;
        let marker = directory.open_file("root.identity", Access::Read)?;
        let mut text = String::new();
        marker
            .take(257)
            .read_to_string(&mut text)
            .map_err(|_| FileError::Corrupt)?;
        if text.len() > 256 {
            return Err(FileError::Corrupt);
        }
        let parts: Vec<_> = text.split('\n').collect();
        if parts.len() != 4 || parts[0] != "Pixels-Cache-v1" || !parts[3].is_empty() {
            return Err(FileError::Corrupt);
        }
        let stored = Uuid::parse_str(parts[1]).map_err(|_| FileError::Corrupt)?;
        let id = Uuid::parse_str(parts[2]).map_err(|_| FileError::Corrupt)?;
        if stored != deployment || id.is_nil() || text != Self::marker(deployment, id) {
            return Err(FileError::Corrupt);
        }
        Ok(Arc::new(Self {
            directory,
            _owner: owner,
            id,
            deployment,
        }))
    }
    fn marker(deployment: Uuid, id: Uuid) -> String {
        format!("Pixels-Cache-v1\n{deployment}\n{id}\n")
    }
    pub fn id(&self) -> Uuid {
        self.id
    }
    pub fn deployment(&self) -> Uuid {
        self.deployment
    }
    pub fn try_lock_blob(self: &Arc<Self>, id: Uuid) -> Result<BlobGuard> {
        if id.is_nil() {
            return Err(FileError::InvalidInput);
        }
        let lock = self
            .directory
            .open_file(&format!("{id}.lock"), Access::Lock)?;
        let lock = FileLock::exclusive(lock)?;
        Ok(BlobGuard {
            root: self.clone(),
            id,
            _lock: lock,
        })
    }
    /// Authorization/ledger reference must be rechecked by the coordinator after this
    /// guard is obtained. Read leases must remain valid throughout network delivery.
    pub fn try_read(self: &Arc<Self>, id: Uuid, expected: ContentIdentity) -> Result<BlobReader> {
        if id.is_nil() {
            return Err(FileError::InvalidInput);
        }
        let lock = self
            .directory
            .open_file(&format!("{id}.lock"), Access::Lock)?;
        let lock = FileLock::shared(lock)?;
        let mut file = self
            .directory
            .open_file(&format!("{id}.blob"), Access::Read)?;
        if file.metadata()?.len() != expected.size {
            return Err(FileError::Corrupt);
        }
        let mut hash = Sha256::new();
        let mut buffer = [0_u8; 65536];
        let mut size = 0_u64;
        loop {
            let count = file.read(&mut buffer)?;
            if count == 0 {
                break;
            }
            size = size.checked_add(count as u64).ok_or(FileError::Corrupt)?;
            if size > expected.size {
                return Err(FileError::Corrupt);
            }
            hash.update(&buffer[..count]);
        }
        let actual: [u8; 32] = hash.finalize().into();
        if size != expected.size || actual != expected.sha256 {
            return Err(FileError::Corrupt);
        }
        file.seek(SeekFrom::Start(0))?;
        Ok(BlobReader {
            file,
            _lock: lock,
            _root: self.clone(),
            id,
            content: expected,
        })
    }
}
pub struct BlobGuard {
    _lock: FileLock,
    id: Uuid,
    root: Arc<CacheRoot>,
}
impl BlobGuard {
    pub fn id(&self) -> Uuid {
        self.id
    }
    pub fn root_id(&self) -> Uuid {
        self.root.id
    }
    /// A blob ID is one attempt only. Staging/published names are create-new, not reusable.
    pub fn begin_write(mut self, expected: ContentIdentity) -> Result<BlobWriter> {
        if self._lock.file.metadata()?.len() != 0 {
            return Err(FileError::Exists);
        }
        // Persist a one-attempt tombstone in the never-unlinked lock file. A cancelled
        // or collected ID cannot be reused even when no data file remains.
        self._lock.file.write_all(b"Pixels-Blob-v1\n")?;
        self._lock.file.sync_all()?;
        self.root.directory.sync()?;
        let file = self
            .root
            .directory
            .open_file(&format!("{}.part", self.id), Access::Create)?;
        Ok(BlobWriter {
            guard: self,
            file,
            expected,
            hash: Sha256::new(),
            written: 0,
            failed: false,
        })
    }
    /// Low-level exact-object removal only. The database coordinator MUST first prove
    /// this registered blob is unreferenced, unpinned and has no read lease, under this
    /// exclusive guard. Lock files are deliberately never unlinked while live.
    pub fn remove_data(&self) -> Result<bool> {
        let part = self.root.directory.remove(&format!("{}.part", self.id))?;
        let blob = self.root.directory.remove(&format!("{}.blob", self.id))?;
        Ok(part || blob)
    }
    /// Owns the exclusive lock through the database's final deleted acknowledgement.
    /// A never-started attempt is tombstoned too, so a delayed worker cannot create it.
    pub fn delete(mut self) -> Result<DeletedBlob> {
        if self._lock.file.metadata()?.len() == 0 {
            self._lock.file.write_all(b"Pixels-Blob-v1\n")?;
            self._lock.file.sync_all()?;
            self.root.directory.sync()?;
        }
        self.remove_data()?;
        Ok(DeletedBlob { guard: self })
    }
}
pub struct DeletedBlob {
    guard: BlobGuard,
}
impl DeletedBlob {
    pub fn id(&self) -> Uuid {
        self.guard.id
    }
    pub fn root_id(&self) -> Uuid {
        self.guard.root.id
    }
}
pub struct BlobWriter {
    file: File,
    expected: ContentIdentity,
    hash: Sha256,
    written: u64,
    failed: bool,
    guard: BlobGuard,
}
impl BlobWriter {
    pub fn id(&self) -> Uuid {
        self.guard.id
    }
    pub fn root_id(&self) -> Uuid {
        self.guard.root.id
    }
    pub fn content(&self) -> ContentIdentity {
        self.expected
    }
    pub fn append(&mut self, bytes: &[u8]) -> Result<()> {
        if self.failed {
            return Err(FileError::Corrupt);
        }
        self.failed = true;
        if bytes.is_empty() || bytes.len() > 1_048_576 {
            return Err(FileError::InvalidInput);
        }
        let total = self
            .written
            .checked_add(bytes.len() as u64)
            .ok_or(FileError::InvalidInput)?;
        if total > self.expected.size {
            return Err(FileError::Corrupt);
        }
        self.file.write_all(bytes)?;
        self.hash.update(bytes);
        self.written = total;
        self.failed = false;
        Ok(())
    }
    pub fn written(&self) -> u64 {
        self.written
    }
    pub fn finish(self) -> Result<PublishedBlob> {
        if self.failed || self.written != self.expected.size {
            return Err(FileError::Corrupt);
        }
        let hash: [u8; 32] = self.hash.finalize().into();
        if hash != self.expected.sha256 || self.file.metadata()?.len() != self.expected.size {
            return Err(FileError::Corrupt);
        }
        self.file.sync_all()?;
        drop(self.file);
        self.guard.root.directory.publish(
            &format!("{}.part", self.guard.id),
            &format!("{}.blob", self.guard.id),
        )?;
        Ok(PublishedBlob {
            guard: self.guard,
            content: self.expected,
        })
    }
}
/// Constructed only after complete size/hash verification, file sync and no-overwrite
/// publication. Retains the exclusive blob lock while a DB Ready commit is checked.
pub struct PublishedBlob {
    guard: BlobGuard,
    content: ContentIdentity,
}
impl PublishedBlob {
    pub fn id(&self) -> Uuid {
        self.guard.id
    }
    pub fn root_id(&self) -> Uuid {
        self.guard.root.id
    }
    pub fn content(&self) -> ContentIdentity {
        self.content
    }
    pub fn into_guard(self) -> BlobGuard {
        self.guard
    }
}
pub struct BlobReader {
    file: File,
    _lock: FileLock,
    _root: Arc<CacheRoot>,
    id: Uuid,
    content: ContentIdentity,
}
impl BlobReader {
    pub fn root_id(&self) -> Uuid {
        self._root.id
    }
    pub fn id(&self) -> Uuid {
        self.id
    }
    pub fn content(&self) -> ContentIdentity {
        self.content
    }
}
impl Read for BlobReader {
    fn read(&mut self, bytes: &mut [u8]) -> std::io::Result<usize> {
        self.file.read(bytes)
    }
}
impl Seek for BlobReader {
    fn seek(&mut self, position: SeekFrom) -> std::io::Result<u64> {
        self.file.seek(position)
    }
}
