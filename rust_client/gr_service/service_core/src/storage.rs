use serde::{Deserialize, Serialize};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use crate::state::RenderLaunchSpec;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct PersistedServiceState {
    pub desktop_launch: Option<PersistedRenderLaunchSpec>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct PersistedRenderLaunchSpec {
    pub work_dir: String,
    pub app_path: String,
    pub args: Vec<String>,
}

impl From<RenderLaunchSpec> for PersistedRenderLaunchSpec {
    fn from(value: RenderLaunchSpec) -> Self {
        Self {
            work_dir: value.work_dir,
            app_path: value.app_path,
            args: value.args,
        }
    }
}

impl From<PersistedRenderLaunchSpec> for RenderLaunchSpec {
    fn from(value: PersistedRenderLaunchSpec) -> Self {
        Self {
            work_dir: value.work_dir,
            app_path: value.app_path,
            args: value.args,
        }
    }
}

#[derive(Debug, Clone)]
pub struct ServiceStorage {
    file_path: PathBuf,
}

impl ServiceStorage {
    pub fn new(file_path: PathBuf) -> Self {
        Self { file_path }
    }

    pub fn file_path(&self) -> &Path {
        &self.file_path
    }

    pub fn load(&self) -> io::Result<PersistedServiceState> {
        match fs::read_to_string(&self.file_path) {
            Ok(content) => serde_json::from_str(&content)
                .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err)),
            Err(err) if err.kind() == io::ErrorKind::NotFound => {
                Ok(PersistedServiceState::default())
            }
            Err(err) => Err(err),
        }
    }

    pub fn save(&self, state: &PersistedServiceState) -> io::Result<()> {
        if let Some(parent) = self.file_path.parent() {
            fs::create_dir_all(parent)?;
        }
        let content = serde_json::to_string_pretty(state)
            .map_err(|err| io::Error::new(io::ErrorKind::InvalidData, err))?;
        fs::write(&self.file_path, content)
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
        std::env::temp_dir().join(format!("{}_{}.json", name, stamp))
    }

    #[test]
    fn load_missing_file_returns_default() {
        let storage = ServiceStorage::new(unique_file("missing"));
        let state = storage.load().unwrap();
        assert_eq!(state, PersistedServiceState::default());
    }

    #[test]
    fn save_and_load_round_trip() {
        let path = unique_file("save_load");
        let storage = ServiceStorage::new(path.clone());
        let state = PersistedServiceState {
            desktop_launch: Some(PersistedRenderLaunchSpec {
                work_dir: "D:/app".to_string(),
                app_path: "D:/app/GammaRayRender.exe".to_string(),
                args: vec!["--app_mode=desktop".to_string()],
            }),
        };
        storage.save(&state).unwrap();
        let loaded = storage.load().unwrap();
        assert_eq!(loaded, state);
        let _ = fs::remove_file(path);
    }
}
