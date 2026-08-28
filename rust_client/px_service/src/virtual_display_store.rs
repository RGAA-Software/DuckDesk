use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

pub const VIRTUAL_DISPLAY_STATE_FILE: &str = "virtual_displays.json";

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct OwnedVirtualDisplay {
    pub logical_id: String,
    pub width: u32,
    pub height: u32,
    pub refresh_hz: u32,
    /// Diagnostic only. Windows may assign a different DISPLAYn after reboot.
    pub observed_device_name: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct PersistedVirtualDisplayState {
    pub schema_version: u32,
    pub initialized: bool,
    pub desired_count: u32,
    pub owned_slots: Vec<OwnedVirtualDisplay>,
    /// Number of Parsec VDD displays that existed before GammaRay added its first
    /// display. Those displays are never removed by GammaRay.
    pub foreign_baseline: u32,
    pub topology_generation: u64,
    pub last_known_total: u32,
    /// Cleared if the Parsec VDD count changes outside GammaRay. Parsec VDD removal
    /// is LIFO, so deleting while this is false could remove another product's
    /// display.
    pub removal_safe: bool,
    pub last_error: Option<String>,
}

impl Default for PersistedVirtualDisplayState {
    fn default() -> Self {
        Self {
            schema_version: 2,
            initialized: false,
            desired_count: 0,
            owned_slots: Vec::new(),
            foreign_baseline: 0,
            topology_generation: 0,
            last_known_total: 0,
            removal_safe: true,
            last_error: None,
        }
    }
}

#[derive(Debug, Clone)]
pub struct VirtualDisplayStore {
    file_path: PathBuf,
}

impl VirtualDisplayStore {
    pub fn new(file_path: PathBuf) -> Self {
        Self { file_path }
    }

    pub fn file_path(&self) -> &Path {
        &self.file_path
    }

    pub fn load(&self) -> io::Result<PersistedVirtualDisplayState> {
        match fs::read_to_string(&self.file_path) {
            Ok(content) => {
                let state: PersistedVirtualDisplayState = serde_json::from_str(&content)
                    .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;
                if state.schema_version < 2 {
                    Ok(PersistedVirtualDisplayState::default())
                } else {
                    Ok(state)
                }
            }
            Err(err) if err.kind() == io::ErrorKind::NotFound => {
                Ok(PersistedVirtualDisplayState::default())
            }
            Err(err) => Err(err),
        }
    }

    pub fn save(&self, state: &PersistedVirtualDisplayState) -> io::Result<()> {
        if let Some(parent) = self.file_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let content = serde_json::to_vec_pretty(state)
            .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;
        let temp_path = self.file_path.with_extension("json.tmp");
        fs::write(&temp_path, content)?;
        if self.file_path.exists() {
            let backup_path = self.file_path.with_extension("json.bak");
            let _ = fs::remove_file(&backup_path);
            fs::rename(&self.file_path, &backup_path)?;
            if let Err(err) = fs::rename(&temp_path, &self.file_path) {
                let _ = fs::rename(&backup_path, &self.file_path);
                return Err(err);
            }
            let _ = fs::remove_file(backup_path);
        } else {
            fs::rename(temp_path, &self.file_path)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn unique_file(name: &str) -> PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        std::env::temp_dir().join(format!("{name}_{stamp}.json"))
    }

    #[test]
    fn missing_store_loads_default() {
        let store = VirtualDisplayStore::new(unique_file("virtual_display_missing"));
        assert_eq!(
            store.load().unwrap(),
            PersistedVirtualDisplayState::default()
        );
    }

    #[test]
    fn legacy_usbmmidd_state_is_reset_for_parsec_vdd() {
        let path = unique_file("virtual_display_legacy_usbmmidd");
        fs::write(
            &path,
            r#"{"schema_version":1,"initialized":true,"desired_count":1,"owned_slots":[{"logical_id":"usbmmidd-slot-1","width":1920,"height":1080,"refresh_hz":60,"observed_device_name":"\\\\.\\DISPLAY9"}]}"#,
        )
        .unwrap();
        let store = VirtualDisplayStore::new(path.clone());
        assert_eq!(
            store.load().unwrap(),
            PersistedVirtualDisplayState::default()
        );
        let _ = fs::remove_file(path);
    }

    #[test]
    fn store_round_trip_is_atomic() {
        let path = unique_file("virtual_display_round_trip");
        let store = VirtualDisplayStore::new(path.clone());
        let state = PersistedVirtualDisplayState {
            initialized: true,
            desired_count: 1,
            owned_slots: vec![OwnedVirtualDisplay {
                logical_id: "parsec-vdd-slot-1".to_string(),
                width: 1920,
                height: 1080,
                refresh_hz: 60,
                observed_device_name: r"\\.\DISPLAY9".to_string(),
            }],
            last_known_total: 1,
            ..Default::default()
        };
        store.save(&state).unwrap();
        assert_eq!(store.load().unwrap(), state);
        let _ = fs::remove_file(path);
    }
}
