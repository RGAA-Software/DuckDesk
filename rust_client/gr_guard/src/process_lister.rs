use windows::Win32::Foundation::{CloseHandle, INVALID_HANDLE_VALUE};
use windows::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};

use crate::process_monitor::{ProcessEntry, ProcessLister};

#[derive(Default)]
pub struct ToolhelpProcessLister;

impl ProcessLister for ToolhelpProcessLister {
    fn list_processes(&self) -> Result<Vec<ProcessEntry>, String> {
        let snapshot = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) }
            .map_err(|err| err.to_string())?;
        if snapshot == INVALID_HANDLE_VALUE {
            return Err("CreateToolhelp32Snapshot returned invalid handle".to_string());
        }

        let mut entries = Vec::new();
        let mut process_entry = PROCESSENTRY32W::default();
        process_entry.dwSize = std::mem::size_of::<PROCESSENTRY32W>() as u32;

        let first = unsafe { Process32FirstW(snapshot, &mut process_entry) };
        if let Err(err) = first {
            unsafe {
                let _ = CloseHandle(snapshot);
            }
            return Err(format!("Process32FirstW failed: {err}"));
        }

        loop {
            entries.push(ProcessEntry {
                exe_name: utf16z_to_string(&process_entry.szExeFile),
            });
            if unsafe { Process32NextW(snapshot, &mut process_entry) }.is_err() {
                break;
            }
        }

        unsafe {
            let _ = CloseHandle(snapshot);
        }

        Ok(entries)
    }
}

fn utf16z_to_string(buffer: &[u16]) -> String {
    let end = buffer.iter().position(|value| *value == 0).unwrap_or(buffer.len());
    String::from_utf16_lossy(&buffer[..end])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn utf16z_to_string_stops_at_zero_terminator() {
        let data = ['G' as u16, 'R' as u16, 0, 'X' as u16];
        assert_eq!(utf16z_to_string(&data), "GR");
    }
}
