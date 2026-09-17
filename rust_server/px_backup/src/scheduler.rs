use fs2::FileExt;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::{
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
};
use uuid::Uuid;

const STATE_SCHEMA_VERSION: u32 = 1;
const MAX_STATE_BYTES: u64 = 1024 * 1024;
const MAX_HISTORY: usize = 64;

#[derive(Debug, Clone, Copy, PartialEq, Eq, thiserror::Error)]
pub enum SchedulerError {
    #[error("invalid backup scheduler input")]
    InvalidInput,
    #[error("backup scheduler permission or type rejected")]
    Permission,
    #[error("backup scheduler is busy")]
    Busy,
    #[error("backup scheduler state requires reconciliation")]
    Corrupt,
    #[error("backup scheduler state is unavailable")]
    Unavailable,
    #[error("backup task identity or state conflict")]
    Conflict,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BackupScheduleConfig {
    pub deployment_id: Uuid,
    pub anchor_unix: u64,
    pub period_seconds: u64,
}

impl BackupScheduleConfig {
    pub(crate) fn validate(self) -> Result<(), SchedulerError> {
        if self.deployment_id.is_nil()
            || self.anchor_unix == 0
            || !(60..=31 * 24 * 60 * 60).contains(&self.period_seconds)
        {
            return Err(SchedulerError::InvalidInput);
        }
        Ok(())
    }

