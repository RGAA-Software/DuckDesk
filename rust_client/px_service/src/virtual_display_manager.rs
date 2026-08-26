use std::path::PathBuf;
use std::sync::{Arc, Mutex, MutexGuard};

use serde::Serialize;

#[cfg(test)]
use crate::usbmmidd::WindowsUsbMmIddBackend;
use crate::usbmmidd::{MonitorSnapshot, UsbMmIddBackend, UsbMmIddError};
use crate::virtual_display_session::SessionUsbMmIddBackend;
use crate::virtual_display_store::{
    OwnedVirtualDisplay, PersistedVirtualDisplayState, VirtualDisplayStore,
    VIRTUAL_DISPLAY_STATE_FILE,
};
use crate::windows_process::ProcessManager;

pub const GAMMARAY_VIRTUAL_DISPLAY_LIMIT: usize = 2;
pub const DEFAULT_WIDTH: u32 = 1920;
pub const DEFAULT_HEIGHT: u32 = 1080;
pub const DEFAULT_REFRESH_HZ: u32 = 60;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub enum VirtualDisplayPhase {
    NoDriver,
    Ready,
    Creating,
    Removing,
    Reconciling,
    Faulted,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct VirtualDisplayStatus {
    pub phase: VirtualDisplayPhase,
    pub driver_installed: bool,
    pub package_valid: bool,
    pub topology_generation: u64,
    pub desired_count: u32,
    pub owned_slots: Vec<OwnedVirtualDisplay>,
    pub monitors: Vec<MonitorSnapshot>,
    pub foreign_baseline: u32,
    pub removal_safe: bool,
    pub last_error: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct VirtualDisplayOperationResult {
    pub topology_changed: bool,
    pub logical_display_id: Option<String>,
    pub status: VirtualDisplayStatus,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VirtualDisplayError {
    pub code: String,
    pub message: String,
}

impl VirtualDisplayError {
    fn new(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self {
            code: code.into(),
            message: message.into(),
        }
    }
}

impl std::fmt::Display for VirtualDisplayError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for VirtualDisplayError {}

impl From<UsbMmIddError> for VirtualDisplayError {
    fn from(value: UsbMmIddError) -> Self {
        Self::new(value.code, value.message)
    }
}

struct ManagerState {
    phase: VirtualDisplayPhase,
    persisted: PersistedVirtualDisplayState,
}

pub struct VirtualDisplayManager {
    backend: Arc<dyn UsbMmIddBackend>,
    store: VirtualDisplayStore,
    state: Mutex<ManagerState>,
}

impl VirtualDisplayManager {
    pub fn new(
        backend: Arc<dyn UsbMmIddBackend>,
        store: VirtualDisplayStore,
    ) -> Result<Self, VirtualDisplayError> {
        let persisted = store.load().map_err(|err| {
            VirtualDisplayError::new(
                "STATE_LOAD_FAILED",
                format!("cannot load {}: {err}", store.file_path().display()),
            )
        })?;
        Ok(Self {
            backend,
            store,
            state: Mutex::new(ManagerState {
                phase: VirtualDisplayPhase::Reconciling,
                persisted,
            }),
        })
    }

    #[cfg(test)]
    pub fn new_windows(
        data_root: PathBuf,
        driver_dir: PathBuf,
    ) -> Result<Self, VirtualDisplayError> {
        Self::new(
            Arc::new(WindowsUsbMmIddBackend::new(driver_dir)),
            VirtualDisplayStore::new(data_root.join(VIRTUAL_DISPLAY_STATE_FILE)),
        )
    }

    pub fn new_windows_session_aware(
        data_root: PathBuf,
        driver_dir: PathBuf,
        process_manager: Arc<dyn ProcessManager>,
    ) -> Result<Self, VirtualDisplayError> {
        let backend = SessionUsbMmIddBackend::new(
            driver_dir,
            data_root.join("virtual_display_worker"),
            process_manager,
        )
        .map_err(VirtualDisplayError::from)?;
        Self::new(
            Arc::new(backend),
            VirtualDisplayStore::new(data_root.join(VIRTUAL_DISPLAY_STATE_FILE)),
        )
    }

    pub fn query(&self) -> Result<VirtualDisplayOperationResult, VirtualDisplayError> {
        let mut state = self.lock_state()?;
        let monitors = self.reconcile_locked(&mut state)?;
        Ok(VirtualDisplayOperationResult {
            topology_changed: false,
            logical_display_id: None,
            status: self.status_locked(&state, monitors),
        })
    }

    pub fn create(
        &self,
        width: u32,
        height: u32,
        refresh_hz: u32,
    ) -> Result<VirtualDisplayOperationResult, VirtualDisplayError> {
        validate_mode(width, height, refresh_hz)?;
        let mut state = self.lock_state()?;
        let before = self.reconcile_locked(&mut state)?;
        self.ensure_mutation_safe(&state)?;
        if state.persisted.owned_slots.len() >= GAMMARAY_VIRTUAL_DISPLAY_LIMIT {
            return Err(VirtualDisplayError::new(
                "VIRTUAL_DISPLAY_LIMIT_REACHED",
                format!(
                    "GammaRay already owns {} virtual displays (limit {})",
                    state.persisted.owned_slots.len(),
                    GAMMARAY_VIRTUAL_DISPLAY_LIMIT
                ),
            ));
        }

        state.phase = VirtualDisplayPhase::Creating;
        let created = match self.backend.add_monitor(width, height, refresh_hz) {
            Ok(created) => created,
            Err(err) => {
                // The interactive worker can lose its response after the
                // driver already accepted the add IOCTL. Adopt that single,
                // mode-matching topology addition so ownership does not remain
                // at zero while a GammaRay-created monitor is live.
                let observed = self.backend.enumerate_monitors().map_err(|query_err| {
                    self.record_fault(
                        &mut state,
                        format!("{err}; post-create reconciliation failed: {query_err}"),
                    );
                    VirtualDisplayError::from(query_err)
                })?;
                let additions: Vec<_> = observed
                    .iter()
                    .filter(|monitor| {
                        !before
                            .iter()
                            .any(|existing| existing.device_name == monitor.device_name)
                            && monitor.width == width
                            && monitor.height == height
                            && monitor.refresh_hz == refresh_hz
                    })
                    .cloned()
                    .collect();
                if observed.len() != before.len() + 1 || additions.len() != 1 {
                    self.record_fault(&mut state, err.to_string());
                    return Err(err.into());
                }
                additions
                    .into_iter()
                    .next()
                    .expect("one addition was verified")
            }
        };
        let logical_id = format!("usbmmidd-slot-{}", state.persisted.owned_slots.len() + 1);
        let previous = state.persisted.clone();
        state.persisted.owned_slots.push(OwnedVirtualDisplay {
            logical_id: logical_id.clone(),
            width,
            height,
            refresh_hz,
            observed_device_name: created.device_name,
        });
        state.persisted.desired_count = state.persisted.owned_slots.len() as u32;
        state.persisted.topology_generation += 1;
        state.persisted.last_known_total = before.len() as u32 + 1;
        state.persisted.last_error = None;
        state.phase = VirtualDisplayPhase::Ready;
        if let Err(err) = self.save_locked(&state) {
            let rollback = self.backend.remove_last_monitor();
            state.persisted = previous;
            self.record_fault(
                &mut state,
                format!("{err}; hardware rollback result: {rollback:?}"),
            );
            return Err(err);
        }
        let monitors = self.backend.enumerate_monitors()?;
        Ok(VirtualDisplayOperationResult {
            topology_changed: true,
            logical_display_id: Some(logical_id),
            status: self.status_locked(&state, monitors),
        })
    }

    pub fn remove_last(&self) -> Result<VirtualDisplayOperationResult, VirtualDisplayError> {
        let mut state = self.lock_state()?;
        let before = self.reconcile_locked(&mut state)?;
        self.ensure_mutation_safe(&state)?;
        let removed = state.persisted.owned_slots.last().cloned().ok_or_else(|| {
            VirtualDisplayError::new(
                "NO_OWNED_VIRTUAL_DISPLAY",
                "GammaRay has no owned virtual display to remove",
            )
        })?;
        let expected =
            state.persisted.foreign_baseline as usize + state.persisted.owned_slots.len();
        if before.len() != expected {
            self.record_fault(
                &mut state,
                format!(
                    "refusing LIFO removal: expected {expected} USBMMIDD monitors, observed {}",
                    before.len()
                ),
            );
            return Err(VirtualDisplayError::new(
                "OWNERSHIP_CONFLICT",
                state.persisted.last_error.clone().unwrap_or_default(),
            ));
        }

        state.phase = VirtualDisplayPhase::Removing;
        if let Err(err) = self.backend.remove_last_monitor() {
            // Some driver/interactive-worker failures are reported after the
            // remove IOCTL has already changed the topology. Reconcile that
            // irreversible side effect instead of preserving a stale owned
            // slot that would block every subsequent operation.
            let observed = self.backend.enumerate_monitors().map_err(|query_err| {
                self.record_fault(
                    &mut state,
                    format!("{err}; post-remove reconciliation failed: {query_err}"),
                );
                VirtualDisplayError::from(query_err)
            })?;
            if observed.len().saturating_add(1) != before.len() {
                self.record_fault(&mut state, err.to_string());
                return Err(err.into());
            }
        }
        state.persisted.owned_slots.pop();
        state.persisted.desired_count = state.persisted.owned_slots.len() as u32;
        state.persisted.topology_generation += 1;
        state.persisted.last_known_total = before.len().saturating_sub(1) as u32;
        state.persisted.last_error = None;
        state.phase = if self.backend.driver_installed() {
            VirtualDisplayPhase::Ready
        } else {
            VirtualDisplayPhase::NoDriver
        };
        self.save_locked(&state)?;
        let monitors = self.backend.enumerate_monitors()?;
        Ok(VirtualDisplayOperationResult {
            topology_changed: true,
            logical_display_id: Some(removed.logical_id),
            status: self.status_locked(&state, monitors),
        })
    }

    pub fn reset_owned(&self) -> Result<VirtualDisplayOperationResult, VirtualDisplayError> {
        let mut changed = false;
        let mut last_id = None;
        loop {
            let count = self.lock_state()?.persisted.owned_slots.len();
            if count == 0 {
                break;
            }
            let result = self.remove_last()?;
            changed |= result.topology_changed;
            last_id = result.logical_display_id;
        }
        let mut result = self.query()?;
        result.topology_changed = changed;
        result.logical_display_id = last_id;
        Ok(result)
    }

    fn lock_state(&self) -> Result<MutexGuard<'_, ManagerState>, VirtualDisplayError> {
        self.state.lock().map_err(|_| {
            VirtualDisplayError::new("STATE_LOCK_POISONED", "virtual display state lock poisoned")
        })
    }

    fn reconcile_locked(
        &self,
        state: &mut ManagerState,
    ) -> Result<Vec<MonitorSnapshot>, VirtualDisplayError> {
        state.phase = VirtualDisplayPhase::Reconciling;
        let monitors = self.backend.enumerate_monitors()?;
        let actual = monitors.len() as u32;
        if !state.persisted.initialized {
            state.persisted.initialized = true;
            state.persisted.foreign_baseline = actual;
            state.persisted.last_known_total = actual;
            state.persisted.removal_safe = true;
            state.persisted.last_error = None;
            state.phase = if self.backend.driver_installed() {
                VirtualDisplayPhase::Ready
            } else {
                VirtualDisplayPhase::NoDriver
            };
            self.save_locked(state)?;
            return Ok(monitors);
        }

        let expected = state.persisted.foreign_baseline + state.persisted.owned_slots.len() as u32;
        if state.persisted.owned_slots.is_empty() {
            // With no GammaRay-owned display, every observed monitor is foreign
            // and rebasing is always safe.
            state.persisted.foreign_baseline = actual;
            state.persisted.last_known_total = actual;
            state.persisted.removal_safe = true;
            state.persisted.last_error = None;
        } else if actual == state.persisted.foreign_baseline {
            // All GammaRay-owned monitors are already absent. This can happen
            // when the driver completed an operation but the worker response
            // was lost, or after a process/service interruption. Clearing the
            // stale ownership is safe because the observed topology is exactly
            // the foreign baseline and no removal is attempted here.
            state.persisted.owned_slots.clear();
            state.persisted.desired_count = 0;
            state.persisted.topology_generation += 1;
            state.persisted.last_known_total = actual;
            state.persisted.removal_safe = true;
            state.persisted.last_error = None;
        } else if actual != expected {
            state.persisted.removal_safe = false;
            state.persisted.last_known_total = actual;
            state.persisted.last_error = Some(format!(
                "USBMMIDD topology changed outside GammaRay: expected {expected}, observed {actual}"
            ));
        }
        state.phase = if state.persisted.removal_safe {
            if self.backend.driver_installed() {
                VirtualDisplayPhase::Ready
            } else {
                VirtualDisplayPhase::NoDriver
            }
        } else {
            VirtualDisplayPhase::Faulted
        };
        self.save_locked(state)?;
        Ok(monitors)
    }

    fn ensure_mutation_safe(&self, state: &ManagerState) -> Result<(), VirtualDisplayError> {
        if !state.persisted.removal_safe {
            return Err(VirtualDisplayError::new(
                "OWNERSHIP_CONFLICT",
                state
                    .persisted
                    .last_error
                    .clone()
                    .unwrap_or_else(|| "USBMMIDD ownership is uncertain".to_string()),
            ));
        }
        Ok(())
    }

    fn save_locked(&self, state: &ManagerState) -> Result<(), VirtualDisplayError> {
        self.store.save(&state.persisted).map_err(|err| {
            VirtualDisplayError::new(
                "STATE_SAVE_FAILED",
                format!("cannot save {}: {err}", self.store.file_path().display()),
            )
        })
    }

    fn record_fault(&self, state: &mut ManagerState, message: String) {
        state.phase = VirtualDisplayPhase::Faulted;
        state.persisted.last_error = Some(message);
        let _ = self.store.save(&state.persisted);
    }

    fn status_locked(
        &self,
        state: &ManagerState,
        monitors: Vec<MonitorSnapshot>,
    ) -> VirtualDisplayStatus {
        VirtualDisplayStatus {
            phase: state.phase,
            driver_installed: self.backend.driver_installed(),
            package_valid: self.backend.verify_package().is_ok(),
            topology_generation: state.persisted.topology_generation,
            desired_count: state.persisted.desired_count,
            owned_slots: state.persisted.owned_slots.clone(),
            monitors,
            foreign_baseline: state.persisted.foreign_baseline,
            removal_safe: state.persisted.removal_safe,
            last_error: state.persisted.last_error.clone(),
        }
    }
}

fn validate_mode(width: u32, height: u32, refresh_hz: u32) -> Result<(), VirtualDisplayError> {
    if (width, height, refresh_hz) != (DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ) {
        return Err(VirtualDisplayError::new(
            "UNSUPPORTED_DISPLAY_MODE",
            format!(
                "phase one supports {}x{}@{} only, requested {}x{}@{}",
                DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ, width, height, refresh_hz
            ),
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex as StdMutex;
    use std::time::{SystemTime, UNIX_EPOCH};

    struct FakeBackend {
        monitors: StdMutex<Vec<MonitorSnapshot>>,
        installed: StdMutex<bool>,
        fail_after_add: StdMutex<bool>,
        fail_after_remove: StdMutex<bool>,
    }

    impl FakeBackend {
        fn new(foreign_count: usize) -> Self {
            let monitors = (0..foreign_count)
                .map(|index| MonitorSnapshot {
                    device_name: format!(r"\\.\DISPLAY{}", index + 1),
                    width: DEFAULT_WIDTH,
                    height: DEFAULT_HEIGHT,
                    refresh_hz: DEFAULT_REFRESH_HZ,
                })
                .collect();
            Self {
                monitors: StdMutex::new(monitors),
                installed: StdMutex::new(foreign_count > 0),
                fail_after_add: StdMutex::new(false),
                fail_after_remove: StdMutex::new(false),
            }
        }

        fn add_external(&self) {
            let mut monitors = self.monitors.lock().unwrap();
            let index = monitors.len() + 1;
            monitors.push(MonitorSnapshot {
                device_name: format!(r"\\.\DISPLAY{index}"),
                width: DEFAULT_WIDTH,
                height: DEFAULT_HEIGHT,
                refresh_hz: DEFAULT_REFRESH_HZ,
            });
        }

        fn remove_external(&self) {
            self.monitors.lock().unwrap().pop();
        }

        fn fail_next_remove_after_side_effect(&self) {
            *self.fail_after_remove.lock().unwrap() = true;
        }

        fn fail_next_add_after_side_effect(&self) {
            *self.fail_after_add.lock().unwrap() = true;
        }
    }

    impl UsbMmIddBackend for FakeBackend {
        fn verify_package(&self) -> Result<(), UsbMmIddError> {
            Ok(())
        }

        fn driver_installed(&self) -> bool {
            *self.installed.lock().unwrap()
        }

        fn install_driver(&self) -> Result<(), UsbMmIddError> {
            *self.installed.lock().unwrap() = true;
            Ok(())
        }

        fn enumerate_monitors(&self) -> Result<Vec<MonitorSnapshot>, UsbMmIddError> {
            Ok(self.monitors.lock().unwrap().clone())
        }

        fn add_monitor(
            &self,
            width: u32,
            height: u32,
            refresh_hz: u32,
        ) -> Result<MonitorSnapshot, UsbMmIddError> {
            self.install_driver()?;
            let mut monitors = self.monitors.lock().unwrap();
            let monitor = MonitorSnapshot {
                device_name: format!(r"\\.\DISPLAY{}", monitors.len() + 1),
                width,
                height,
                refresh_hz,
            };
            monitors.push(monitor.clone());
            if std::mem::take(&mut *self.fail_after_add.lock().unwrap()) {
                return Err(UsbMmIddError::new(
                    "WORKER_RESPONSE_LOST",
                    "monitor was added before the worker response was lost",
                ));
            }
            Ok(monitor)
        }

        fn remove_last_monitor(&self) -> Result<(), UsbMmIddError> {
            self.monitors.lock().unwrap().pop().ok_or_else(|| {
                UsbMmIddError::new("NO_VIRTUAL_DISPLAY", "no fake monitor to remove")
            })?;
            if std::mem::take(&mut *self.fail_after_remove.lock().unwrap()) {
                return Err(UsbMmIddError::new(
                    "WORKER_RESPONSE_LOST",
                    "monitor was removed before the worker response was lost",
                ));
            }
            Ok(())
        }
    }

    fn unique_store(name: &str) -> VirtualDisplayStore {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        VirtualDisplayStore::new(std::env::temp_dir().join(format!("{name}_{stamp}.json")))
    }

    #[test]
    fn adopts_existing_monitors_as_foreign() {
        let backend = Arc::new(FakeBackend::new(2));
        let manager = VirtualDisplayManager::new(backend, unique_store("adopt_foreign")).unwrap();
        let result = manager.query().unwrap();
        assert_eq!(result.status.foreign_baseline, 2);
        assert!(result.status.owned_slots.is_empty());
    }

    #[test]
    fn creates_two_and_removes_only_last_owned_monitor() {
        let backend = Arc::new(FakeBackend::new(1));
        let manager = VirtualDisplayManager::new(backend, unique_store("create_remove")).unwrap();
        manager.query().unwrap();
        let first = manager
            .create(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ)
            .unwrap();
        assert_eq!(first.logical_display_id.as_deref(), Some("usbmmidd-slot-1"));
        let second = manager
            .create(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ)
            .unwrap();
        assert_eq!(
            second.logical_display_id.as_deref(),
            Some("usbmmidd-slot-2")
        );
        let limit = manager
            .create(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ)
            .unwrap_err();
        assert_eq!(limit.code, "VIRTUAL_DISPLAY_LIMIT_REACHED");
        let removed = manager.remove_last().unwrap();
        assert_eq!(
            removed.logical_display_id.as_deref(),
            Some("usbmmidd-slot-2")
        );
        assert_eq!(removed.status.monitors.len(), 2);
    }

    #[test]
    fn restart_reconciles_unchanged_owned_state() {
        let backend = Arc::new(FakeBackend::new(0));
        let store = unique_store("restart_reconcile");
        let path = store.file_path().to_path_buf();
        let manager = VirtualDisplayManager::new(backend.clone(), store).unwrap();
        manager.query().unwrap();
        manager
            .create(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ)
            .unwrap();
        drop(manager);
        let restarted =
            VirtualDisplayManager::new(backend, VirtualDisplayStore::new(path.clone())).unwrap();
        let status = restarted.query().unwrap().status;
        assert_eq!(status.owned_slots.len(), 1);
        assert!(status.removal_safe);
        let _ = std::fs::remove_file(path);
    }

    #[test]
    fn remove_commits_when_the_driver_changed_topology_before_reporting_an_error() {
        let backend = Arc::new(FakeBackend::new(0));
        let manager = VirtualDisplayManager::new(
            backend.clone(),
            unique_store("remove_side_effect_reconcile"),
        )
        .unwrap();
        manager.query().unwrap();
        manager
            .create(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ)
            .unwrap();
        backend.fail_next_remove_after_side_effect();

        let removed = manager.remove_last().unwrap();

        assert!(removed.status.owned_slots.is_empty());
        assert!(removed.status.removal_safe);
        assert!(removed.status.monitors.is_empty());
    }

    #[test]
    fn create_commits_when_the_driver_changed_topology_before_reporting_an_error() {
        let backend = Arc::new(FakeBackend::new(1));
        let manager = VirtualDisplayManager::new(
            backend.clone(),
            unique_store("create_side_effect_reconcile"),
        )
        .unwrap();
        manager.query().unwrap();
        backend.fail_next_add_after_side_effect();

        let created = manager
            .create(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ)
            .unwrap();

        assert_eq!(
            created.logical_display_id.as_deref(),
            Some("usbmmidd-slot-1")
        );
        assert_eq!(created.status.owned_slots.len(), 1);
        assert!(created.status.removal_safe);
        assert_eq!(created.status.monitors.len(), 2);
    }

    #[test]
    fn query_clears_stale_ownership_when_all_owned_monitors_are_already_absent() {
        let backend = Arc::new(FakeBackend::new(0));
        let manager =
            VirtualDisplayManager::new(backend.clone(), unique_store("missing_owned_reconcile"))
                .unwrap();
        manager.query().unwrap();
        let created = manager
            .create(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ)
            .unwrap();
        backend.remove_external();

        let reconciled = manager.query().unwrap();

        assert!(reconciled.status.owned_slots.is_empty());
        assert!(reconciled.status.removal_safe);
        assert_eq!(
            reconciled.status.topology_generation,
            created.status.topology_generation + 1
        );
    }

    #[test]
    fn external_topology_change_blocks_lifo_removal() {
        let backend = Arc::new(FakeBackend::new(0));
        let manager =
            VirtualDisplayManager::new(backend.clone(), unique_store("external_change")).unwrap();
        manager.query().unwrap();
        manager
            .create(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_REFRESH_HZ)
            .unwrap();
        backend.add_external();
        let error = manager.remove_last().unwrap_err();
        assert_eq!(error.code, "OWNERSHIP_CONFLICT");
        assert_eq!(backend.enumerate_monitors().unwrap().len(), 2);
    }

    #[test]
    fn rejects_non_phase_one_mode() {
        let backend = Arc::new(FakeBackend::new(0));
        let manager = VirtualDisplayManager::new(backend, unique_store("mode")).unwrap();
        let error = manager.create(2560, 1440, 60).unwrap_err();
        assert_eq!(error.code, "UNSUPPORTED_DISPLAY_MODE");
    }
}
