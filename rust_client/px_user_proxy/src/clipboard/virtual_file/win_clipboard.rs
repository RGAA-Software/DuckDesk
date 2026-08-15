//! OLE virtual-file clipboard (`CFSTR_FILEDESCRIPTOR` / `CFSTR_FILECONTENTS`).
//! Must run on the clipboard STA thread (see `win_listener`).

use std::ffi::c_void;
use std::mem::ManuallyDrop;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use tracing::{debug, error, info, warn};
use windows::core::{BOOL, HRESULT, PCWSTR};
use windows::Win32::Foundation::{HGLOBAL, S_FALSE, S_OK};
use windows::Win32::Storage::FileSystem::FILE_ATTRIBUTE_NORMAL;
use windows::Win32::System::Com::{
    IDataObject, IDataObject_Impl, IAdviseSink, IEnumFORMATETC, IEnumSTATDATA, IStream,
    DATADIR_GET, DVASPECT_CONTENT, FORMATETC, LOCKTYPE, STGC, STGMEDIUM, STATFLAG, STGTY_STREAM,
    STREAM_SEEK, STREAM_SEEK_CUR, STREAM_SEEK_END, STREAM_SEEK_SET, TYMED_HGLOBAL, TYMED_ISTREAM,
    ISequentialStream_Impl, IStream_Impl, STATSTG,
};
use windows::Win32::System::DataExchange::RegisterClipboardFormatW;
use windows::Win32::System::Memory::{GlobalAlloc, GlobalLock, GlobalUnlock};
use windows::Win32::System::Ole::{OleFlushClipboard, OleSetClipboard};
use windows::Win32::System::SystemInformation::GetSystemTimeAsFileTime;
use windows::Win32::UI::Shell::{
    IDataObjectAsyncCapability, IDataObjectAsyncCapability_Impl, CFSTR_FILECONTENTS,
    CFSTR_FILEDESCRIPTOR, CFSTR_PREFERREDDROPEFFECT, FILEDESCRIPTORW,
    FILEGROUPDESCRIPTORW, FD_ATTRIBUTES, FD_CREATETIME, FD_FILESIZE, FD_PROGRESSUI, FD_WRITESTIME,
    SHCreateStdEnumFmtEtc,
};
use windows_implement::implement;

use crate::clipboard::content::ClipboardFileEntry;
use crate::clipboard::virtual_file::coordinator::VirtualFileCoordinator;
use crate::clipboard::virtual_file::stream::VirtualFileStreamCore;
use crate::clipboard::win_platform::{
    clipboard_global_alloc_flags, pump_sta_messages, WinClipboardPlatform,
};

const DROPEFFECT_COPY: u32 = 1;
const E_NOTIMPL: HRESULT = HRESULT(0x80004001u32 as i32);
const CLIPBOARD_SET_RETRY_MS: u64 = 10;
const CLIPBOARD_SET_MAX_RETRIES: usize = 20;
const CLIPBOARD_CLEAR_MAX_RETRIES: usize = 100;

fn clear_ole_clipboard_after_operation() {
    unsafe {
        let _ = OleFlushClipboard();
    }
    for attempt in 0..CLIPBOARD_CLEAR_MAX_RETRIES {
        unsafe {
            if OleSetClipboard(None).is_ok() {
                pump_sta_messages();
                return;
            }
        }
        if attempt + 1 < CLIPBOARD_CLEAR_MAX_RETRIES {
            std::thread::sleep(std::time::Duration::from_millis(CLIPBOARD_SET_RETRY_MS));
            pump_sta_messages();
        }
    }
    let platform = WinClipboardPlatform::new();
    let _ = platform.clear();
    pump_sta_messages();
}

fn com_err(code: u32) -> windows::core::Error {
    windows::core::Error::from_hresult(HRESULT(code as i32))
}

#[derive(Clone)]
struct ClipboardFormats {
    file_desc: u16,
    file_content: u16,
    preferred_drop_effect: u16,
}

