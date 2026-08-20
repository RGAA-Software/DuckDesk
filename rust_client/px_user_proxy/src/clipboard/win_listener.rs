use std::sync::mpsc::{Receiver, Sender};
use std::sync::OnceLock;

use windows::core::{w, PCWSTR};
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::System::DataExchange::AddClipboardFormatListener;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::WindowsAndMessaging::{
    CreateWindowExW, DefWindowProcW, RegisterClassW, HWND_MESSAGE, WINDOW_EX_STYLE, WINDOW_STYLE,
    WM_CLIPBOARDUPDATE, WNDCLASSW,
};

use crate::clipboard::backend::ClipboardBackend;
use crate::clipboard::content::ClipboardContent;
use crate::clipboard::virtual_file::install_virtual_file_clipboard;
use crate::clipboard::virtual_file::VirtualFileCoordinator;
use crate::clipboard::win_platform::{pump_sta_messages, wait_sta_messages, WinClipboardPlatform};

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
    WriteVirtualFiles {
        coordinator: std::sync::Arc<VirtualFileCoordinator>,
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

    fn write_virtual_files(
        &self,
        coordinator: std::sync::Arc<VirtualFileCoordinator>,
    ) -> anyhow::Result<()> {
        install_virtual_file_clipboard(coordinator)
    }
}

impl ClipboardBackend for WinClipboardBackend {
    fn read_content(&self) -> anyhow::Result<ClipboardContent> {
        let (tx, rx) = std::sync::mpsc::channel();
        self.request_tx.send(ClipboardRequest::ReadContent(tx))?;
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

    fn write_virtual_files(
        &self,
        coordinator: std::sync::Arc<VirtualFileCoordinator>,
    ) -> anyhow::Result<()> {
        let (tx, rx) = std::sync::mpsc::channel();
        self.request_tx.send(ClipboardRequest::WriteVirtualFiles {
            coordinator,
            reply: tx,
        })?;
        rx.recv()?
    }
}

pub fn spawn_win_clipboard_listener() -> anyhow::Result<(WinClipboardBackend, Receiver<()>)> {
    let (notify_tx, notify_rx) = std::sync::mpsc::channel();
    let (request_tx, request_rx) = std::sync::mpsc::channel();
    std::thread::Builder::new()
        .name("px_user_proxy_clipboard".into())
        .spawn(move || clipboard_worker_loop(notify_tx, request_rx))?;
    Ok((WinClipboardBackend { request_tx }, notify_rx))
}

/// Sender exposed to the WM_CLIPBOARDUPDATE window proc.
static NOTIFY_TX: OnceLock<Sender<()>> = OnceLock::new();

unsafe extern "system" fn clipboard_wnd_proc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    if msg == WM_CLIPBOARDUPDATE {
        if let Some(tx) = NOTIFY_TX.get() {
            let _ = tx.send(());
        }
        LRESULT(0)
    } else {
        DefWindowProcW(hwnd, msg, wparam, lparam)
    }
}

/// Create a message-only hidden window and register it for clipboard updates.
fn create_clipboard_listener_window(notify_tx: Sender<()>) -> anyhow::Result<HWND> {
    let _ = NOTIFY_TX.set(notify_tx);

    let instance: windows::Win32::Foundation::HINSTANCE =
        unsafe { GetModuleHandleW(PCWSTR::null())? }.into();
    let class_name = w!("px_function_clipboard");

    let wnd_class = WNDCLASSW {
        lpfnWndProc: Some(clipboard_wnd_proc),
        lpszClassName: class_name,
        hInstance: instance,
        ..Default::default()
    };
    unsafe {
        RegisterClassW(&wnd_class);
    }

    let hwnd = unsafe {
        CreateWindowExW(
            WINDOW_EX_STYLE(0),
            class_name,
            w!(""),
            WINDOW_STYLE(0),
            0,
            0,
            0,
            0,
            Some(HWND_MESSAGE),
            None,
            Some(instance),
            None,
        )?
    };

    unsafe {
        AddClipboardFormatListener(hwnd)?;
    }
    tracing::info!("clipboard listener window created, hwnd={:?}", hwnd.0);
    Ok(hwnd)
}

fn clipboard_worker_loop(notify_tx: Sender<()>, request_rx: Receiver<ClipboardRequest>) {
    let worker = WinClipboardWorker {
        platform: WinClipboardPlatform::new(),
    };

    // Event-driven: a hidden message-only window receives WM_CLIPBOARDUPDATE and
    // posts a notify via NOTIFY_TX. No polling.
    if let Err(err) = create_clipboard_listener_window(notify_tx) {
        tracing::error!("create clipboard listener window failed: {err:#}");
        return;
    }

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
                ClipboardRequest::WriteVirtualFiles { coordinator, reply } => {
                    let _ = reply.send(worker.write_virtual_files(coordinator));
                }
            }
            pump_sta_messages();
        }

        // Pump messages so WM_CLIPBOARDUPDATE is dispatched.
        pump_sta_messages();
        wait_sta_messages(std::time::Duration::from_millis(250));
    }
}

pub type ClipboardListener = WinClipboardBackend;

#[cfg(test)]
mod tests {
    use std::time::Duration;

    #[test]
    fn poll_wait_pumps_for_quarter_second() {
        let start = std::time::Instant::now();
        crate::clipboard::win_platform::wait_sta_messages(Duration::from_millis(250));
        assert!(start.elapsed() >= Duration::from_millis(200));
    }
}
