use std::ffi::OsStr;
use std::os::windows::ffi::OsStrExt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use windows::core::BOOL;
use windows::Win32::Foundation::{GlobalFree, HGLOBAL, HANDLE, POINT};
use windows::Win32::System::DataExchange::{
    CloseClipboard, EmptyClipboard, GetClipboardData, OpenClipboard, SetClipboardData,
};
use windows::Win32::System::Memory::{GlobalAlloc, GlobalLock, GlobalUnlock, GMEM_MOVEABLE, GMEM_ZEROINIT};
use windows::Win32::System::Ole::{OleInitialize, OleSetClipboard};
use windows::Win32::UI::Shell::{DragQueryFileW, HDROP};
use windows::Win32::UI::WindowsAndMessaging::{
    DispatchMessageW, PeekMessageW, TranslateMessage, MSG, PM_REMOVE,
};

use super::content::{build_file_entries_from_paths, ClipboardContent};

const CF_UNICODETEXT: u32 = 13;
pub(crate) const CF_HDROP: u32 = 15;
const HRESULT_ACCESS_DENIED: i32 = 0x80070005u32 as i32;

static OLE_CLIPBOARD_ACTIVE: AtomicBool = AtomicBool::new(false);

pub(crate) fn set_ole_clipboard_active(active: bool) {
    OLE_CLIPBOARD_ACTIVE.store(active, Ordering::SeqCst);
}

pub(crate) fn is_ole_clipboard_active() -> bool {
    OLE_CLIPBOARD_ACTIVE.load(Ordering::SeqCst)
}

fn is_clipboard_access_denied(err: &anyhow::Error) -> bool {
    if let Some(win_err) = err.downcast_ref::<windows::core::Error>() {
        return win_err.code().0 == HRESULT_ACCESS_DENIED;
    }
    false
}

/// `GMEM_SHARE` — not exported from `Win32::System::Memory` in windows-rs.
const GMEM_SHARE: u32 = 0x2000;

/// `GHND | GMEM_SHARE` — shell clipboard data must be moveable and shared.
#[inline]
pub(crate) fn clipboard_global_alloc_flags() -> windows::Win32::System::Memory::GLOBAL_ALLOC_FLAGS {
    GMEM_MOVEABLE | GMEM_ZEROINIT | windows::Win32::System::Memory::GLOBAL_ALLOC_FLAGS(GMEM_SHARE)
}

#[repr(C)]
struct DropFiles {
    p_files: u32,
    pt: POINT,
    f_nc: BOOL,
    f_wide: BOOL,
}

/// Closes the clipboard on scope exit so failure paths never leave it open.
pub(crate) struct OpenClipboardGuard;

impl OpenClipboardGuard {
    pub(crate) fn open() -> anyhow::Result<Self> {
        unsafe {
            OpenClipboard(None).map_err(|err| anyhow::anyhow!("OpenClipboard failed: {err}"))?;
        }
        Ok(Self)
    }
}

impl Drop for OpenClipboardGuard {
    fn drop(&mut self) {
        unsafe {
            let _ = CloseClipboard();
        }
    }
}

/// `GlobalUnlock` returns FALSE with `NO_ERROR` when the lock count reaches
/// zero, which is the normal success case; only a real error code is a failure.
unsafe fn global_unlock(mem: HGLOBAL) -> anyhow::Result<()> {
    if let Err(err) = GlobalUnlock(mem) {
        if err.code() != windows::core::HRESULT(0) {
            anyhow::bail!("GlobalUnlock failed: {err}");
        }
    }
    Ok(())
}

pub struct WinClipboardPlatform;

/// Drain pending COM/OLE messages on the current STA thread.
pub(crate) fn pump_sta_messages() {
    unsafe {
        let mut msg = MSG::default();
        while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {
            let _ = TranslateMessage(&msg);
            let _ = DispatchMessageW(&msg);
        }
    }
}

/// Wait while continuing to pump COM/OLE messages (required for async IDataObject paste).
pub(crate) fn wait_sta_messages(total: Duration) {
    let start = std::time::Instant::now();
    while start.elapsed() < total {
        pump_sta_messages();
        let remaining = total.saturating_sub(start.elapsed());
        std::thread::sleep(remaining.min(Duration::from_millis(15)));
    }
}