impl ClipboardFormats {
    fn register() -> anyhow::Result<Self> {
        unsafe {
            Ok(Self {
                file_desc: RegisterClipboardFormatW(PCWSTR(CFSTR_FILEDESCRIPTOR.as_ptr())) as u16,
                file_content: RegisterClipboardFormatW(PCWSTR(CFSTR_FILECONTENTS.as_ptr())) as u16,
                preferred_drop_effect: RegisterClipboardFormatW(PCWSTR(
                    CFSTR_PREFERREDDROPEFFECT.as_ptr(),
                )) as u16,
            })
        }
    }
}

fn build_file_group_descriptor(files: &[ClipboardFileEntry]) -> anyhow::Result<HGLOBAL> {
    let count = files.len();
    if count == 0 {
        anyhow::bail!("no virtual files");
    }
    let header_size = std::mem::size_of::<FILEGROUPDESCRIPTORW>();
    let entry_size = std::mem::size_of::<FILEDESCRIPTORW>();
    let total = header_size + entry_size * (count - 1);

    unsafe {
        let mem = GlobalAlloc(clipboard_global_alloc_flags(), total)
            .map_err(|err| anyhow::anyhow!("GlobalAlloc failed: {err}"))?;
        let ptr = GlobalLock(mem);
        if ptr.is_null() {
            anyhow::bail!("GlobalLock failed");
        }

        let group = ptr as *mut FILEGROUPDESCRIPTORW;
        (*group).cItems = count as u32;
        let fd_base = (ptr as usize + std::mem::size_of::<u32>()) as *mut FILEDESCRIPTORW;

        for (index, file) in files.iter().enumerate() {
            let fd = fd_base.add(index);
            std::ptr::write_bytes(fd, 0, 1);
            let name = file.ref_path.encode_utf16().collect::<Vec<_>>();
            const MAX_NAME: usize = 260;
            let copy_len = name.len().min(MAX_NAME - 1);
            for (i, ch) in name.iter().take(copy_len).enumerate() {
                (*fd).cFileName[i] = *ch;
            }

            let size = file.total_size.max(0) as u64;
            (*fd).dwFlags = (FD_FILESIZE.0
                | FD_ATTRIBUTES.0
                | FD_WRITESTIME.0
                | FD_CREATETIME.0
                | FD_PROGRESSUI.0) as u32;
            (*fd).nFileSizeLow = (size & 0xFFFF_FFFF) as u32;
            (*fd).nFileSizeHigh = (size >> 32) as u32;
            (*fd).dwFileAttributes = FILE_ATTRIBUTE_NORMAL.0;
            let ft = GetSystemTimeAsFileTime();
            (*fd).ftCreationTime = ft;
            (*fd).ftLastWriteTime = ft;
            (*fd).ftLastAccessTime = ft;
        }

        let _ = GlobalUnlock(mem);
        Ok(mem)
    }
}

#[implement(IStream)]
struct VirtualFileStream {
    core: Arc<VirtualFileStreamCore>,
    coordinator: Arc<VirtualFileCoordinator>,
}

impl VirtualFileStream {
    fn new(core: Arc<VirtualFileStreamCore>, coordinator: Arc<VirtualFileCoordinator>) -> Self {
        Self { core, coordinator }
    }

    fn perform_read(&self, pv: *mut c_void, cb: u32, pcbread: *mut u32) -> HRESULT {
        if pv.is_null() || pcbread.is_null() {
            return S_FALSE;
        }

        let req = match self.core.begin_read(cb) {
            Ok(req) => req,
            Err(_) => return S_FALSE,
        };

        info!(
            "virtual file IStream::Read chunk, index={}, start={}, size={}",
            req.req_index, req.req_start, req.req_size
        );
        if let Err(err) = self.coordinator.send_req_buffer(&req) {
            error!("virtual file req_buffer failed: {err:#}");
            return S_FALSE;
        }

        let mut dest = unsafe { std::slice::from_raw_parts_mut(pv as *mut u8, cb as usize) };
        let read_size = match self.core.complete_read(&mut dest) {
            Ok(size) => size,
            Err(err) => {
                warn!("virtual file IStream::Read failed: {err:?}");
                return S_FALSE;
            }
        };

        info!("virtual file IStream::Read done, bytes={read_size}");

        unsafe {
            *pcbread = read_size as u32;
        }
        S_OK
    }
}

