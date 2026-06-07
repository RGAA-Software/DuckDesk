use std::path::Path;
use std::process::Command;

use tracing::info;

use crate::config::GUARD_TASK_NAME;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LogonTaskSpec {
    pub task_name: String,
    pub exe_path: String,
    pub author: String,
}

impl LogonTaskSpec {
    pub fn new(task_name: &str, exe_path: &Path, author: &str) -> Self {
        Self {
            task_name: task_name.to_string(),
            exe_path: exe_path.to_string_lossy().to_string(),
            author: author.to_string(),
        }
    }

    pub fn tr_argument(&self) -> String {
        quote_task_path(&self.exe_path)
    }

    pub fn create_args(&self) -> Vec<String> {
        vec![
            "/Create".to_string(),
            "/F".to_string(),
            "/SC".to_string(),
            "ONLOGON".to_string(),
            "/RL".to_string(),
            "HIGHEST".to_string(),
            "/IT".to_string(),
            "/TN".to_string(),
            self.task_name.clone(),
            "/TR".to_string(),
            self.tr_argument(),
        ]
    }
}

pub trait TaskSchedulerBackend: Send + Sync + 'static {
    fn create_or_update(&self, spec: &LogonTaskSpec) -> Result<(), String>;
}

#[derive(Default)]
pub struct SchtasksBackend;

impl TaskSchedulerBackend for SchtasksBackend {
    fn create_or_update(&self, spec: &LogonTaskSpec) -> Result<(), String> {
        let status = Command::new("schtasks")
            .args(spec.create_args())
            .status()
            .map_err(|err| format!("failed to launch schtasks: {err}"))?;
        if !status.success() {
            return Err(format!(
                "schtasks create failed for {}: {}",
                spec.task_name, status
            ));
        }
        Ok(())
    }
}

pub fn ensure_guard_logon_task(
    backend: &dyn TaskSchedulerBackend,
    exe_path: &Path,
    author: &str,
) -> Result<(), String> {
    let spec = LogonTaskSpec::new(GUARD_TASK_NAME, exe_path, author);
    info!(
        "ensuring guard logon task, task_name={}, exe_path={}",
        spec.task_name, spec.exe_path
    );
    backend.create_or_update(&spec)
}

fn quote_task_path(path: &str) -> String {
    let trimmed = path.trim_matches('"');
    format!("\"{trimmed}\"")
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::{Arc, Mutex};

    #[derive(Default)]
    struct RecordingBackend {
        specs: Arc<Mutex<Vec<LogonTaskSpec>>>,
    }

    impl TaskSchedulerBackend for RecordingBackend {
        fn create_or_update(&self, spec: &LogonTaskSpec) -> Result<(), String> {
            self.specs.lock().expect("lock").push(spec.clone());
            Ok(())
        }
    }

    #[test]
    fn task_spec_preserves_task_name_and_author() {
        let spec = LogonTaskSpec::new("GammaRay_Guard_Start", Path::new("D:/GammaRay/GammaRayGuard.exe"), "GR");
        assert_eq!(spec.task_name, "GammaRay_Guard_Start");
        assert_eq!(spec.author, "GR");
    }

    #[test]
    fn task_spec_quotes_executable_path() {
        let spec = LogonTaskSpec::new(
            "GammaRay_Guard_Start",
            Path::new("D:/GammaRay Premium/GammaRayGuard.exe"),
            "GR",
        );
        assert_eq!(
            spec.tr_argument(),
            "\"D:/GammaRay Premium/GammaRayGuard.exe\""
        );
    }

    #[test]
    fn create_args_use_onlogon_highest_and_interactive() {
        let spec = LogonTaskSpec::new("GammaRay_Guard_Start", Path::new("D:/GammaRay/GammaRayGuard.exe"), "GR");
        let args = spec.create_args();
        assert!(args.windows(2).any(|w| w == ["/SC", "ONLOGON"]));
        assert!(args.windows(2).any(|w| w == ["/RL", "HIGHEST"]));
        assert!(args.iter().any(|arg| arg == "/IT"));
    }

    #[test]
    fn ensure_guard_logon_task_uses_default_task_name() {
        let backend = RecordingBackend::default();
        ensure_guard_logon_task(
            &backend,
            Path::new("D:/GammaRay/GammaRayGuard.exe"),
            "GR",
        )
        .expect("ensure");
        let specs = backend.specs.lock().expect("lock");
        assert_eq!(specs.len(), 1);
        assert_eq!(specs[0].task_name, GUARD_TASK_NAME);
    }

    #[test]
    fn quote_task_path_normalizes_existing_quotes() {
        assert_eq!(
            quote_task_path("\"D:/GammaRay/GammaRayGuard.exe\""),
            "\"D:/GammaRay/GammaRayGuard.exe\""
        );
    }

    #[test]
    fn create_args_include_task_name_and_trigger_path() {
        let spec = LogonTaskSpec::new(
            "GammaRay_Guard_Start",
            Path::new("D:/GammaRay/GammaRayGuard.exe"),
            "GR",
        );
        let args = spec.create_args();
        assert!(args.windows(2).any(|w| w == ["/TN", "GammaRay_Guard_Start"]));
        assert!(args
            .windows(2)
            .any(|w| w == ["/TR", "\"D:/GammaRay/GammaRayGuard.exe\""]));
    }
}
