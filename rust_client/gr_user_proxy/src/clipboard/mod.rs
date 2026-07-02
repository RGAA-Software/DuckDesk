pub mod backend;
pub mod content;
pub mod echo;
pub mod win_listener;
pub mod win_platform;

use std::sync::Arc;

use crate::clipboard::backend::ClipboardBackend;
use crate::clipboard::content::{ClipboardContent, ClipboardFileEntry};
use crate::clipboard::echo::{EchoFilter, SuppressOutboundGuard};

pub struct ClipboardService {
    pub echo: EchoFilter,
    backend: Arc<dyn ClipboardBackend>,
}

impl ClipboardService {
    pub fn new(backend: Arc<dyn ClipboardBackend>) -> Self {
        Self {
            echo: EchoFilter::default(),
            backend,
        }
    }

    pub fn apply_remote_text(&self, text: &str) -> anyhow::Result<()> {
        if text.is_empty() {
            tracing::info!("no syncable text");
            return Ok(());
        }
        let _guard = SuppressOutboundGuard::new(&self.echo);
        self.echo.set_remote_echo(text);
        self.backend.write_text(text)?;
        tracing::info!("apply remote clipboard text, len={}", text.len());
        Ok(())
    }

    pub fn apply_remote_files(&self, files: &[ClipboardFileEntry]) -> anyhow::Result<()> {
        if files.is_empty() {
            tracing::info!("no syncable files");
            return Ok(());
        }

        let local_paths: Vec<String> = files
            .iter()
            .filter_map(|file| {
                if file.full_path.is_empty() {
                    tracing::warn!(
                        "remote clipboard file missing full_path, name={}",
                        file.file_name
                    );
                    return None;
                }
                if !std::path::Path::new(&file.full_path).exists() {
                    tracing::warn!(
                        "remote clipboard file path not found, name={}, path={}",
                        file.file_name,
                        file.full_path
                    );
                    return None;
                }
                Some(file.full_path.clone())
            })
            .collect();

        if local_paths.is_empty() {
            tracing::warn!(
                "remote clipboard files require local paths or OLE virtual file stream, count={}",
                files.len()
            );
            return Ok(());
        }

        let _guard = SuppressOutboundGuard::new(&self.echo);
        self.backend.write_file_paths(&local_paths)?;
        // Record echo from the read-back entries (paths may be canonicalized by
        // the backend), so the poller does not send these files back to Render.
        match self.backend.read_content() {
            Ok(content) if content.has_files() => {
                self.echo
                    .set_remote_echo(&crate::clipboard::content::files_signature(&content.files));
            }
            Ok(_) => {}
            Err(err) => tracing::warn!("read back clipboard files for echo failed: {err:#}"),
        }
        tracing::info!(
            "apply remote clipboard files, count={}, paths={local_paths:?}",
            local_paths.len()
        );
        Ok(())
    }

    pub fn read_local_content(&self) -> anyhow::Result<ClipboardContent> {
        self.backend.read_content()
    }

    pub fn read_local_text(&self) -> anyhow::Result<Option<String>> {
        Ok(self.read_local_content()?.text)
    }
}

pub use backend::InMemoryClipboard;
pub use win_listener::{spawn_win_clipboard_listener, ClipboardListener, WinClipboardBackend};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn echo_integration_remote_write_skips_outbound() {
        let (backend, _rx) = InMemoryClipboard::new_pair();
        let svc = ClipboardService::new(Arc::new(backend));
        svc.apply_remote_text("from-remote").expect("apply");
        assert!(svc.echo.should_skip_outbound("from-remote"));
        assert!(!svc.echo.should_skip_outbound("other"));
    }

    #[test]
    fn apply_remote_files_writes_paths() {
        let root = std::env::temp_dir().join(format!(
            "gr_user_proxy_apply_{}",
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .expect("time")
                .as_nanos()
        ));
        std::fs::create_dir_all(&root).expect("mkdir");
        let file = root.join("clip.txt");
        std::fs::write(&file, b"x").expect("write");

        let (backend, _rx) = InMemoryClipboard::new_pair();
        let svc = ClipboardService::new(Arc::new(backend));
        svc.apply_remote_files(&[ClipboardFileEntry {
            full_path: file.display().to_string(),
            file_name: "clip.txt".to_string(),
            ref_path: "clip.txt".to_string(),
            total_size: 1,
        }])
        .expect("apply files");

        let content = svc.read_local_content().expect("read");
        assert_eq!(content.files.len(), 1);
        assert_eq!(content.files[0].file_name, "clip.txt");

        // Echo must match the read-back signature so the poller does not loop
        // remote files back to Render.
        let signature = crate::clipboard::content::files_signature(&content.files);
        assert!(svc.echo.should_skip_outbound(&signature));
    }
}