impl ISequentialStream_Impl for VirtualFileStream_Impl {
    fn Read(&self, pv: *mut c_void, cb: u32, pcbread: *mut u32) -> HRESULT {
        self.perform_read(pv, cb, pcbread)
    }

    fn Write(&self, _: *const c_void, _: u32, _: *mut u32) -> HRESULT {
        S_OK
    }
}

impl IStream_Impl for VirtualFileStream_Impl {
    fn Seek(
        &self,
        _dlibmove: i64,
        dworigin: STREAM_SEEK,
        plibnewposition: *mut u64,
    ) -> windows::core::Result<()> {
        if dworigin == STREAM_SEEK_SET {
            self.core.reset_position();
            if !plibnewposition.is_null() {
                unsafe {
                    *plibnewposition = 0;
                }
            }
        } else if dworigin != STREAM_SEEK_CUR && dworigin != STREAM_SEEK_END {
            return Err(com_err(0x80030009)); // STG_E_INVALIDFUNCTION
        }
        Ok(())
    }

    fn SetSize(&self, _: u64) -> windows::core::Result<()> {
        Err(com_err(0x80004001))
    }

    fn CopyTo(
        &self,
        _: windows::core::Ref<'_, IStream>,
        _: u64,
        _: *mut u64,
        _: *mut u64,
    ) -> windows::core::Result<()> {
        Err(com_err(0x80004001))
    }

    fn Commit(&self, _: &STGC) -> windows::core::Result<()> {
        Err(com_err(0x80004001))
    }

    fn Revert(&self) -> windows::core::Result<()> {
        Err(com_err(0x80004001))
    }

    fn LockRegion(&self, _: u64, _: u64, _: &LOCKTYPE) -> windows::core::Result<()> {
        Err(com_err(0x80004001))
    }

    fn UnlockRegion(&self, _: u64, _: u64, _: u32) -> windows::core::Result<()> {
        Err(com_err(0x80004001))
    }

    fn Stat(&self, pstatstg: *mut STATSTG, _: &STATFLAG) -> windows::core::Result<()> {
        if pstatstg.is_null() {
            return Err(com_err(0x80070057));
        }
        let total = self.core.file().total_size.max(0) as u64;
        unsafe {
            std::ptr::write_bytes(pstatstg, 0, 1);
            (*pstatstg).r#type = STGTY_STREAM.0 as u32;
            (*pstatstg).cbSize = total;
        }
        Ok(())
    }

    fn Clone(&self) -> windows::core::Result<IStream> {
        Err(com_err(0x80004001))
    }
}

#[implement(IDataObject, IDataObjectAsyncCapability)]
struct VirtualFileDataObject {
    coordinator: Arc<VirtualFileCoordinator>,
    formats: ClipboardFormats,
    descriptor_files: Mutex<Vec<ClipboardFileEntry>>,
    in_async_op: AtomicBool,
    active_stream: Mutex<Option<IStream>>,
}

impl VirtualFileDataObject {
    fn new(coordinator: Arc<VirtualFileCoordinator>) -> anyhow::Result<IDataObject> {
        let formats = ClipboardFormats::register()?;
        let files = coordinator
            .session_files()
            .ok_or_else(|| anyhow::anyhow!("virtual file session missing"))?;
        Ok(VirtualFileDataObject {
            coordinator,
            formats,
            descriptor_files: Mutex::new(files),
            in_async_op: AtomicBool::new(false),
            active_stream: Mutex::new(None),
        }
        .into())
    }
}

