use px_node_protocol::{RecordingCodec, RecordingReport};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    collections::{HashMap, HashSet},
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
    sync::{Arc, Mutex},
    time::UNIX_EPOCH,
};
use uuid::Uuid;

const INVENTORY_FILE: &str = "recording_inventory.json";
const INVENTORY_SCHEMA: u32 = 1;
const MAX_RECORDING_BYTES: u64 = 1_099_511_627_776;
const HASH_BUFFER_BYTES: usize = 1024 * 1024;

#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct RecordingEntry {
    source_id: Uuid,
    file_name: String,
    size_bytes: u64,
    modified_unix_ms: i64,
    source_sha256: [u8; 32],
    sequence: u64,
    present: bool,
    acknowledged: bool,
}

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct InventoryFile {
    schema: u32,
    entries: Vec<RecordingEntry>,
}

pub(crate) struct RecordingInventory {
    state_path: PathBuf,
    recording_root: PathBuf,
    entries: HashMap<Uuid, RecordingEntry>,
}

impl RecordingInventory {
    pub(crate) fn load(data_root: &Path) -> Result<Arc<Mutex<Self>>, String> {
        let parent = data_root
            .parent()
            .ok_or_else(|| "recording inventory data root has no parent".to_string())?;
        let state_path = data_root.join(INVENTORY_FILE);
        let recording_root = parent.join("px_render_records");
        let entries = if state_path.exists() {
            let metadata = fs::symlink_metadata(&state_path)
                .map_err(|_| "recording inventory state is unavailable".to_string())?;
            if !metadata.is_file() || is_reparse_point(&metadata) {
                return Err("recording inventory state must be a regular file".into());
            }
            let bytes = fs::read(&state_path)
                .map_err(|_| "recording inventory state cannot be read".to_string())?;
            let stored: InventoryFile = serde_json::from_slice(&bytes)
                .map_err(|_| "recording inventory state is invalid".to_string())?;
            if stored.schema != INVENTORY_SCHEMA || stored.entries.len() > 10_000 {
                return Err("recording inventory schema or size is invalid".into());
            }
            let mut entries = HashMap::with_capacity(stored.entries.len());
            for entry in stored.entries {
                validate_entry(&entry)?;
                if entries.insert(entry.source_id, entry).is_some() {
                    return Err("recording inventory contains duplicate source identities".into());
                }
            }
            entries
        } else {
            HashMap::new()
        };
        Ok(Arc::new(Mutex::new(Self {
            state_path,
            recording_root,
            entries,
        })))
    }

    pub(crate) fn scan(&mut self) -> Result<Vec<RecordingReport>, String> {
        let observations = scan_directory(&self.recording_root, &self.entries)?;
        let mut seen = HashSet::with_capacity(observations.len());
        for observation in observations {
            seen.insert(observation.source_id);
            self.entries.insert(observation.source_id, observation);
        }
        let disappeared: Vec<_> = self
            .entries
            .values()
            .filter(|entry| entry.present && !seen.contains(&entry.source_id))
            .map(|entry| entry.source_id)
            .collect();
        for source_id in disappeared {
            if self
                .entries
                .get(&source_id)
                .is_some_and(|entry| !entry.acknowledged)
            {
                self.entries.remove(&source_id);
                continue;
            }
            let entry = self.entries.get_mut(&source_id).unwrap();
            entry.present = false;
            entry.acknowledged = false;
            entry.sequence = entry
                .sequence
                .checked_add(1)
                .ok_or_else(|| "recording inventory sequence exhausted".to_string())?;
        }
        self.persist()?;
        Ok(self
            .entries
            .values()
            .filter(|entry| !entry.acknowledged)
            .map(report)
            .collect())
    }

    pub(crate) fn begin_connection(&mut self) -> Result<(), String> {
        for entry in self.entries.values_mut() {
            entry.acknowledged = false;
        }
        self.persist()
    }

    pub(crate) fn acknowledge(&mut self, source_id: Uuid, sequence: u64) -> Result<(), String> {
        let entry = self
            .entries
            .get_mut(&source_id)
            .ok_or_else(|| "recording acknowledgement source is unknown".to_string())?;
        if entry.sequence != sequence {
            return Err("recording acknowledgement sequence is stale".into());
        }
        entry.acknowledged = true;
        self.persist()
    }

    pub(crate) fn open_upload(
        &self,
        source_id: Uuid,
        expected_size: u64,
        expected_sha256: [u8; 32],
    ) -> Result<File, String> {
        let entry = self
            .entries
            .get(&source_id)
            .filter(|entry| entry.present)
            .ok_or_else(|| "recording upload source is not present".to_string())?;
        if entry.size_bytes != expected_size || entry.source_sha256 != expected_sha256 {
            return Err("recording upload content identity changed".into());
        }
        let path = self.recording_root.join(&entry.file_name);
        let metadata = fs::symlink_metadata(&path)
            .map_err(|_| "recording upload source is unavailable".to_string())?;
        if !metadata.is_file() || is_reparse_point(&metadata) || metadata.len() != entry.size_bytes
        {
            return Err("recording upload source type or size changed".into());
        }
        OpenOptions::new()
            .read(true)
            .open(path)
            .map_err(|_| "recording upload source cannot be opened".to_string())
    }

