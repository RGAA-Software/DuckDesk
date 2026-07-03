use std::sync::mpsc::{Receiver, Sender};
use std::sync::{Arc, Mutex};

use super::content::{ClipboardContent, ClipboardFileEntry};
use crate::clipboard::virtual_file::VirtualFileCoordinator;

pub trait ClipboardBackend: Send + Sync {
    fn read_content(&self) -> anyhow::Result<ClipboardContent>;

    fn write_text(&self, text: &str) -> anyhow::Result<()>;

    fn write_file_paths(&self, paths: &[String]) -> anyhow::Result<()> {
        let _ = paths;
        anyhow::bail!("write_file_paths not supported by this backend")
    }

    fn write_virtual_files(&self, _coordinator: Arc<VirtualFileCoordinator>) -> anyhow::Result<()> {
        anyhow::bail!("write_virtual_files not supported by this backend")
    }

    fn read_text(&self) -> anyhow::Result<Option<String>> {
        Ok(self.read_content()?.text)
    }
}

#[derive(Clone)]
pub struct InMemoryClipboard {
    content: Arc<Mutex<ClipboardContent>>,
    notify: Sender<()>,
    virtual_session: Arc<Mutex<Option<Arc<VirtualFileCoordinator>>>>,
}

impl InMemoryClipboard {
    pub fn new_pair() -> (Self, Receiver<()>) {
        let (notify_tx, notify_rx) = std::sync::mpsc::channel();
        (
            Self {
                content: Arc::new(Mutex::new(ClipboardContent::default())),
                notify: notify_tx,
                virtual_session: Arc::new(Mutex::new(None)),
            },
            notify_rx,
        )
    }

    pub fn set_local_text(&self, text: impl Into<String>) {
        {
            let mut guard = self.content.lock().expect("lock");
            guard.text = Some(text.into());
            guard.files.clear();
        }
        let _ = self.notify.send(());
    }

    pub fn set_local_files(&self, files: Vec<ClipboardFileEntry>) {
        {
            let mut guard = self.content.lock().expect("lock");
            guard.text = None;
            guard.files = files;
        }
        let _ = self.notify.send(());
    }
    pub fn virtual_session(&self) -> Option<Arc<VirtualFileCoordinator>> {
        self.virtual_session.lock().expect("lock").clone()
    }
}

impl ClipboardBackend for InMemoryClipboard {
    fn read_content(&self) -> anyhow::Result<ClipboardContent> {
        Ok(self.content.lock().expect("lock").clone())
    }

    fn write_text(&self, text: &str) -> anyhow::Result<()> {
        let mut guard = self.content.lock().expect("lock");
        guard.text = Some(text.to_string());
        guard.files.clear();
        Ok(())
    }

    fn write_file_paths(&self, paths: &[String]) -> anyhow::Result<()> {
        let entries = super::content::build_file_entries_from_paths(
            &paths.iter().cloned().collect::<Vec<_>>(),
        );
        let mut guard = self.content.lock().expect("lock");
        guard.text = None;
        guard.files = entries;
        *self.virtual_session.lock().expect("lock") = None;
        Ok(())
    }

    fn write_virtual_files(&self, coordinator: Arc<VirtualFileCoordinator>) -> anyhow::Result<()> {
        let files = coordinator
            .session_files()
            .ok_or_else(|| anyhow::anyhow!("virtual file session missing"))?;
        let mut guard = self.content.lock().expect("lock");
        guard.text = None;
        guard.files = files;
        *self.virtual_session.lock().expect("lock") = Some(coordinator);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn in_memory_write_read_roundtrip() {
        let (clip, _rx) = InMemoryClipboard::new_pair();
        clip.write_text("hello").expect("write");
        assert_eq!(clip.read_text().expect("read").as_deref(), Some("hello"));
    }

    #[test]
    fn set_local_text_notifies() {
        let (clip, rx) = InMemoryClipboard::new_pair();
        clip.set_local_text("changed");
        rx.recv_timeout(std::time::Duration::from_secs(1))
            .expect("notify");
    }
}
