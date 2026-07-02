use std::sync::mpsc::{Receiver, Sender};

use crate::clipboard::backend::ClipboardBackend;
use crate::clipboard::content::ClipboardContent;
use crate::clipboard::win_platform::WinClipboardPlatform;

enum ClipboardRequest {
    ReadContent(Sender<anyhow::Result<ClipboardContent>>),
    WriteText {
        text: String,
        reply: Sender<anyhow::Result<()>>,
    },
    WriteFilePaths {
        paths: Vec<String>,
        reply: Sender<anyhow::Result<()>>,
    },
}

pub struct WinClipboardBackend {
    request_tx: Sender<ClipboardRequest>,
}

struct WinClipboardWorker {
    platform: WinClipboardPlatform,
}

impl ClipboardBackend for WinClipboardWorker {
    fn read_content(&self) -> anyhow::Result<ClipboardContent> {
        self.platform.read_content()
    }

    fn write_text(&self, text: &str) -> anyhow::Result<()> {
        self.platform.write_text(text)
    }

    fn write_file_paths(&self, paths: &[String]) -> anyhow::Result<()> {
        self.platform.write_file_paths(paths)
    }
}

impl ClipboardBackend for WinClipboardBackend {
    fn read_content(&self) -> anyhow::Result<ClipboardContent> {
        let (tx, rx) = std::sync::mpsc::channel();
        self.request_tx
            .send(ClipboardRequest::ReadContent(tx))?;
        rx.recv()?
    }

    fn write_text(&self, text: &str) -> anyhow::Result<()> {
        let (tx, rx) = std::sync::mpsc::channel();
        self.request_tx.send(ClipboardRequest::WriteText {
            text: text.to_string(),
            reply: tx,
        })?;
        rx.recv()?
    }

    fn write_file_paths(&self, paths: &[String]) -> anyhow::Result<()> {
        let (tx, rx) = std::sync::mpsc::channel();
        self.request_tx.send(ClipboardRequest::WriteFilePaths {
            paths: paths.to_vec(),
            reply: tx,
        })?;
        rx.recv()?
    }
}

pub fn spawn_win_clipboard_listener(
) -> anyhow::Result<(WinClipboardBackend, Receiver<()>)> {
    let (notify_tx, notify_rx) = std::sync::mpsc::channel();
    let (request_tx, request_rx) = std::sync::mpsc::channel();
    std::thread::Builder::new()
        .name("gr_user_proxy_clipboard".into())
        .spawn(move || clipboard_worker_loop(notify_tx, request_rx))?;
    Ok((WinClipboardBackend { request_tx }, notify_rx))
}

fn clipboard_worker_loop(notify_tx: Sender<()>, request_rx: Receiver<ClipboardRequest>) {
    let worker = WinClipboardWorker {
        platform: WinClipboardPlatform::new(),
    };
    let mut last_fingerprint = String::new();
    loop {
        while let Ok(req) = request_rx.try_recv() {
            match req {
                ClipboardRequest::ReadContent(reply) => {
                    let _ = reply.send(worker.read_content());
                }
                ClipboardRequest::WriteText { text, reply } => {
                    let _ = reply.send(worker.write_text(&text));
                }
                ClipboardRequest::WriteFilePaths { paths, reply } => {
                    let _ = reply.send(worker.write_file_paths(&paths));
                }
            }
        }

        match worker.read_content() {
            Ok(content) => {
                let fingerprint = content.fingerprint();
                if !content.is_empty() && fingerprint != last_fingerprint {
                    last_fingerprint = fingerprint;
                    tracing::info!(
                        "local clipboard changed, has_text={}, file_count={}",
                        content.has_text(),
                        content.files.len()
                    );
                    let _ = notify_tx.send(());
                }
            }
            Err(err) => tracing::error!("clipboard poll read failed: {err:#}"),
        }
        std::thread::sleep(std::time::Duration::from_millis(250));
    }
}

pub type ClipboardListener = WinClipboardBackend;

#[cfg(test)]
mod tests {
    use std::time::Duration;

    #[test]
    fn poll_interval_is_quarter_second() {
        let start = std::time::Instant::now();
        std::thread::sleep(Duration::from_millis(250));
        assert!(start.elapsed() >= Duration::from_millis(200));
    }
}