impl IDataObject_Impl for VirtualFileDataObject_Impl {
    fn GetData(&self, pformatetcin: *const FORMATETC) -> windows::core::Result<STGMEDIUM> {
        if pformatetcin.is_null() {
            return Err(com_err(0x80070057));
        }
        let format = unsafe { (*pformatetcin).cfFormat };
        let tymed = unsafe { (*pformatetcin).tymed };
        debug!(
            "virtual file GetData format={} tymed={} file_desc={} file_content={} preferred={}",
            format, tymed, self.formats.file_desc, self.formats.file_content, self.formats.preferred_drop_effect
        );

        if format == self.formats.file_desc && (tymed & TYMED_HGLOBAL.0 as u32) != 0 {
            let files = self.descriptor_files.lock().expect("lock").clone();
            info!("virtual file GetData FILEDESCRIPTOR, count={}", files.len());
            let mem = build_file_group_descriptor(&files).map_err(|_| com_err(0x8007000E))?;
            return Ok(STGMEDIUM {
                tymed: TYMED_HGLOBAL.0 as u32,
                u: windows::Win32::System::Com::STGMEDIUM_0 { hGlobal: mem },
                pUnkForRelease: ManuallyDrop::new(None),
            });
        }

        if format == self.formats.file_content && (tymed & TYMED_ISTREAM.0 as u32) != 0 {
            let file_index = unsafe { (*pformatetcin).lindex };
            if file_index < 0 {
                return Err(com_err(0x80004005));
            }
            let index = file_index as u32;
            let file_count = self
                .coordinator
                .session_files()
                .map(|files| files.len() as u32)
                .unwrap_or(0);
            if index >= file_count {
                return Err(com_err(0x80004005));
            }
            info!("virtual file GetData FILECONTENTS, index={index}");
            let full_name = self
                .coordinator
                .session_files()
                .and_then(|files| files.get(index as usize).map(|file| file.full_path.clone()))
                .unwrap_or_default();
            if let Some(prev) = self.active_stream.lock().expect("lock").take() {
                drop(prev);
            }
            let stream_arc = self
                .coordinator
                .activate_stream(index)
                .ok_or_else(|| com_err(0x80004005))?;
            if !full_name.is_empty() {
                info!("virtual file req_at_begin, full_name={full_name}");
                if let Err(err) = self.coordinator.send_req_at_begin(&full_name) {
                    warn!("virtual file req_at_begin failed: {err:#}");
                }
            }
            let stream_obj: IStream =
                VirtualFileStream::new(stream_arc, self.coordinator.clone()).into();
            *self.active_stream.lock().expect("lock") = Some(stream_obj.clone());
            self.descriptor_files.lock().expect("lock").clear();
            return Ok(STGMEDIUM {
                tymed: TYMED_ISTREAM.0 as u32,
                u: windows::Win32::System::Com::STGMEDIUM_0 {
                    pstm: ManuallyDrop::new(Some(stream_obj)),
                },
                pUnkForRelease: ManuallyDrop::new(None),
            });
        }

        if format == self.formats.preferred_drop_effect && (tymed & TYMED_HGLOBAL.0 as u32) != 0 {
            unsafe {
                let mem = GlobalAlloc(clipboard_global_alloc_flags(), 4).map_err(|_| com_err(0x8007000E))?;
                let ptr = GlobalLock(mem);
                if ptr.is_null() {
                    return Err(com_err(0x8007000E));
                }
                std::ptr::write(ptr as *mut u32, DROPEFFECT_COPY);
                let _ = GlobalUnlock(mem);
                return Ok(STGMEDIUM {
                    tymed: TYMED_HGLOBAL.0 as u32,
                    u: windows::Win32::System::Com::STGMEDIUM_0 { hGlobal: mem },
                    pUnkForRelease: ManuallyDrop::new(None),
                });
            }
        }

        Err(com_err(0x80040064))
    }

