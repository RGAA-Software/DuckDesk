use std::ffi::OsStr;
use std::iter;
use std::os::windows::ffi::OsStrExt;

use windows::core::PCWSTR;
use windows::Win32::Foundation::{CloseHandle, ERROR_ALREADY_EXISTS, GetLastError, HANDLE};
use windows::Win32::System::Threading::CreateMutexW;

pub struct SingleInstanceGuard {
    handle: HANDLE,
}

impl SingleInstanceGuard {
    pub fn acquire(name: &str) -> Result<Self, String> {
        let wide_name: Vec<u16> = OsStr::new(name)
            .encode_wide()
            .chain(iter::once(0))
            .collect();
        let handle = unsafe { CreateMutexW(None, false, PCWSTR(wide_name.as_ptr())) }
            .map_err(|err| format!("CreateMutexW failed: {err}"))?;
        let last_error = unsafe { GetLastError() };
        if last_error == ERROR_ALREADY_EXISTS {
            unsafe {
                let _ = CloseHandle(handle);
            }
            return Err(format!("instance already exists: {name}"));
        }
        Ok(Self { handle })
    }
}

impl Drop for SingleInstanceGuard {
    fn drop(&mut self) {
        unsafe {
            let _ = CloseHandle(self.handle);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn unique_name() -> String {
        let ticks = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("clock")
            .as_nanos();
        format!("GammaRayUserProxy.Test.{ticks}")
    }

    #[test]
    fn first_acquire_succeeds() {
        let _guard = SingleInstanceGuard::acquire(&unique_name()).expect("first acquire");
    }

    #[test]
    fn second_acquire_fails_for_same_name() {
        let name = unique_name();
        let _guard = SingleInstanceGuard::acquire(&name).expect("first acquire");
        let second = SingleInstanceGuard::acquire(&name);
        assert!(second.is_err());
    }

    #[test]
    fn lock_is_released_after_drop() {
        let name = unique_name();
        {
            let _guard = SingleInstanceGuard::acquire(&name).expect("first acquire");
        }
        let second = SingleInstanceGuard::acquire(&name);
        assert!(second.is_ok());
    }
}