    fn persist(&self) -> Result<(), String> {
        let mut entries: Vec<_> = self.entries.values().cloned().collect();
        entries.sort_by_key(|entry| entry.source_id);
        let bytes = serde_json::to_vec(&InventoryFile {
            schema: INVENTORY_SCHEMA,
            entries,
        })
        .map_err(|_| "recording inventory cannot be serialized".to_string())?;
        let parent = self
            .state_path
            .parent()
            .ok_or_else(|| "recording inventory state has no parent".to_string())?;
        fs::create_dir_all(parent)
            .map_err(|_| "recording inventory directory cannot be created".to_string())?;
        let temporary = parent.join(format!(".{INVENTORY_FILE}.{}.tmp", Uuid::new_v4()));
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&temporary)
            .map_err(|_| "recording inventory temporary file cannot be created".to_string())?;
        let result = (|| {
            file.write_all(&bytes)
                .map_err(|_| "recording inventory cannot be written".to_string())?;
            file.sync_all()
                .map_err(|_| "recording inventory cannot be synchronized".to_string())?;
            replace_file(&temporary, &self.state_path)?;
            sync_directory(parent)
        })();
        if result.is_err() {
            let _ = fs::remove_file(&temporary);
        }
        result
    }
}

fn scan_directory(
    root: &Path,
    previous: &HashMap<Uuid, RecordingEntry>,
) -> Result<Vec<RecordingEntry>, String> {
    if !root.exists() {
        return Ok(Vec::new());
    }
    let root_metadata =
        fs::symlink_metadata(root).map_err(|_| "recording directory is unavailable".to_string())?;
    if !root_metadata.is_dir() || is_reparse_point(&root_metadata) {
        return Err("recording directory must not be a reparse point".into());
    }
    let mut by_name: HashMap<&str, &RecordingEntry> = HashMap::new();
    for entry in previous.values().filter(|entry| entry.present) {
        by_name.insert(&entry.file_name, entry);
    }
    let mut result = Vec::new();
    for directory_entry in
        fs::read_dir(root).map_err(|_| "recording directory cannot be listed".to_string())?
    {
        let directory_entry =
            directory_entry.map_err(|_| "recording directory entry is invalid".to_string())?;
        let metadata = fs::symlink_metadata(directory_entry.path())
            .map_err(|_| "recording file metadata is unavailable".to_string())?;
        let file_name = directory_entry
            .file_name()
            .into_string()
            .map_err(|_| "recording filename is not UTF-8".to_string())?;
        if !metadata.is_file()
            || is_reparse_point(&metadata)
            || !is_recording_basename(&file_name)
            || root.join(format!("{file_name}.recording")).exists()
            || metadata.len() == 0
            || metadata.len() > MAX_RECORDING_BYTES
        {
            continue;
        }
        let modified_unix_ms = i64::try_from(
            metadata
                .modified()
                .map_err(|_| "recording modification time is unavailable".to_string())?
                .duration_since(UNIX_EPOCH)
                .map_err(|_| "recording modification time predates the epoch".to_string())?
                .as_millis(),
        )
        .map_err(|_| "recording modification time overflows".to_string())?;
        if let Some(existing) = by_name.get(file_name.as_str()).filter(|entry| {
            entry.size_bytes == metadata.len() && entry.modified_unix_ms == modified_unix_ms
        }) {
            result.push((*existing).clone());
            continue;
        }
        let source_sha256 = hash_file(&directory_entry.path())?;
        let verified = fs::symlink_metadata(directory_entry.path())
            .map_err(|_| "recording file metadata changed during hashing".to_string())?;
        let verified_modified_unix_ms = i64::try_from(
            verified
                .modified()
                .map_err(|_| "recording modification time is unavailable".to_string())?
                .duration_since(UNIX_EPOCH)
                .map_err(|_| "recording modification time predates the epoch".to_string())?
                .as_millis(),
        )
        .map_err(|_| "recording modification time overflows".to_string())?;
        if !verified.is_file()
            || is_reparse_point(&verified)
            || verified.len() != metadata.len()
            || verified_modified_unix_ms != modified_unix_ms
        {
            continue;
        }
        result.push(RecordingEntry {
            source_id: Uuid::new_v4(),
            file_name,
            size_bytes: metadata.len(),
            modified_unix_ms,
            source_sha256,
            sequence: 1,
            present: true,
            acknowledged: false,
        });
    }
    Ok(result)
}

fn hash_file(path: &Path) -> Result<[u8; 32], String> {
    let mut file = File::open(path).map_err(|_| "recording file cannot be opened".to_string())?;
    let mut digest = Sha256::new();
    let mut buffer = vec![0_u8; HASH_BUFFER_BYTES];
    loop {
        let count = file
            .read(&mut buffer)
            .map_err(|_| "recording file cannot be read".to_string())?;
        if count == 0 {
            break;
        }
        digest.update(&buffer[..count]);
    }
    Ok(digest.finalize().into())
}