pub(crate) fn build_hdrop_global(paths: &[&str]) -> anyhow::Result<HGLOBAL> {
    if paths.is_empty() {
        anyhow::bail!("no paths for HDROP");
    }
    let mut wide_paths = Vec::new();
    for path in paths {
        wide_paths.extend(
            OsStr::new(path)
                .encode_wide()
                .chain(std::iter::once(0)),
        );
    }
    wide_paths.push(0);

    let header_size = std::mem::size_of::<DropFiles>();
    let payload_bytes = wide_paths.len() * 2;
    let total_bytes = header_size + payload_bytes;

    unsafe {
        let mem = GlobalAlloc(clipboard_global_alloc_flags(), total_bytes)
            .map_err(|err| anyhow::anyhow!("GlobalAlloc HDROP failed: {err}"))?;
        let base = GlobalLock(mem);
        if base.is_null() {
            let _ = GlobalFree(Some(mem));
            anyhow::bail!("GlobalLock HDROP failed");
        }

        let drop_files = DropFiles {
            p_files: header_size as u32,
            pt: POINT { x: 0, y: 0 },
            f_nc: BOOL(0),
            f_wide: BOOL(1),
        };
        std::ptr::write(base as *mut DropFiles, drop_files);
        std::ptr::copy_nonoverlapping(
            wide_paths.as_ptr() as *const u8,
            base.add(header_size) as *mut u8,
            payload_bytes,
        );
        global_unlock(mem)?;
        Ok(mem)
    }
}

fn release_ole_clipboard_owner() {
    unsafe {
        let _ = OleSetClipboard(None);
    }
}

impl WinClipboardPlatform {
    pub fn new() -> Self {
        unsafe {
            let _ = OleInitialize(None);
        }
        Self
    }

