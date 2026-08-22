use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::fmt;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::{Duration, Instant};

pub const USBMMIDD_MAX_DRIVER_MONITORS: usize = 4;

const DEVICE_INSTALLER: &str = "deviceinstaller64.exe";
const DRIVER_INF: &str = "usbmmIdd.inf";
const HARDWARE_ID: &str = "usbmmidd";

const REQUIRED_FILES: &[(&str, &str)] = &[
    (
        "deviceinstaller64.exe",
        "E9BAA02CDAE921ACF0AAE4D8E8C29A4CDF4057AB61F9C60862B7CC439E2753F7",
    ),
    (
        "License.txt",
        "44C73FC4110979899DBB5054662DE581F7D4350939F19BB2D50574F613314BA2",
    ),
    (
        "usbmmidd.cat",
        "470149C4CF9970BA59070AA7C9409C9F63A15727DE99BAB53E7E51F55310779F",
    ),
    (
        "usbmmIdd.inf",
        "A8750CA15A86742F3012886C9932BB974158CD2D9779CF891C730D976A47726A",
    ),
    (
        "x64/usbmmIdd.dll",
        "DC135D675127113915A7E5AA9FE57C84EDAD6BE41D0890B265EF124AB26EA9E3",
    ),
];

fn is_usbmmidd_monitor_child_id(device_id: &str) -> bool {
    device_id
        .to_ascii_uppercase()
        .starts_with(r"MONITOR\DEFAULT_MONITOR\")
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MonitorSnapshot {
    pub device_name: String,
    pub width: u32,
    pub height: u32,
    pub refresh_hz: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UsbMmIddError {
    pub code: &'static str,
    pub message: String,
}

impl UsbMmIddError {
    pub(crate) fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }
}

impl fmt::Display for UsbMmIddError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for UsbMmIddError {}

pub trait UsbMmIddBackend: Send + Sync {
    fn verify_package(&self) -> Result<(), UsbMmIddError>;
    fn driver_installed(&self) -> bool;
    fn install_driver(&self) -> Result<(), UsbMmIddError>;
    fn enumerate_monitors(&self) -> Result<Vec<MonitorSnapshot>, UsbMmIddError>;
    fn add_monitor(
        &self,
        width: u32,
        height: u32,
        refresh_hz: u32,
    ) -> Result<MonitorSnapshot, UsbMmIddError>;
    fn remove_last_monitor(&self) -> Result<(), UsbMmIddError>;
}

#[derive(Debug, Clone)]
pub struct WindowsUsbMmIddBackend {
    driver_dir: PathBuf,
    operation_timeout: Duration,
}

impl WindowsUsbMmIddBackend {
    pub fn new(driver_dir: PathBuf) -> Self {
        Self {
            driver_dir,
            operation_timeout: Duration::from_secs(15),
        }
    }

    fn wait_for_monitor_count(
        &self,
        before: usize,
        adding: bool,
    ) -> Result<Vec<MonitorSnapshot>, UsbMmIddError> {
        let started = Instant::now();
        loop {
            let monitors = self.enumerate_monitors()?;
            let changed = if adding {
                monitors.len() > before
            } else {
                monitors.len() < before
            };
            if changed {
                return Ok(monitors);
            }
            if started.elapsed() >= self.operation_timeout {
                return Err(UsbMmIddError::new(
                    "TOPOLOGY_TIMEOUT",
                    format!(
                        "USBMMIDD monitor count did not {} from {} within {:?}",
                        if adding { "increase" } else { "decrease" },
                        before,
                        self.operation_timeout
                    ),
                ));
            }
            thread::sleep(Duration::from_millis(100));
        }
    }

    fn wait_for_driver(&self) -> Result<(), UsbMmIddError> {
        let started = Instant::now();
        while started.elapsed() < self.operation_timeout {
            if self.driver_installed() {
                return Ok(());
            }
            thread::sleep(Duration::from_millis(100));
        }
        Err(UsbMmIddError::new(
            "DRIVER_INSTALL_TIMEOUT",
            format!(
                "USBMMIDD device interface did not appear within {:?}",
                self.operation_timeout
            ),
        ))
    }
}

impl UsbMmIddBackend for WindowsUsbMmIddBackend {
    fn verify_package(&self) -> Result<(), UsbMmIddError> {
        for (relative, expected) in REQUIRED_FILES {
            let path = self.driver_dir.join(relative);
            let actual = sha256_file(&path)?;
            if !actual.eq_ignore_ascii_case(expected) {
                return Err(UsbMmIddError::new(
                    "DRIVER_PACKAGE_HASH_MISMATCH",
                    format!(
                        "{} SHA-256 mismatch: expected {}, got {}",
                        path.display(),
                        expected,
                        actual
                    ),
                ));
            }
        }
        Ok(())
    }

    fn driver_installed(&self) -> bool {
        #[cfg(windows)]
        unsafe {
            get_device_path(&USBMMIDD_INTERFACE_GUID).is_ok()
        }
        #[cfg(not(windows))]
        {
            false
        }
    }

    fn install_driver(&self) -> Result<(), UsbMmIddError> {
        self.verify_package()?;
        if self.driver_installed() {
            return Ok(());
        }
        #[cfg(windows)]
        {
            use std::os::windows::process::CommandExt;
            const CREATE_NO_WINDOW: u32 = 0x0800_0000;
            let installer = self.driver_dir.join(DEVICE_INSTALLER);
            let output = std::process::Command::new(&installer)
                .current_dir(&self.driver_dir)
                .args(["install", DRIVER_INF, HARDWARE_ID])
                .creation_flags(CREATE_NO_WINDOW)
                .output()
                .map_err(|err| {
                    UsbMmIddError::new(
                        "DRIVER_INSTALL_LAUNCH_FAILED",
                        format!("failed to launch {}: {err}", installer.display()),
                    )
                })?;
            if !output.status.success() {
                // The vendored deviceinstaller uses exit code 1 even when the
                // device node and driver were installed successfully.  The
                // authoritative success condition is the signed driver's
                // device interface becoming available.
                if self.wait_for_driver().is_ok() {
                    return Ok(());
                }
                return Err(UsbMmIddError::new(
                    "DRIVER_INSTALL_FAILED",
                    format!(
                        "deviceinstaller64 exited with {:?}: {}{}",
                        output.status.code(),
                        String::from_utf8_lossy(&output.stdout),
                        String::from_utf8_lossy(&output.stderr)
                    ),
                ));
            }
            self.wait_for_driver()
        }
        #[cfg(not(windows))]
        {
            Err(UsbMmIddError::new(
                "UNSUPPORTED_PLATFORM",
                "USBMMIDD is supported on Windows only",
            ))
        }
    }

    fn enumerate_monitors(&self) -> Result<Vec<MonitorSnapshot>, UsbMmIddError> {
        #[cfg(windows)]
        unsafe {
            enumerate_usbmmidd_monitors()
        }
        #[cfg(not(windows))]
        {
            Ok(Vec::new())
        }
    }

    fn add_monitor(
        &self,
        width: u32,
        height: u32,
        refresh_hz: u32,
    ) -> Result<MonitorSnapshot, UsbMmIddError> {
        self.install_driver()?;
        let before = self.enumerate_monitors()?;
        if before.len() >= USBMMIDD_MAX_DRIVER_MONITORS {
            return Err(UsbMmIddError::new(
                "DRIVER_MONITOR_LIMIT",
                format!(
                    "USBMMIDD already exposes {} monitors (driver limit {})",
                    before.len(),
                    USBMMIDD_MAX_DRIVER_MONITORS
                ),
            ));
        }
        #[cfg(windows)]
        unsafe {
            send_monitor_ioctl(true)?;
        }
        #[cfg(not(windows))]
        return Err(UsbMmIddError::new(
            "UNSUPPORTED_PLATFORM",
            "USBMMIDD is supported on Windows only",
        ));

        let after = match self.wait_for_monitor_count(before.len(), true) {
            Ok(after) => after,
            Err(err) => {
                #[cfg(windows)]
                unsafe {
                    let rollback = send_monitor_ioctl(false).and_then(|_| {
                        self.wait_for_monitor_count(before.len() + 1, false)
                            .map(|_| ())
                    });
                    return Err(UsbMmIddError::new(
                        err.code,
                        format!("{}; add rollback result: {rollback:?}", err.message),
                    ));
                }
                #[cfg(not(windows))]
                return Err(err);
            }
        };
        let before_names: HashSet<_> = before.iter().map(|m| m.device_name.as_str()).collect();
        let created_result = after
            .iter()
            .find(|m| !before_names.contains(m.device_name.as_str()))
            .cloned()
            .or_else(|| after.last().cloned())
            .ok_or_else(|| {
                UsbMmIddError::new(
                    "MONITOR_ENUMERATION_FAILED",
                    "USBMMIDD count increased but no monitor could be identified",
                )
            });
        let mut created = match created_result {
            Ok(created) => created,
            Err(err) => {
                #[cfg(windows)]
                unsafe {
                    let rollback = send_monitor_ioctl(false).and_then(|_| {
                        self.wait_for_monitor_count(before.len() + 1, false)
                            .map(|_| ())
                    });
                    return Err(UsbMmIddError::new(
                        err.code,
                        format!("{}; add rollback result: {rollback:?}", err.message),
                    ));
                }
                #[cfg(not(windows))]
                return Err(err);
            }
        };

        #[cfg(windows)]
        unsafe {
            if let Err(err) = change_resolution(&created.device_name, width, height, refresh_hz) {
                let rollback = send_monitor_ioctl(false).and_then(|_| {
                    self.wait_for_monitor_count(before.len() + 1, false)
                        .map(|_| ())
                });
                return Err(UsbMmIddError::new(
                    err.code,
                    format!("{}; add rollback result: {rollback:?}", err.message),
                ));
            }
        }
        created.width = width;
        created.height = height;
        created.refresh_hz = refresh_hz;
        Ok(created)
    }

    fn remove_last_monitor(&self) -> Result<(), UsbMmIddError> {
        let before = self.enumerate_monitors()?;
        if before.is_empty() {
            return Err(UsbMmIddError::new(
                "NO_VIRTUAL_DISPLAY",
                "USBMMIDD has no active monitor to remove",
            ));
        }
        #[cfg(windows)]
        unsafe {
            send_monitor_ioctl(false)?;
        }
        #[cfg(not(windows))]
        return Err(UsbMmIddError::new(
            "UNSUPPORTED_PLATFORM",
            "USBMMIDD is supported on Windows only",
        ));
        self.wait_for_monitor_count(before.len(), false).map(|_| ())
    }
}

fn sha256_file(path: &Path) -> Result<String, UsbMmIddError> {
    let mut file = File::open(path).map_err(|err| {
        UsbMmIddError::new(
            "DRIVER_PACKAGE_INCOMPLETE",
            format!("cannot open {}: {err}", path.display()),
        )
    })?;
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = file.read(&mut buffer).map_err(|err| {
            UsbMmIddError::new(
                "DRIVER_PACKAGE_READ_FAILED",
                format!("cannot read {}: {err}", path.display()),
            )
        })?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(format!("{:X}", hasher.finalize()))
}

#[cfg(windows)]
use winapi::shared::guiddef::GUID;

#[cfg(windows)]
const USBMMIDD_INTERFACE_GUID: GUID = GUID {
    Data1: 0xb5ffd75f,
    Data2: 0xda40,
    Data3: 0x4353,
    Data4: [0x8f, 0xf8, 0xb6, 0xda, 0xf6, 0xf1, 0xd8, 0xca],
};

#[cfg(windows)]
const USBMMIDD_MONITOR_IOCTL: u32 = 2_307_084;

#[cfg(windows)]
struct DeviceInfo(winapi::um::setupapi::HDEVINFO);

#[cfg(windows)]
impl Drop for DeviceInfo {
    fn drop(&mut self) {
        unsafe {
            winapi::um::setupapi::SetupDiDestroyDeviceInfoList(self.0);
        }
    }
}

#[cfg(windows)]
unsafe fn get_device_path(interface_guid: &GUID) -> Result<Vec<u16>, UsbMmIddError> {
    use std::io;
    use std::ptr::null_mut;
    use winapi::shared::minwindef::FALSE;
    use winapi::shared::winerror::ERROR_INSUFFICIENT_BUFFER;
    use winapi::um::handleapi::INVALID_HANDLE_VALUE;
    use winapi::um::setupapi::{
        SetupDiEnumDeviceInterfaces, SetupDiGetClassDevsW, SetupDiGetDeviceInterfaceDetailW,
        DIGCF_DEVICEINTERFACE, DIGCF_PRESENT, SP_DEVICE_INTERFACE_DATA,
        SP_DEVICE_INTERFACE_DETAIL_DATA_W,
    };

    let raw_info = SetupDiGetClassDevsW(
        interface_guid,
        null_mut(),
        null_mut(),
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE,
    );
    if raw_info == INVALID_HANDLE_VALUE {
        return Err(UsbMmIddError::new(
            "DRIVER_INTERFACE_NOT_FOUND",
            io::Error::last_os_error().to_string(),
        ));
    }
    let info = DeviceInfo(raw_info);
    let mut interface_data: SP_DEVICE_INTERFACE_DATA = std::mem::zeroed();
    interface_data.cbSize = std::mem::size_of::<SP_DEVICE_INTERFACE_DATA>() as u32;
    if SetupDiEnumDeviceInterfaces(info.0, null_mut(), interface_guid, 0, &mut interface_data)
        == FALSE
    {
        return Err(UsbMmIddError::new(
            "DRIVER_INTERFACE_NOT_FOUND",
            io::Error::last_os_error().to_string(),
        ));
    }

    let mut required = 0_u32;
    let first = SetupDiGetDeviceInterfaceDetailW(
        info.0,
        &mut interface_data,
        null_mut(),
        0,
        &mut required,
        null_mut(),
    );
    if first != FALSE
        || io::Error::last_os_error().raw_os_error() != Some(ERROR_INSUFFICIENT_BUFFER as i32)
    {
        return Err(UsbMmIddError::new(
            "DRIVER_INTERFACE_QUERY_FAILED",
            io::Error::last_os_error().to_string(),
        ));
    }

    let mut detail_buffer = vec![0_u8; required as usize];
    let detail = detail_buffer.as_mut_ptr() as *mut SP_DEVICE_INTERFACE_DETAIL_DATA_W;
    (*detail).cbSize = std::mem::size_of::<SP_DEVICE_INTERFACE_DETAIL_DATA_W>() as u32;
    if SetupDiGetDeviceInterfaceDetailW(
        info.0,
        &mut interface_data,
        detail,
        required,
        &mut required,
        null_mut(),
    ) == FALSE
    {
        return Err(UsbMmIddError::new(
            "DRIVER_INTERFACE_QUERY_FAILED",
            io::Error::last_os_error().to_string(),
        ));
    }

    let path_ptr = std::ptr::addr_of!((*detail).DevicePath) as *const u16;
    let mut path = Vec::new();
    for index in 0..(required as usize / 2) {
        let ch = *path_ptr.add(index);
        path.push(ch);
        if ch == 0 {
            return Ok(path);
        }
    }
    Err(UsbMmIddError::new(
        "DRIVER_INTERFACE_QUERY_FAILED",
        "device interface path was not NUL terminated",
    ))
}

#[cfg(windows)]
unsafe fn send_monitor_ioctl(add: bool) -> Result<(), UsbMmIddError> {
    use std::io;
    use std::ptr::null_mut;
    use winapi::shared::minwindef::FALSE;
    use winapi::shared::ntdef::NULL;
    use winapi::um::fileapi::{CreateFileW, OPEN_EXISTING};
    use winapi::um::handleapi::{CloseHandle, INVALID_HANDLE_VALUE};
    use winapi::um::ioapiset::DeviceIoControl;
    use winapi::um::winnt::{GENERIC_READ, GENERIC_WRITE};

    let path = get_device_path(&USBMMIDD_INTERFACE_GUID)?;
    let handle = CreateFileW(
        path.as_ptr(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        null_mut(),
        OPEN_EXISTING,
        0,
        null_mut(),
    );
    if handle == INVALID_HANDLE_VALUE || handle == NULL {
        return Err(UsbMmIddError::new(
            "DRIVER_OPEN_FAILED",
            io::Error::last_os_error().to_string(),
        ));
    }
    let command = [if add { 0x10 } else { 0x00 }, 0x00, 0x00, 0x00];
    let mut returned = 0_u32;
    let result = DeviceIoControl(
        handle,
        USBMMIDD_MONITOR_IOCTL,
        command.as_ptr() as *mut _,
        command.len() as u32,
        null_mut(),
        0,
        &mut returned,
        null_mut(),
    );
    let last_error = io::Error::last_os_error();
    CloseHandle(handle);
    if result == FALSE {
        return Err(UsbMmIddError::new(
            "DRIVER_IOCTL_FAILED",
            last_error.to_string(),
        ));
    }
    Ok(())
}

#[cfg(windows)]
unsafe fn enumerate_usbmmidd_monitors() -> Result<Vec<MonitorSnapshot>, UsbMmIddError> {
    use std::ptr::null_mut;
    use winapi::shared::minwindef::{BOOL, FALSE, LPARAM, TRUE};
    use winapi::shared::windef::{HDC, HMONITOR, LPRECT};
    use winapi::um::wingdi::{
        DEVMODEW, DISPLAY_DEVICEW, DISPLAY_DEVICE_ACTIVE, DISPLAY_DEVICE_MIRRORING_DRIVER,
    };
    use winapi::um::winuser::{
        EnumDisplayDevicesW, EnumDisplayMonitors, EnumDisplaySettingsExW, GetMonitorInfoW,
        ENUM_CURRENT_SETTINGS, MONITORINFO, MONITORINFOEXW,
    };

    unsafe extern "system" fn collect_monitor_name(
        monitor: HMONITOR,
        _dc: HDC,
        _rect: LPRECT,
        data: LPARAM,
    ) -> BOOL {
        let names = &mut *(data as *mut Vec<Vec<u16>>);
        let mut info: MONITORINFOEXW = std::mem::zeroed();
        info.cbSize = std::mem::size_of::<MONITORINFOEXW>() as u32;
        if GetMonitorInfoW(
            monitor,
            &mut info as *mut MONITORINFOEXW as *mut MONITORINFO,
        ) == FALSE
        {
            return TRUE;
        }
        names.push(info.szDevice.to_vec());
        TRUE
    }

    let mut result = Vec::new();
    let mut active_names: Vec<Vec<u16>> = Vec::new();
    if EnumDisplayMonitors(
        null_mut(),
        null_mut(),
        Some(collect_monitor_name),
        &mut active_names as *mut Vec<Vec<u16>> as LPARAM,
    ) == FALSE
    {
        return Err(UsbMmIddError::new(
            "MONITOR_ENUMERATION_FAILED",
            std::io::Error::last_os_error().to_string(),
        ));
    }

    for device_name_wide in active_names {
        let mut child: DISPLAY_DEVICEW = std::mem::zeroed();
        child.cb = std::mem::size_of::<DISPLAY_DEVICEW>() as u32;
        if EnumDisplayDevicesW(device_name_wide.as_ptr(), 0, &mut child, 0) == FALSE {
            continue;
        }
        if child.StateFlags & DISPLAY_DEVICE_ACTIVE == 0
            || child.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER != 0
        {
            continue;
        }
        let child_id = wide_array_to_string(&child.DeviceID);
        // USBMMIDD v2 outputs are exposed to the desktop as active Generic
        // Non-PnP monitors. On some NVIDIA systems the top-level adapter
        // enumeration is empty after the topology settles, while querying the
        // active DISPLAYn child remains stable. Existing matching displays are
        // accounted for as the foreign baseline by VirtualDisplayManager.
        if !is_usbmmidd_monitor_child_id(&child_id) {
            continue;
        }
        let device_name = wide_array_to_string(&device_name_wide);
        let mut mode: DEVMODEW = std::mem::zeroed();
        mode.dmSize = std::mem::size_of::<DEVMODEW>() as u16;
        if EnumDisplaySettingsExW(
            device_name_wide.as_ptr(),
            ENUM_CURRENT_SETTINGS,
            &mut mode,
            0,
        ) == FALSE
        {
            continue;
        }
        result.push(MonitorSnapshot {
            device_name,
            width: mode.dmPelsWidth,
            height: mode.dmPelsHeight,
            refresh_hz: mode.dmDisplayFrequency,
        });
    }
    result.sort_by(|a, b| a.device_name.cmp(&b.device_name));
    Ok(result)
}

#[cfg(windows)]
fn wide_array_to_string(value: &[u16]) -> String {
    let end = value.iter().position(|ch| *ch == 0).unwrap_or(value.len());
    String::from_utf16_lossy(&value[..end])
}

#[cfg(windows)]
unsafe fn change_resolution(
    device_name: &str,
    width: u32,
    height: u32,
    refresh_hz: u32,
) -> Result<(), UsbMmIddError> {
    use std::os::windows::ffi::OsStrExt;
    use std::ptr::null_mut;
    use winapi::um::wingdi::{DEVMODEW, DM_DISPLAYFREQUENCY, DM_PELSHEIGHT, DM_PELSWIDTH};
    use winapi::um::winuser::{
        ChangeDisplaySettingsExW, CDS_GLOBAL, CDS_RESET, CDS_UPDATEREGISTRY, DISP_CHANGE_SUCCESSFUL,
    };

    let wide_name: Vec<u16> = std::ffi::OsStr::new(device_name)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let mut mode: DEVMODEW = std::mem::zeroed();
    mode.dmSize = std::mem::size_of::<DEVMODEW>() as u16;
    mode.dmPelsWidth = width;
    mode.dmPelsHeight = height;
    mode.dmDisplayFrequency = refresh_hz;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    let result = ChangeDisplaySettingsExW(
        wide_name.as_ptr(),
        &mut mode,
        null_mut(),
        CDS_UPDATEREGISTRY | CDS_GLOBAL | CDS_RESET,
        null_mut(),
    );
    if result != DISP_CHANGE_SUCCESSFUL {
        return Err(UsbMmIddError::new(
            "SET_DISPLAY_MODE_FAILED",
            format!(
                "ChangeDisplaySettingsExW({}, {}x{}@{}) returned {}",
                device_name, width, height, refresh_hz, result
            ),
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recognizes_usbmmidd_default_monitor_child_id_case_insensitively() {
        assert!(is_usbmmidd_monitor_child_id(
            r"MONITOR\Default_Monitor\{4d36e96e-e325-11ce-bfc1-08002be10318}\0042"
        ));
        assert!(!is_usbmmidd_monitor_child_id(
            r"MONITOR\XMD009A\{4d36e96e-e325-11ce-bfc1-08002be10318}\0034"
        ));
    }

    #[test]
    fn vendored_package_matches_pinned_hashes() {
        let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../..")
            .join("third_party")
            .join("usbmmidd_v2");
        WindowsUsbMmIddBackend::new(root).verify_package().unwrap();
    }
}
