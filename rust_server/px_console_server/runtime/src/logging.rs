use std::{fs::OpenOptions, io, path::Path, sync::Mutex};

/// The service has no interactive stdout. Keep diagnostics beside its persistent
/// recording cache. The Windows installer grants only this directory write access.
pub fn initialize(recording_cache_root: &Path) -> io::Result<()> {
    let directory = recording_cache_root
        .parent()
        .ok_or_else(|| io::Error::other("recording cache has no parent for logs"))?
        .join("logs");
    std::fs::create_dir_all(&directory)?;
    let log_path = directory.join("console.log");
    let log_file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)?;
    tracing_subscriber::fmt()
        .with_ansi(false)
        .with_env_filter(tracing_subscriber::EnvFilter::new(
            "warn,px_console_runtime=info,px_console=info",
        ))
        .with_writer(Mutex::new(log_file))
        .try_init()
        .map_err(io::Error::other)?;
    tracing::info!(path = %log_path.display(), "Console file logging initialized");
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn service_diagnostics_are_written_to_the_persistent_log_file() {
        let directory = tempfile::tempdir().unwrap();
        super::initialize(&directory.path().join("recording-cache")).unwrap();
        tracing::info!(target: "px_console_runtime::resource_api", session_id = "test-session", state = "closed", "resource connection close processed");
        let contents = std::fs::read_to_string(directory.path().join("logs/console.log")).unwrap();
        assert!(contents.contains("resource connection close processed"));
        assert!(contents.contains("test-session"));
        assert!(!contents.contains('\u{1b}'));
    }
}
