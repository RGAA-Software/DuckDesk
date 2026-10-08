//! CPU, memory and disk probes have no GPU, WMI or driver-library dependency.

use crate::hardware_probe::{CpuSnapshot, DiskSnapshot, MemorySnapshot};
use std::time::Duration;
use windows::core::PCWSTR;
use windows::Win32::{
    Foundation::FILETIME,
    Storage::FileSystem::{GetDiskFreeSpaceExW, GetDriveTypeW, GetLogicalDrives},
    System::{
        SystemInformation::{GlobalMemoryStatusEx, MEMORYSTATUSEX},
        Threading::{GetActiveProcessorCount, GetSystemTimes},
    },
};

pub(crate) fn sample_cpu() -> Result<CpuSnapshot, String> {
    let logical_processors = u16::try_from(unsafe { GetActiveProcessorCount(0xffff) })
        .ok()
        .filter(|count| *count > 0 && *count <= 1024)
        .ok_or("logical processor count unavailable")?;
    // GetSystemTimes covers only the calling processor group on systems with >64 processors.
    // Keep the full inventory count but never mislabel a single-group load as whole-machine load.
    if logical_processors > 64 {
        eprintln!(
            "CPU utilization unavailable: native system-times API covers one processor group"
        );
        return Ok(CpuSnapshot {
            logical_processors,
            utilization_per_mille: None,
        });
    }
    let first = cpu_times()?;
    std::thread::sleep(Duration::from_millis(150));
    let second = cpu_times()?;
    let total_delta = second.1.checked_sub(first.1);
    let idle_delta = second.0.checked_sub(first.0);
    let utilization_per_mille = total_delta.zip(idle_delta).and_then(|(total, idle)| {
        if total == 0 || idle > total {
            return None;
        }
        u16::try_from((u128::from(total - idle) * 1000) / u128::from(total)).ok()
    });
    Ok(CpuSnapshot {
        logical_processors,
        utilization_per_mille,
    })
}

fn cpu_times() -> Result<(u64, u64), String> {
    let mut idle = FILETIME::default();
    let mut kernel = FILETIME::default();
    let mut user = FILETIME::default();
    unsafe { GetSystemTimes(Some(&mut idle), Some(&mut kernel), Some(&mut user)) }
        .map_err(|error| error.to_string())?;
    let ticks =
        |time: FILETIME| (u64::from(time.dwHighDateTime) << 32) | u64::from(time.dwLowDateTime);
    Ok((
        ticks(idle),
        ticks(kernel)
            .checked_add(ticks(user))
            .ok_or("CPU counter overflow")?,
    ))
}

pub(crate) fn sample_memory() -> Result<MemorySnapshot, String> {
    let mut status = MEMORYSTATUSEX {
        dwLength: std::mem::size_of::<MEMORYSTATUSEX>() as u32,
        ..Default::default()
    };
    unsafe { GlobalMemoryStatusEx(&mut status) }.map_err(|error| error.to_string())?;
    if status.ullTotalPhys == 0 || status.ullAvailPhys > status.ullTotalPhys {
        return Err("physical memory counters invalid".into());
    }
    Ok(MemorySnapshot {
        total_bytes: status.ullTotalPhys,
        available_bytes: status.ullAvailPhys,
    })
}

pub(crate) fn sample_disk() -> Result<DiskSnapshot, String> {
    let drive_mask = unsafe { GetLogicalDrives() };
    if drive_mask == 0 {
        return Err("logical drive enumeration failed".into());
    }
    let mut disk_snapshot = DiskSnapshot {
        total_bytes: 0,
        free_bytes: 0,
    };
    for drive_index in 0..26 {
        if drive_mask & (1 << drive_index) == 0 {
            continue;
        }
        let root = [
            u16::from(b'A') + drive_index,
            u16::from(b':'),
            u16::from(b'\\'),
            0,
        ];
        if unsafe { GetDriveTypeW(PCWSTR(root.as_ptr())) } != 3 {
            continue;
        }
        let mut total_bytes = 0;
        let mut free_bytes = 0;
        unsafe {
            GetDiskFreeSpaceExW(
                PCWSTR(root.as_ptr()),
                None,
                Some(&mut total_bytes),
                Some(&mut free_bytes),
            )
        }
        .map_err(|error| error.to_string())?;
        disk_snapshot.total_bytes = disk_snapshot
            .total_bytes
            .checked_add(total_bytes)
            .ok_or("disk total overflow")?;
        disk_snapshot.free_bytes = disk_snapshot
            .free_bytes
            .checked_add(free_bytes)
            .ok_or("disk free overflow")?;
    }
    if disk_snapshot.total_bytes == 0 || disk_snapshot.free_bytes > disk_snapshot.total_bytes {
        return Err("local disk counters unavailable".into());
    }
    Ok(disk_snapshot)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn native_probes_report_machine_fields_without_loading_gpu_or_wmi() {
        assert!(sample_cpu().unwrap().logical_processors > 0);
        let memory = sample_memory().unwrap();
        assert!(memory.total_bytes > 0 && memory.available_bytes <= memory.total_bytes);
        let disk = sample_disk().unwrap();
        assert!(disk.total_bytes > 0 && disk.free_bytes <= disk.total_bytes);
    }
}