    fn latest_due(self, now_unix: u64) -> Result<Option<u64>, SchedulerError> {
        if now_unix < self.anchor_unix {
            return Ok(None);
        }
        let periods = (now_unix - self.anchor_unix) / self.period_seconds;
        self.anchor_unix
            .checked_add(
                periods
                    .checked_mul(self.period_seconds)
                    .ok_or(SchedulerError::InvalidInput)?,
            )
            .map(Some)
            .ok_or(SchedulerError::InvalidInput)
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "status", rename_all = "snake_case", deny_unknown_fields)]
pub enum BackupTaskOutcome {
    Succeeded { recovery_set_id: Uuid },
    Failed { code: String },
    Interrupted,
}

impl BackupTaskOutcome {
    fn validate(&self) -> Result<(), SchedulerError> {
        match self {
            Self::Succeeded { recovery_set_id } if recovery_set_id.is_nil() => {
                Err(SchedulerError::InvalidInput)
            }
            Self::Failed { code }
                if code.is_empty()
                    || code.len() > 64
                    || !code
                        .bytes()
                        .all(|byte| byte.is_ascii_uppercase() || byte == b'_') =>
            {
                Err(SchedulerError::InvalidInput)
            }
            _ => Ok(()),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct BackupTask {
    pub task_id: Uuid,
    pub scheduled_at_unix: u64,
    pub attempt: u32,
    pub started_at_unix: u64,
    pub completed_at_unix: Option<u64>,
    pub outcome: Option<BackupTaskOutcome>,
}

impl BackupTask {
    fn validate(&self, deployment_id: Uuid) -> Result<(), SchedulerError> {
        if self.task_id != task_id(deployment_id, self.scheduled_at_unix)
            || self.scheduled_at_unix == 0
            || self.attempt == 0
            || self.started_at_unix < self.scheduled_at_unix
        {
            return Err(SchedulerError::Corrupt);
        }
        match (&self.completed_at_unix, &self.outcome) {
            (None, None) => Ok(()),
            (Some(completed), Some(outcome)) if *completed >= self.started_at_unix => {
                outcome.validate().map_err(|_| SchedulerError::Corrupt)
            }
            _ => Err(SchedulerError::Corrupt),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BackupTaskSnapshot {
    pub active: Option<BackupTask>,
    pub pending_scheduled_at_unix: Option<u64>,
    pub recent: Vec<BackupTask>,
    pub revision: u64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
struct PersistedState {
    schema_version: u32,
    deployment_id: Uuid,
    anchor_unix: u64,
    period_seconds: u64,
    revision: u64,
    last_started_schedule_unix: Option<u64>,
    pending_scheduled_at_unix: Option<u64>,
    active: Option<BackupTask>,
    recent: Vec<BackupTask>,
}

impl PersistedState {
    fn new(config: BackupScheduleConfig) -> Self {
        Self {
            schema_version: STATE_SCHEMA_VERSION,
            deployment_id: config.deployment_id,
            anchor_unix: config.anchor_unix,
            period_seconds: config.period_seconds,
            revision: 1,
            last_started_schedule_unix: None,
            pending_scheduled_at_unix: None,
            active: None,
            recent: Vec::new(),
        }
    }

    fn validate(&self, config: BackupScheduleConfig) -> Result<(), SchedulerError> {
        if self.schema_version != STATE_SCHEMA_VERSION
            || self.deployment_id != config.deployment_id
            || self.anchor_unix != config.anchor_unix
            || self.period_seconds != config.period_seconds
            || self.revision == 0
            || self.recent.len() > MAX_HISTORY
        {
            return Err(SchedulerError::Corrupt);
        }
        if let Some(active) = &self.active {
            active.validate(config.deployment_id)?;
            if active.completed_at_unix.is_some() {
                return Err(SchedulerError::Corrupt);
            }
        }
        for task in &self.recent {
            task.validate(config.deployment_id)?;
            if task.completed_at_unix.is_none() {
                return Err(SchedulerError::Corrupt);
            }
        }
        if self
            .pending_scheduled_at_unix
            .is_some_and(|scheduled| scheduled < config.anchor_unix)
            || self
                .last_started_schedule_unix
                .is_some_and(|scheduled| scheduled < config.anchor_unix)
        {
            return Err(SchedulerError::Corrupt);
        }
        Ok(())
    }
}

pub struct BackupTaskStore {
    root: PathBuf,
    config: BackupScheduleConfig,
    state: PersistedState,
    lock: File,
}

impl BackupTaskStore {
    pub fn open(root: &Path, config: BackupScheduleConfig) -> Result<Self, SchedulerError> {
        config.validate()?;
        if !root.is_absolute() {
            return Err(SchedulerError::InvalidInput);
        }
        px_private_files::private::verify_private_directory(root)
            .map_err(|_| SchedulerError::Permission)?;
        let lock = private_lock_file(&root.join("scheduler.lock"))?;
        lock.try_lock_exclusive().map_err(|error| {
            if error.kind() == std::io::ErrorKind::WouldBlock || error.raw_os_error() == Some(33) {
                SchedulerError::Busy
            } else {
                SchedulerError::Unavailable
            }
        })?;
        verify_known_entries(root)?;
        let state = recover_snapshot(root, config)?.unwrap_or_else(|| PersistedState::new(config));
        state.validate(config)?;
        if !root.join("state.json").exists() {
            persist_snapshot(root, &state)?;
        }
        Ok(Self {
            root: root.to_path_buf(),
            config,
            state,
            lock,
        })
    }

    pub fn poll(&mut self, now_unix: u64) -> Result<Option<BackupTask>, SchedulerError> {
        let latest_due = self.config.latest_due(now_unix)?;
        let mut next = self.state.clone();
        if let Some(active) = &next.active {
            if latest_due.is_some_and(|due| due > active.scheduled_at_unix)
                && latest_due > next.pending_scheduled_at_unix
            {
                next.pending_scheduled_at_unix = latest_due;
                self.commit(next)?;
            }
            return Ok(None);
        }
        let scheduled = if let Some(pending) = next.pending_scheduled_at_unix.take() {
            Some(pending)
        } else {
            latest_due.filter(|due| {
                next.last_started_schedule_unix
                    .is_none_or(|last| *due > last)
            })
        };
        let Some(scheduled_at_unix) = scheduled else {
            return Ok(None);
        };
        let task_id = task_id(self.config.deployment_id, scheduled_at_unix);
        let attempts = next
            .recent
            .iter()
            .filter(|task| task.task_id == task_id)
            .count();
        let attempt = u32::try_from(attempts)
            .ok()
            .and_then(|value| value.checked_add(1))
            .ok_or(SchedulerError::Corrupt)?;
        let task = BackupTask {
            task_id,
            scheduled_at_unix,
            attempt,
            started_at_unix: now_unix,
            completed_at_unix: None,
            outcome: None,
        };
        task.validate(self.config.deployment_id)?;
        next.last_started_schedule_unix = Some(
            next.last_started_schedule_unix
                .map_or(scheduled_at_unix, |last| last.max(scheduled_at_unix)),
        );
        next.active = Some(task.clone());
        self.commit(next)?;
        Ok(Some(task))
    }

    pub fn complete(
        &mut self,
        task_id: Uuid,
        completed_at_unix: u64,
        outcome: BackupTaskOutcome,
    ) -> Result<(), SchedulerError> {
        outcome.validate()?;
        let mut next = self.state.clone();
        let mut task = next.active.take().ok_or(SchedulerError::Conflict)?;
        if task.task_id != task_id || completed_at_unix < task.started_at_unix {
            return Err(SchedulerError::Conflict);
        }
        task.completed_at_unix = Some(completed_at_unix);
        task.outcome = Some(outcome);
        task.validate(self.config.deployment_id)?;
        next.recent.push(task);
        if next.recent.len() > MAX_HISTORY {
            next.recent.remove(0);
        }
        self.commit(next)
    }

    pub fn reconcile_after_restart(&mut self, now_unix: u64) -> Result<bool, SchedulerError> {
        let Some(mut interrupted) = self.state.active.clone() else {
            return Ok(false);
        };
        if now_unix < interrupted.started_at_unix {
            return Err(SchedulerError::InvalidInput);
        }
        interrupted.completed_at_unix = Some(now_unix);
        interrupted.outcome = Some(BackupTaskOutcome::Interrupted);
        let mut next = self.state.clone();
        next.active = None;
        next.recent.push(interrupted.clone());
        if next.recent.len() > MAX_HISTORY {
            next.recent.remove(0);
        }
        let latest_due = self.config.latest_due(now_unix)?;
        next.pending_scheduled_at_unix = Some(
            next.pending_scheduled_at_unix
                .into_iter()
                .chain(latest_due)
                .chain([interrupted.scheduled_at_unix])
                .max()
                .ok_or(SchedulerError::Corrupt)?,
        );
        self.commit(next)?;
        Ok(true)
    }

    pub fn snapshot(&self) -> BackupTaskSnapshot {
        BackupTaskSnapshot {
            active: self.state.active.clone(),
            pending_scheduled_at_unix: self.state.pending_scheduled_at_unix,
            recent: self.state.recent.clone(),
            revision: self.state.revision,
        }
    }

    fn commit(&mut self, mut next: PersistedState) -> Result<(), SchedulerError> {
        next.revision = self
            .state
            .revision
            .checked_add(1)
            .ok_or(SchedulerError::Corrupt)?;
        next.validate(self.config)?;
        persist_snapshot(&self.root, &next)?;
        self.state = next;
        Ok(())
    }
}

impl Drop for BackupTaskStore {
    fn drop(&mut self) {
        let _ = FileExt::unlock(&self.lock);
    }
}

fn task_id(deployment_id: Uuid, scheduled_at_unix: u64) -> Uuid {
    let mut hasher = Sha256::new();
    hasher.update(b"pixels-backup-task-v1");
    hasher.update(deployment_id.as_bytes());
    hasher.update(scheduled_at_unix.to_be_bytes());
    let digest = hasher.finalize();
    let mut bytes = [0_u8; 16];
    bytes.copy_from_slice(&digest[..16]);
    bytes[6] = (bytes[6] & 0x0f) | 0x50;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    Uuid::from_bytes(bytes)
}

fn recover_snapshot(
    root: &Path,
    config: BackupScheduleConfig,
) -> Result<Option<PersistedState>, SchedulerError> {
    let current_path = root.join("state.json");
    let previous_path = root.join("state.previous");
    let next_path = root.join("state.next");
    let current = read_state(&current_path, config)?;
    let previous = read_state(&previous_path, config)?;
    let next = read_state(&next_path, config)?;
    match (current, previous, next) {
        (None, None, None) => Ok(None),
        (Some(current), None, None) => Ok(Some(current)),
        (Some(current), Some(_), None) => {
            fs::remove_file(previous_path).map_err(|_| SchedulerError::Unavailable)?;
            sync_directory(root)?;
            Ok(Some(current))
        }
        (Some(current), None, Some(_)) => {
            fs::remove_file(next_path).map_err(|_| SchedulerError::Unavailable)?;
            sync_directory(root)?;
            Ok(Some(current))
        }
        (None, Some(previous), None) => {
            fs::rename(previous_path, current_path).map_err(|_| SchedulerError::Unavailable)?;
            sync_directory(root)?;
            Ok(Some(previous))
        }
        (None, Some(_), Some(next)) => {
            fs::rename(next_path, current_path).map_err(|_| SchedulerError::Unavailable)?;
            fs::remove_file(previous_path).map_err(|_| SchedulerError::Unavailable)?;
            sync_directory(root)?;
            Ok(Some(next))
        }
        (None, None, Some(next)) => {
            fs::rename(next_path, current_path).map_err(|_| SchedulerError::Unavailable)?;
            sync_directory(root)?;
            Ok(Some(next))
        }
        _ => Err(SchedulerError::Corrupt),
    }
}

fn persist_snapshot(root: &Path, state: &PersistedState) -> Result<(), SchedulerError> {
    let current = root.join("state.json");
    let previous = root.join("state.previous");
    let next = root.join("state.next");
    if next.exists() || previous.exists() {
        return Err(SchedulerError::Corrupt);
    }
    let bytes = serde_json::to_vec_pretty(state).map_err(|_| SchedulerError::Corrupt)?;
    if bytes.is_empty() || bytes.len() as u64 > MAX_STATE_BYTES {
        return Err(SchedulerError::Corrupt);
    }
    let mut file = private_new_file(&next)?;
    file.write_all(&bytes)
        .map_err(|_| SchedulerError::Unavailable)?;
    file.sync_all().map_err(|_| SchedulerError::Unavailable)?;
    drop(file);
    if current.exists() {
        fs::rename(&current, &previous).map_err(|_| SchedulerError::Unavailable)?;
    }
    fs::rename(&next, &current).map_err(|_| SchedulerError::Unavailable)?;
    sync_directory(root)?;
    if previous.exists() {
        fs::remove_file(&previous).map_err(|_| SchedulerError::Unavailable)?;
        sync_directory(root)?;
    }
    Ok(())
}

fn verify_known_entries(root: &Path) -> Result<(), SchedulerError> {
    for entry in fs::read_dir(root).map_err(|_| SchedulerError::Unavailable)? {
        let entry = entry.map_err(|_| SchedulerError::Unavailable)?;
        let name = entry.file_name();
        let name = name.to_str().ok_or(SchedulerError::Corrupt)?;
        if !matches!(
            name,
            "scheduler.lock" | "state.json" | "state.previous" | "state.next"
        ) {
            return Err(SchedulerError::Corrupt);
        }
        let kind = entry.file_type().map_err(|_| SchedulerError::Unavailable)?;
        if !kind.is_file() || kind.is_symlink() {
            return Err(SchedulerError::Corrupt);
        }
    }
    Ok(())
}

fn private_lock_file(path: &Path) -> Result<File, SchedulerError> {
    let mut options = OpenOptions::new();
    options.read(true).write(true).create(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
    }
    let file = options
        .open(path)
        .map_err(|_| SchedulerError::Unavailable)?;
    verify_regular_private_file(&file)?;
    Ok(file)
}

fn private_new_file(path: &Path) -> Result<File, SchedulerError> {
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600).custom_flags(libc::O_NOFOLLOW);
    }
    let file = options
        .open(path)
        .map_err(|_| SchedulerError::Unavailable)?;
    verify_regular_private_file(&file)?;
    Ok(file)
}

fn read_state(
    path: &Path,
    config: BackupScheduleConfig,
) -> Result<Option<PersistedState>, SchedulerError> {
    if !path.exists() {
        return Ok(None);
    }
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.custom_flags(libc::O_NOFOLLOW);
    }
    let file = options.open(path).map_err(|_| SchedulerError::Corrupt)?;
    verify_regular_private_file(&file)?;
    let size = file
        .metadata()
        .map_err(|_| SchedulerError::Unavailable)?
        .len();
    if size == 0 || size > MAX_STATE_BYTES {
        return Err(SchedulerError::Corrupt);
    }
    let mut bytes = Vec::with_capacity(size as usize);
    file.take(MAX_STATE_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| SchedulerError::Unavailable)?;
    if bytes.len() as u64 != size {
        return Err(SchedulerError::Corrupt);
    }
    let state =
        serde_json::from_slice::<PersistedState>(&bytes).map_err(|_| SchedulerError::Corrupt)?;
    state.validate(config)?;
    Ok(Some(state))
}

fn verify_regular_private_file(file: &File) -> Result<(), SchedulerError> {
    let metadata = file.metadata().map_err(|_| SchedulerError::Unavailable)?;
    if !metadata.is_file() {
        return Err(SchedulerError::Permission);
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if metadata.permissions().mode() & 0o077 != 0 {
            return Err(SchedulerError::Permission);
        }
    }
    Ok(())
}

fn sync_directory(path: &Path) -> Result<(), SchedulerError> {
    #[cfg(windows)]
    {
        let _ = path;
        Ok(())
    }
    #[cfg(unix)]
    {
        let directory = File::open(path).map_err(|_| SchedulerError::Unavailable)?;
        directory
            .sync_all()
            .map_err(|_| SchedulerError::Unavailable)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct Fixture {
        _base: tempfile::TempDir,
        root: PathBuf,
        config: BackupScheduleConfig,
    }

    impl Fixture {
        fn new() -> Self {
            let base = tempfile::Builder::new()
                .prefix("pixels-backup-scheduler-")
                .tempdir()
                .unwrap();
            make_private(base.path());
            let root = base.path().join("tasks");
            fs::create_dir(&root).unwrap();
            make_private(&root);
            Self {
                _base: base,
                root,
                config: BackupScheduleConfig {
                    deployment_id: Uuid::new_v4(),
                    anchor_unix: 1_000,
                    period_seconds: 60,
                },
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

    #[test]
    fn long_running_task_collapses_missed_periods_to_one_latest_pending_run() {
        let fixture = Fixture::new();
        let mut store = BackupTaskStore::open(&fixture.root, fixture.config).unwrap();
        let first = store.poll(1_000).unwrap().unwrap();
        assert_eq!(first.scheduled_at_unix, 1_000);
        assert_eq!(store.poll(1_245).unwrap(), None);
        assert_eq!(store.snapshot().pending_scheduled_at_unix, Some(1_240));
        store
            .complete(
                first.task_id,
                1_250,
                BackupTaskOutcome::Succeeded {
                    recovery_set_id: Uuid::new_v4(),
                },
            )
            .unwrap();
        let catch_up = store.poll(1_250).unwrap().unwrap();
        assert_eq!(catch_up.scheduled_at_unix, 1_240);
        assert_eq!(store.snapshot().pending_scheduled_at_unix, None);
        assert_eq!(store.poll(1_500).unwrap(), None);
        assert_eq!(store.snapshot().pending_scheduled_at_unix, Some(1_480));
    }

    #[test]
    fn restart_marks_active_attempt_interrupted_and_retries_same_deterministic_task() {
        let fixture = Fixture::new();
        let task = {
            let mut store = BackupTaskStore::open(&fixture.root, fixture.config).unwrap();
            store.poll(1_000).unwrap().unwrap()
        };
        let mut reopened = BackupTaskStore::open(&fixture.root, fixture.config).unwrap();
        assert!(reopened.reconcile_after_restart(1_010).unwrap());
        let retry = reopened.poll(1_010).unwrap().unwrap();
        assert_eq!(retry.task_id, task.task_id);
        assert_eq!(retry.scheduled_at_unix, task.scheduled_at_unix);
        assert_eq!(retry.attempt, 2);
        assert_eq!(
            reopened.snapshot().recent[0].outcome,
            Some(BackupTaskOutcome::Interrupted)
        );
    }

    #[test]
    fn store_is_exclusive_configuration_bound_and_rejects_unknown_entries() {
        let fixture = Fixture::new();
        let store = BackupTaskStore::open(&fixture.root, fixture.config).unwrap();
        assert!(matches!(
            BackupTaskStore::open(&fixture.root, fixture.config),
            Err(SchedulerError::Busy)
        ));
        drop(store);
        let mut wrong = fixture.config;
        wrong.period_seconds = 120;
        assert!(matches!(
            BackupTaskStore::open(&fixture.root, wrong),
            Err(SchedulerError::Corrupt)
        ));
        fs::write(fixture.root.join("unregistered"), b"never remove").unwrap();
        assert!(matches!(
            BackupTaskStore::open(&fixture.root, fixture.config),
            Err(SchedulerError::Corrupt)
        ));
        assert_eq!(
            fs::read(fixture.root.join("unregistered")).unwrap(),
            b"never remove"
        );
    }

    #[test]
    fn snapshot_recovery_finishes_or_rolls_back_only_known_atomic_write_stages() {
        let fixture = Fixture::new();
        {
            let mut store = BackupTaskStore::open(&fixture.root, fixture.config).unwrap();
            store.poll(1_000).unwrap().unwrap();
        }
        let current = fixture.root.join("state.json");
        let previous = fixture.root.join("state.previous");
        let next = fixture.root.join("state.next");

        fs::copy(&current, &next).unwrap();
        make_private_file(&next);
        {
            let store = BackupTaskStore::open(&fixture.root, fixture.config).unwrap();
            assert!(store.snapshot().active.is_some());
        }
        assert!(!next.exists());

        fs::copy(&current, &next).unwrap();
        make_private_file(&next);
        fs::rename(&current, &previous).unwrap();
        {
            let store = BackupTaskStore::open(&fixture.root, fixture.config).unwrap();
            assert!(store.snapshot().active.is_some());
        }
        assert!(current.is_file());
        assert!(!previous.exists());
        assert!(!next.exists());
    }

    fn make_private_file(path: &Path) {
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(path, fs::Permissions::from_mode(0o600)).unwrap();
        }
        #[cfg(windows)]
        {
            let _ = path;
        }
    }
}
