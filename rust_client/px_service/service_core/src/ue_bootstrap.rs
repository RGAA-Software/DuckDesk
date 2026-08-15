//! UE bootstrap launcher resolution.
//!
//! Unreal Engine packaged games ship a tiny bootstrap exe at the top level
//! (e.g. `VehicleGame.exe`) that merely execs the real renderer process
//! (e.g. `CarGame\Binaries\Win64\VehicleGame-Win64-Shipping.exe`). The
//! bootstrap carries the real exe's relative path in its resources:
//! `RT_RCDATA` id 201 = view exe path, id 202 = base launch arguments
//! (see D:\dolit\streamer `ue_resource_parser.cc` for the same trick).
//!
//! We resolve the view path so the hook targets the real game process while
//! the boot (launcher) process is still launched normally. Non-UE exes have
//! no such resource and simply return `None` (caller falls back to the
//! configured path, behavior unchanged).

use std::path::{Path, PathBuf};

use windows::core::PCWSTR;
use windows::Win32::Foundation::FreeLibrary;
use windows::Win32::System::LibraryLoader::{
    FindResourceW, LoadLibraryExW, LoadResource, LockResource, SizeofResource,
    DONT_RESOLVE_DLL_REFERENCES, LOAD_LIBRARY_AS_DATAFILE,
};

/// RT_RCDATA = MAKEINTRESOURCEW(10); defined locally to avoid pulling in an
/// extra windows-crate feature just for one constant.
const RT_RCDATA: PCWSTR = PCWSTR(10u16 as _);

const IDI_EXEC_FILE: u16 = 201;
const IDI_EXEC_ARGS: u16 = 202;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UeViewInfo {
    /// Absolute path of the real game (view) exe resolved from resource 201.
    pub view_path: PathBuf,
    /// Base launch arguments from resource 202, if present/readable.
    pub base_args: Option<String>,
}

/// Read a resource string (wide, not guaranteed NUL-terminated) by integer id.
/// Returns None when the resource does not exist or cannot be read.
unsafe fn read_resource_string(
    module: windows::Win32::Foundation::HMODULE,
    id: u16,
) -> Option<String> {
    let name = PCWSTR(id as _);
    let hrsrc = unsafe { FindResourceW(Some(module), name, RT_RCDATA) };
    if hrsrc.0.is_null() {
        return None;
    }
    let hglobal = unsafe { LoadResource(Some(module), hrsrc) }.ok()?;
    let size = unsafe { SizeofResource(Some(module), hrsrc) } as usize;
    if size == 0 || size % 2 != 0 {
        return None;
    }
    let data = unsafe { LockResource(hglobal) } as *const u16;
    if data.is_null() {
        return None;
    }
    let words = size / 2;
    let slice = unsafe { std::slice::from_raw_parts(data, words) };
    // Resource data is not guaranteed NUL-terminated: trim trailing NULs.
    let end = slice
        .iter()
        .rposition(|&c| c != 0)
        .map(|i| i + 1)
        .unwrap_or(0);
    if end == 0 {
        return None;
    }
    String::from_utf16(&slice[..end]).ok()
}

/// Strip the `\\?\` verbatim prefix `Path::canonicalize` adds on Windows.
fn strip_verbatim_prefix(path: &Path) -> PathBuf {
    let s = path.to_string_lossy();
    match s.strip_prefix(r"\\?\") {
        Some(rest) => PathBuf::from(rest),
        None => path.to_path_buf(),
    }
}

/// Resolve the UE view (real game) exe for a bootstrap launcher exe.
/// Returns `None` for non-UE exes (no resource 201), unreadable resources,
/// or a resolved path that fails existence validation — the caller treats
/// all of these as "not a UE bootstrap" and keeps the configured path.
pub fn resolve_ue_bootstrap(exe_path: &Path) -> Option<UeViewInfo> {
    let wide: Vec<u16> = exe_path
        .to_string_lossy()
        .encode_utf16()
        .chain(Some(0))
        .collect();
    // LOAD_LIBRARY_AS_DATAFILE: read resources only, never run the exe's code.
    let module = unsafe {
        LoadLibraryExW(
            PCWSTR(wide.as_ptr()),
            None,
            LOAD_LIBRARY_AS_DATAFILE | DONT_RESOLVE_DLL_REFERENCES,
        )
    }
    .ok()?;
    let result = (|| {
        let rel = unsafe { read_resource_string(module, IDI_EXEC_FILE) }?;
        let base_args = unsafe { read_resource_string(module, IDI_EXEC_ARGS) };
        let boot_dir = exe_path.parent()?;
        let view_path = boot_dir.join(rel.trim());
        // The path comes from the exe's own resource; validate it resolves to
        // a real file (canonicalize collapses any '..' segments first).
        let canonical = view_path.canonicalize().ok()?;
        if !canonical.is_file() {
            return None;
        }
        // canonicalize 在 Windows 上返回 \\?\ 前缀的 verbatim 路径，与
        // QueryFullProcessImageNameW 的普通路径比较前必须剥掉。
        let view_path = strip_verbatim_prefix(&canonical);
        Some(UeViewInfo {
            view_path,
            base_args: base_args.filter(|a| !a.trim().is_empty()),
        })
    })();
    unsafe {
        let _ = FreeLibrary(module);
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn non_ue_exe_returns_none() {
        // Any exe without RT_RCDATA 201 must not be mistaken for a UE
        // bootstrap; notepad is guaranteed to exist on Windows.
        let notepad = Path::new(r"C:\Windows\System32\notepad.exe");
        if notepad.is_file() {
            assert!(resolve_ue_bootstrap(notepad).is_none());
        }
    }

    #[test]
    fn missing_exe_returns_none() {
        assert!(resolve_ue_bootstrap(Path::new(r"D:\no\such\file.exe")).is_none());
    }

    #[test]
    fn cargo_test_exe_itself_is_not_a_ue_bootstrap() {
        // Our own test binary exists and is a valid PE, but has no resource 201.
        let current = std::env::current_exe().unwrap();
        assert!(resolve_ue_bootstrap(&current).is_none());
    }

    /// Manual/integration check against a real UE bootstrap launcher:
    /// `UE_BOOT_EXE=D:\path\to\Launcher.exe cargo test -p service_core real_ue -- --ignored`
    #[test]
    #[ignore]
    fn real_ue_bootstrap_resolves_view_path() {
        let exe = std::env::var("UE_BOOT_EXE").expect("set UE_BOOT_EXE");
        let info = resolve_ue_bootstrap(Path::new(&exe)).expect("must resolve");
        assert!(info.view_path.is_file());
        println!("view={:?} args={:?}", info.view_path, info.base_args);
    }
}
