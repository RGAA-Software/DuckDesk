use std::ffi::CString;

use windows::core::PCSTR;
use windows::Win32::Foundation::HMODULE;
use windows::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryA};

pub trait SystemActions: Send + Sync {
    fn send_ctrl_alt_delete(&self) -> Result<(), String>;
}

#[derive(Default)]
pub struct WindowsActions;

impl WindowsActions {
    pub fn new() -> Self {
        Self
    }
}

impl SystemActions for WindowsActions {
    fn send_ctrl_alt_delete(&self) -> Result<(), String> {
        unsafe {
            let lib_name = CString::new("sas.dll").map_err(|err| err.to_string())?;
            let module: HMODULE = LoadLibraryA(PCSTR(lib_name.as_ptr() as _))
                .map_err(|err| format!("LoadLibraryA sas.dll failed: {err}"))?;
            let proc_name = CString::new("SendSAS").map_err(|err| err.to_string())?;
            let proc = GetProcAddress(module, PCSTR(proc_name.as_ptr() as _))
                .ok_or_else(|| "GetProcAddress SendSAS failed".to_string())?;
            let func: extern "system" fn(i32) = std::mem::transmute(proc);
            func(0);
            Ok(())
        }
    }
}