fn report(entry: &RecordingEntry) -> RecordingReport {
    RecordingReport {
        source_id: entry.source_id,
        source_sha256: entry.source_sha256,
        session_id: None,
        file_name: entry.file_name.clone(),
        size_bytes: entry.size_bytes,
        modified_unix_ms: entry.modified_unix_ms,
        codec: RecordingCodec::Unknown,
        sequence: entry.sequence,
        present: entry.present,
    }
}

fn validate_entry(entry: &RecordingEntry) -> Result<(), String> {
    if entry.source_id.is_nil()
        || !is_recording_basename(&entry.file_name)
        || entry.size_bytes == 0
        || entry.size_bytes > MAX_RECORDING_BYTES
        || entry.modified_unix_ms < 0
        || entry.sequence == 0
    {
        return Err("recording inventory entry is invalid".into());
    }
    Ok(())
}

fn is_recording_basename(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 255
        && value.to_ascii_lowercase().ends_with(".mp4")
        && !value.contains(['/', '\\'])
        && value != "."
        && value != ".."
        && !value.chars().any(char::is_control)
}

#[cfg(windows)]
fn is_reparse_point(metadata: &fs::Metadata) -> bool {
    use std::os::windows::fs::MetadataExt;
    metadata.file_attributes() & 0x400 != 0
}

#[cfg(not(windows))]
fn is_reparse_point(metadata: &fs::Metadata) -> bool {
    metadata.file_type().is_symlink()
}

#[cfg(windows)]
fn replace_file(source: &Path, destination: &Path) -> Result<(), String> {
    use std::os::windows::ffi::OsStrExt;
    use windows::{
        core::PCWSTR,
        Win32::Storage::FileSystem::{
            MoveFileExW, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH, MOVE_FILE_FLAGS,
        },
    };
    let source: Vec<u16> = source.as_os_str().encode_wide().chain(Some(0)).collect();
    let destination: Vec<u16> = destination
        .as_os_str()
        .encode_wide()
        .chain(Some(0))
        .collect();
    unsafe {
        MoveFileExW(
            PCWSTR(source.as_ptr()),
            PCWSTR(destination.as_ptr()),
            MOVE_FILE_FLAGS(MOVEFILE_REPLACE_EXISTING.0 | MOVEFILE_WRITE_THROUGH.0),
        )
    }
    .map_err(|_| "recording inventory cannot be published".to_string())
}

#[cfg(windows)]
fn sync_directory(_directory: &Path) -> Result<(), String> {
    Ok(())
}

#[cfg(not(windows))]
fn replace_file(source: &Path, destination: &Path) -> Result<(), String> {
    fs::rename(source, destination)
        .map_err(|_| "recording inventory cannot be published".to_string())
}

#[cfg(not(windows))]
fn sync_directory(directory: &Path) -> Result<(), String> {
    File::open(directory)
        .and_then(|directory| directory.sync_all())
        .map_err(|_| "recording inventory directory cannot be synchronized".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scan_persists_stable_identities_and_rejects_path_names() {
        let base = tempfile::tempdir().unwrap();
        let data_root = base.path().join("px_data");
        let recordings = base.path().join("px_render_records");
        fs::create_dir_all(&data_root).unwrap();
        fs::create_dir_all(&recordings).unwrap();
        fs::write(
            recordings.join("rec_mon0_20260919_04.00.00.mp4"),
            b"recording",
        )
        .unwrap();
        fs::write(recordings.join("ignored.txt"), b"not media").unwrap();
        let inventory = RecordingInventory::load(&data_root).unwrap();
        let first = inventory.lock().unwrap().scan().unwrap();
        assert_eq!(first.len(), 1);
        let source_id = first[0].source_id;
        let source_size = first[0].size_bytes;
        let source_hash = first[0].source_sha256;
        assert!(inventory
            .lock()
            .unwrap()
            .open_upload(source_id, source_size, source_hash)
            .is_ok());
        assert!(inventory
            .lock()
            .unwrap()
            .open_upload(source_id, source_size, [0; 32])
            .is_err());
        inventory
            .lock()
            .unwrap()
            .acknowledge(source_id, first[0].sequence)
            .unwrap();
        assert!(inventory.lock().unwrap().scan().unwrap().is_empty());
        drop(inventory);
        let reopened = RecordingInventory::load(&data_root).unwrap();
        assert!(reopened.lock().unwrap().scan().unwrap().is_empty());
        fs::remove_file(recordings.join("rec_mon0_20260919_04.00.00.mp4")).unwrap();
        let missing = reopened.lock().unwrap().scan().unwrap();
        assert_eq!(missing.len(), 1);
        assert_eq!(missing[0].source_id, source_id);
        assert!(!missing[0].present);
        assert_eq!(missing[0].sequence, 2);
        assert!(!is_recording_basename("../escape.mp4"));
        assert!(!is_recording_basename("sub/escape.mp4"));
    }
}