    pub fn clear(&self) -> anyhow::Result<()> {
        let mut last_err = None;
        for attempt in 0..20 {
            match Self::try_clear() {
                Ok(()) => return Ok(()),
                Err(err) => {
                    tracing::warn!("clipboard clear retry {}: {err:#}", attempt + 1);
                    last_err = Some(err);
                }
            }
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        anyhow::bail!("clipboard clear failed after retries: {:#}", last_err.unwrap())
    }

    fn try_clear() -> anyhow::Result<()> {
        release_ole_clipboard_owner();
        let _guard = OpenClipboardGuard::open()?;
        unsafe {
            let _ = EmptyClipboard();
        }
        Ok(())
    }

    pub fn read_content(&self) -> anyhow::Result<ClipboardContent> {
        if is_ole_clipboard_active() {
            return Ok(ClipboardContent::default());
        }
        for attempt in 0..20 {
            match Self::try_read_content() {
                Ok(v) => return Ok(v),
                Err(err) => {
                    if is_clipboard_access_denied(&err) && is_ole_clipboard_active() {
                        return Ok(ClipboardContent::default());
                    }
                    if attempt == 19 {
                        return Err(err);
                    }
                    std::thread::sleep(Duration::from_millis(5));
                }
            }
        }
        Ok(ClipboardContent::default())
    }

    fn try_read_content() -> anyhow::Result<ClipboardContent> {
        let _guard = OpenClipboardGuard::open()?;
        unsafe {
            let mut content = ClipboardContent::default();

            if let Ok(handle) = GetClipboardData(CF_UNICODETEXT) {
                if let Some(text) = Self::read_unicode_text(handle) {
                    if !text.is_empty() {
                        content.text = Some(text);
                    }
                }
            }

            if let Ok(handle) = GetClipboardData(CF_HDROP) {
                let paths = Self::read_hdrop_paths(HDROP(handle.0 as *mut _));
                if !paths.is_empty() {
                    content.files = build_file_entries_from_paths(&paths);
                }
            }

            Ok(content)
        }
    }

    pub fn read_text(&self) -> anyhow::Result<Option<String>> {
        Ok(self.read_content()?.text)
    }

    unsafe fn read_unicode_text(handle: HANDLE) -> Option<String> {
        let hglobal = HGLOBAL(handle.0);
        let ptr = GlobalLock(hglobal);
        if ptr.is_null() {
            return None;
        }
        let wide =
            std::slice::from_raw_parts(ptr as *const u16, Self::wide_len(ptr as *const u16));
        let text = String::from_utf16_lossy(wide);
        if let Err(err) = global_unlock(hglobal) {
            tracing::warn!("read clipboard text unlock: {err:#}");
        }
        if text.is_empty() {
            None
        } else {
            Some(text)
        }
    }

    unsafe fn read_hdrop_paths(drop: HDROP) -> Vec<String> {
        let count = DragQueryFileW(drop, 0xFFFF, None);
        let mut paths = Vec::new();
        for index in 0..count {
            let len = DragQueryFileW(drop, index, None);
            if len == 0 {
                continue;
            }
            let mut wide = vec![0u16; len as usize + 1];
            if DragQueryFileW(drop, index, Some(&mut wide)) == 0 {
                continue;
            }
            wide.truncate(len as usize);
            paths.push(String::from_utf16_lossy(&wide));
        }
        paths
    }

    unsafe fn wide_len(ptr: *const u16) -> usize {
        let mut len = 0;
        while *ptr.add(len) != 0 {
            len += 1;
        }
        len
    }

    pub fn write_text(&self, text: &str) -> anyhow::Result<()> {
        let mut last_err = None;
        for attempt in 0..20 {
            match Self::try_write_text(text) {
                Ok(()) => return Ok(()),
                Err(err) => {
                    tracing::warn!("write clipboard text retry {}: {err:#}", attempt + 1);
                    last_err = Some(err);
                }
            }
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        anyhow::bail!(
            "write clipboard text failed after retries: {:#}",
            last_err.unwrap()
        )
    }

    fn try_write_text(text: &str) -> anyhow::Result<()> {
        let wide: Vec<u16> = text.encode_utf16().chain(std::iter::once(0)).collect();
        let bytes = wide.len() * 2;
        release_ole_clipboard_owner();
        let _guard = OpenClipboardGuard::open()?;
        unsafe {
            let _ = EmptyClipboard();
            let mem =
                GlobalAlloc(GMEM_MOVEABLE, bytes).map_err(|err| anyhow::anyhow!("GlobalAlloc failed: {err}"))?;
            let ptr = GlobalLock(mem);
            if ptr.is_null() {
                let _ = GlobalFree(Some(mem));
                anyhow::bail!("GlobalLock failed");
            }
            std::ptr::copy_nonoverlapping(wide.as_ptr() as *const u8, ptr as *mut u8, bytes);
            global_unlock(mem)?;
            if let Err(err) = SetClipboardData(CF_UNICODETEXT, Some(HANDLE(mem.0))) {
                let _ = GlobalFree(Some(mem));
                anyhow::bail!("SetClipboardData failed: {err}");
            }
            // Ownership of `mem` transferred to the clipboard on success.
            Ok(())
        }
    }

    pub fn write_file_paths(&self, paths: &[String]) -> anyhow::Result<()> {
        if paths.is_empty() {
            anyhow::bail!("no file paths to write");
        }
        let mut last_err = None;
        for attempt in 0..20 {
            match Self::try_write_file_paths(paths) {
                Ok(()) => return Ok(()),
                Err(err) => {
                    tracing::warn!("write clipboard files retry {}: {err:#}", attempt + 1);
                    last_err = Some(err);
                }
            }
            std::thread::sleep(std::time::Duration::from_millis(10));
        }
        anyhow::bail!(
            "write clipboard files failed after retries: {:#}",
            last_err.unwrap()
        )
    }

    fn try_write_file_paths(paths: &[String]) -> anyhow::Result<()> {
        let path_refs: Vec<&str> = paths.iter().map(|p| p.as_str()).collect();

        release_ole_clipboard_owner();
        let _guard = OpenClipboardGuard::open()?;
        unsafe {
            let _ = EmptyClipboard();
            let mem = build_hdrop_global(&path_refs)?;
            if let Err(err) = SetClipboardData(CF_HDROP, Some(HANDLE(mem.0))) {
                let _ = GlobalFree(Some(mem));
                anyhow::bail!("SetClipboardData files failed: {err}");
            }
            // Ownership of `mem` transferred to the clipboard on success.
            Ok(())
        }
    }
}

#[cfg(test)]
pub(crate) static CLIPBOARD_TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

#[cfg(test)]
mod tests {
    use super::WinClipboardPlatform;
    use super::CLIPBOARD_TEST_LOCK;

    #[test]
    fn read_empty_after_clear() {
        let _lock = CLIPBOARD_TEST_LOCK.lock().unwrap();
        let platform = WinClipboardPlatform::new();
        platform.clear().expect("clear");
        let read = platform.read_content().expect("read");
        assert!(read.is_empty());
    }

    #[test]
    fn write_then_read_text_roundtrip() {
        let _lock = CLIPBOARD_TEST_LOCK.lock().unwrap();
        super::set_ole_clipboard_active(false);
        let platform = WinClipboardPlatform::new();
        platform.clear().expect("clear");
        platform.write_text("gr_user_proxy_roundtrip").expect("write");
        let read = platform.read_content().expect("read");
        assert_eq!(read.text.as_deref(), Some("gr_user_proxy_roundtrip"));
        platform.clear().expect("clear");
    }
}