    fn QueryGetData(&self, pformatetc: *const FORMATETC) -> HRESULT {
        if pformatetc.is_null() {
            return HRESULT(0x80070057u32 as i32);
        }
        let format = unsafe { (*pformatetc).cfFormat };
        debug!(
            "virtual file QueryGetData format={} file_desc={} file_content={} preferred={}",
            format, self.formats.file_desc, self.formats.file_content, self.formats.preferred_drop_effect
        );
        if format == self.formats.file_desc
            || format == self.formats.file_content
            || format == self.formats.preferred_drop_effect
        {
            S_OK
        } else {
            E_NOTIMPL
        }
    }

    fn EnumFormatEtc(&self, dwdirection: u32) -> windows::core::Result<IEnumFORMATETC> {
        if dwdirection != DATADIR_GET.0 as u32 {
            return Err(com_err(0x80004001));
        }
        debug!("virtual file EnumFormatEtc GET");
        let formats = [
            FORMATETC {
                cfFormat: self.formats.file_desc,
                ptd: std::ptr::null_mut(),
                dwAspect: DVASPECT_CONTENT.0 as u32,
                lindex: -1,
                tymed: TYMED_HGLOBAL.0 as u32,
            },
            FORMATETC {
                cfFormat: self.formats.file_content,
                ptd: std::ptr::null_mut(),
                dwAspect: DVASPECT_CONTENT.0 as u32,
                lindex: -1,
                tymed: TYMED_ISTREAM.0 as u32,
            },
            FORMATETC {
                cfFormat: self.formats.preferred_drop_effect,
                ptd: std::ptr::null_mut(),
                dwAspect: DVASPECT_CONTENT.0 as u32,
                lindex: -1,
                tymed: TYMED_HGLOBAL.0 as u32,
            },
        ];
        unsafe { SHCreateStdEnumFmtEtc(&formats) }
    }

    fn GetDataHere(
        &self,
        _: *const FORMATETC,
        _: *mut STGMEDIUM,
    ) -> windows::core::Result<()> {
        Err(com_err(0x80040064))
    }

    fn GetCanonicalFormatEtc(
        &self,
        pformatetcin: *const FORMATETC,
        pformatetcout: *mut FORMATETC,
    ) -> HRESULT {
        if pformatetcin.is_null() || pformatetcout.is_null() {
            return HRESULT(0x80070057u32 as i32);
        }
        unsafe {
            (*pformatetcout).ptd = std::ptr::null_mut();
        }
        E_NOTIMPL
    }

    fn SetData(
        &self,
        _: *const FORMATETC,
        _: *const STGMEDIUM,
        _: BOOL,
    ) -> windows::core::Result<()> {
        Err(com_err(0x80004001))
    }

    fn DAdvise(
        &self,
        _: *const FORMATETC,
        _: u32,
        _: windows::core::Ref<'_, IAdviseSink>,
    ) -> windows::core::Result<u32> {
        Err(com_err(0x80004001))
    }

    fn DUnadvise(&self, _: u32) -> windows::core::Result<()> {
        Err(com_err(0x80004001))
    }

    fn EnumDAdvise(&self) -> windows::core::Result<IEnumSTATDATA> {
        Err(com_err(0x80004001))
    }
}

impl IDataObjectAsyncCapability_Impl for VirtualFileDataObject_Impl {
    fn SetAsyncMode(&self, _fdoopasync: BOOL) -> windows::core::Result<()> {
        Ok(())
    }

    fn GetAsyncMode(&self) -> windows::core::Result<BOOL> {
        Ok(BOOL(1))
    }

    fn StartOperation(
        &self,
        _pbcreserved: windows::core::Ref<'_, windows::Win32::System::Com::IBindCtx>,
    ) -> windows::core::Result<()> {
        self.in_async_op.store(true, Ordering::SeqCst);
        info!("virtual file async StartOperation");
        Ok(())
    }

    fn InOperation(&self) -> windows::core::Result<BOOL> {
        Ok(BOOL(self.in_async_op.load(Ordering::SeqCst) as i32))
    }

