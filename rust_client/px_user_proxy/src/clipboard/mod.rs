pub mod backend;
pub mod content;
pub mod echo;
pub mod virtual_file;
pub mod win_listener;
pub mod win_platform;

use std::sync::Arc;

use crate::clipboard::backend::ClipboardBackend;
use crate::clipboard::content::{files_signature, ClipboardContent, ClipboardFileEntry};
use crate::clipboard::echo::{EchoFilter, SuppressOutboundGuard};
use crate::clipboard::virtual_file::{VirtualFileCoordinator, VirtualFileSession};
use crate::proto::StreamRoute;

pub struct ClipboardService {
    pub echo: EchoFilter,
    backend: Arc<dyn ClipboardBackend>,
    virtual_files: Option<Arc<VirtualFileCoordinator>>,
}

impl ClipboardService {
    pub fn new(backend: Arc<dyn ClipboardBackend>) -> Self {
        Self {
            echo: EchoFilter::default(),
            backend,
            virtual_files: None,
        }
    }

    pub fn with_virtual_files(
        backend: Arc<dyn ClipboardBackend>,
        virtual_files: Arc<VirtualFileCoordinator>,
    ) -> Self {
        Self {
            echo: EchoFilter::default(),
            backend,
            virtual_files: Some(virtual_files),
        }
    }

    pub fn virtual_file_coordinator(&self) -> Option<Arc<VirtualFileCoordinator>> {
        self.virtual_files.clone()
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

    pub fn apply_remote_files(
        &self,
        files: &[ClipboardFileEntry],
        route: &StreamRoute,
    ) -> anyhow::Result<()> {
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

        let _guard = SuppressOutboundGuard::new(&self.echo);

        if local_paths.len() == files.len() {
            self.backend.write_file_paths(&local_paths)?;
            match self.backend.read_content() {
                Ok(content) if content.has_files() => {
                    self.echo
                        .set_remote_echo(&files_signature(&content.files));
                }
                Ok(_) => {}
                Err(err) => tracing::warn!("read back clipboard files for echo failed: {err:#}"),
            }
            tracing::info!(
                "apply remote clipboard files via HDROP, count={}, paths={local_paths:?}",
                local_paths.len()
            );
            return Ok(());
        }

        let Some(coordinator) = self.virtual_files.clone() else {
            tracing::warn!(
                "remote clipboard files require local paths or virtual file coordinator, count={}",
                files.len()
            );
            return Ok(());
        };

        coordinator.install_session(VirtualFileSession {
            route: route.clone(),
            files: files.to_vec(),
        });
        self.backend.write_virtual_files(coordinator.clone())?;
        self.echo.set_remote_echo(&files_signature(files));
        tracing::info!(
            "apply remote clipboard files via OLE virtual file, count={}",
            files.len()
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
        svc.apply_remote_files(
            &[ClipboardFileEntry {
                full_path: file.display().to_string(),
                file_name: "clip.txt".to_string(),
                ref_path: "clip.txt".to_string(),
                total_size: 1,
            }],
            &crate::proto::StreamRoute::default(),
        )
        .expect("apply files");

        let content = svc.read_local_content().expect("read");
        assert_eq!(content.files.len(), 1);
        assert_eq!(content.files[0].file_name, "clip.txt");

        // Echo must match the read-back signature so the poller does not loop
        // remote files back to Render.
        let signature = crate::clipboard::content::files_signature(&content.files);
        assert!(svc.echo.should_skip_outbound(&signature));
    }

    #[test]
    fn apply_remote_virtual_files_without_local_paths() {
        let coordinator = crate::clipboard::virtual_file::VirtualFileCoordinator::new();
        let (backend, _rx) = InMemoryClipboard::new_pair();
        let svc = ClipboardService::with_virtual_files(Arc::new(backend.clone()), coordinator);
        let route = crate::proto::StreamRoute {
            stream_id: "s".to_string(),
            device_id: "d".to_string(),
        };
        svc.apply_remote_files(
            &[ClipboardFileEntry {
                full_path: "Z:/missing/file.bin".to_string(),
                file_name: "file.bin".to_string(),
                ref_path: "file.bin".to_string(),
                total_size: 100,
            }],
            &route,
        )
        .expect("apply virtual");

        let content = backend.read_content().expect("read");
        assert_eq!(content.files.len(), 1);
        assert!(backend.virtual_session().is_some());
        let signature = crate::clipboard::content::files_signature(&[ClipboardFileEntry {
            full_path: "Z:/missing/file.bin".to_string(),
            file_name: "file.bin".to_string(),
            ref_path: "file.bin".to_string(),
            total_size: 100,
        }]);
        assert!(svc.echo.should_skip_outbound(&signature));
    }
}