    fn EndOperation(
        &self,
        hresult: HRESULT,
        _pbcreserved: windows::core::Ref<'_, windows::Win32::System::Com::IBindCtx>,
        _dweffects: u32,
    ) -> windows::core::Result<()> {
        self.in_async_op.store(false, Ordering::SeqCst);
        info!("virtual file async EndOperation, hr={hresult:?}");
        if let Some(stream) = self.active_stream.lock().expect("lock").take() {
            let full_name = self
                .coordinator
                .active_stream()
                .map(|s| s.file().full_path.clone())
                .unwrap_or_default();
            let success = hresult.is_ok()
                && self
                    .coordinator
                    .active_stream()
                    .map(|s| s.is_transfer_complete())
                    .unwrap_or(false);
            if !full_name.is_empty() {
                let _ = self.coordinator.send_req_at_end(&full_name, success);
            }
            drop(stream);
            self.coordinator.clear_active_stream();
        }
        clear_ole_clipboard_after_operation();
        self.coordinator.clear_session();
        Ok(())
    }
}

pub fn install_virtual_file_clipboard(
    coordinator: Arc<VirtualFileCoordinator>,
) -> anyhow::Result<()> {
    let platform = WinClipboardPlatform::new();
    platform
        .clear()
        .map_err(|err| anyhow::anyhow!("clear clipboard before virtual file set: {err:#}"))?;
    std::thread::sleep(std::time::Duration::from_millis(CLIPBOARD_SET_RETRY_MS));

    let data_object = VirtualFileDataObject::new(coordinator)?;
    let mut set_ok = false;
    for attempt in 0..CLIPBOARD_SET_MAX_RETRIES {
        unsafe {
            match OleSetClipboard(&data_object) {
                Ok(()) => {
                    set_ok = true;
                    break;
                }
                Err(err) => {
                    warn!(
                        "OleSetClipboard virtual file retry {}: {err}",
                        attempt + 1
                    );
                }
            }
        }
        std::thread::sleep(std::time::Duration::from_millis(CLIPBOARD_SET_RETRY_MS));
        pump_sta_messages();
    }
    if !set_ok {
        anyhow::bail!("OleSetClipboard virtual file failed after retries");
    }

    pump_sta_messages();
    info!("virtual file clipboard installed");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::clipboard::virtual_file::coordinator::{VirtualFileCoordinator, VirtualFileSession};
    use crate::clipboard::win_platform::OpenClipboardGuard;
    use crate::clipboard::win_platform::CLIPBOARD_TEST_LOCK;
    use crate::proto::StreamRoute;

    #[test]
    fn file_group_descriptor_sizes() {
        let files = vec![ClipboardFileEntry {
            file_name: "a.txt".to_string(),
            full_path: "C:/remote/a.txt".to_string(),
            ref_path: "a.txt".to_string(),
            total_size: 42,
        }];
        let mem = build_file_group_descriptor(&files).expect("descriptor");
        unsafe {
            let ptr = GlobalLock(mem);
            assert!(!ptr.is_null());
            let group = ptr as *const FILEGROUPDESCRIPTORW;
            let items = unsafe { std::ptr::read_unaligned(std::ptr::addr_of!((*group).cItems)) };
            assert_eq!(items, 1);
            let _ = GlobalUnlock(mem);
        }
    }

    #[test]
    fn install_virtual_clipboard_sets_ole() {
        let _lock = CLIPBOARD_TEST_LOCK.lock().unwrap();
        let _ = unsafe { windows::Win32::System::Ole::OleInitialize(None) };
        let coord = VirtualFileCoordinator::new();
        coord.install_session(VirtualFileSession {
            route: StreamRoute::default(),
            files: vec![ClipboardFileEntry {
                file_name: "v.txt".to_string(),
                full_path: "Z:/v.txt".to_string(),
                ref_path: "v.txt".to_string(),
                total_size: 3,
            }],
        });
        install_virtual_file_clipboard(coord).expect("install");
        unsafe {
            let _ = windows::Win32::System::Ole::OleSetClipboard(None);
            let _guard = OpenClipboardGuard::open().expect("open");
            let _ = windows::Win32::System::DataExchange::EmptyClipboard();
        }
    }
}
