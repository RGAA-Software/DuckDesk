use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::HashSet;
use std::fmt;
use std::fs::File;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, ExitStatus, Stdio};
use std::thread;
use std::time::{Duration, Instant};

pub const PARSEC_VDD_MAX_CONTROLLER_MONITORS: usize = 8;

const DEVICE_INSTALLER: &str = "nefconw.exe";
const DRIVER_INF: &str = "driver/mm.inf";
const HARDWARE_ID: &str = r"Root\Parsec\VDA";
const DISPLAY_CLASS_GUID: &str = "4D36E968-E325-11CE-BFC1-08002BE10318";
const CONTROLLER_EXE: &str = "px_display.exe";
const CREATE_NO_WINDOW: u32 = 0x0800_0000;
const TOPOLOGY_QUIET_PERIOD: Duration = Duration::from_millis(1200);
const ADD_RETRY_DELAY: Duration = Duration::from_millis(750);
const ADD_MAX_ATTEMPTS: usize = 4;

const REQUIRED_FILES: &[(&str, &str)] = &[
    (
        "nefconw.exe",
        "CF746D1B0BBB713993D4A90DCCD774C78D9FFF8C2BA5A054B6C8F56C77E1EEE1",
    ),
    (
        "driver/mm.cat",
        "136E64AC07DCE5A3B4935D5A9C5CFE03983C0B3065F46A30A45536D5B1681D5C",
    ),
    (
        "driver/mm.dll",
        "96DB6AE2F950B56E52BE3E68F92893AFA94645EAE09FEA2ABD5DD1985758150A",
    ),
    (
        "driver/mm.inf",
        "34DA9FF45C13577631F67E33D11B8A26E3D22CA685D00C388B6122A795800588",
    ),
];

fn is_parsec_vdd_display_device(device_description: &str) -> bool {
    device_description
        .trim_end_matches('\0')
        .eq_ignore_ascii_case("Parsec Virtual Display Adapter")
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MonitorSnapshot {
    pub device_name: String,
    pub width: u32,
    pub height: u32,
    pub refresh_hz: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ParsecVddError {
    pub code: &'static str,
    pub message: String,
}

impl ParsecVddError {
    pub(crate) fn new(code: &'static str, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
        }
    }
}

impl fmt::Display for ParsecVddError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: {}", self.code, self.message)
    }
}

impl std::error::Error for ParsecVddError {}

pub trait VirtualDisplayBackend: Send + Sync {
    fn verify_package(&self) -> Result<(), ParsecVddError>;
    fn driver_installed(&self) -> bool;
    fn install_driver(&self) -> Result<(), ParsecVddError>;
    fn enumerate_monitors(&self) -> Result<Vec<MonitorSnapshot>, ParsecVddError>;
    fn add_monitor(
        &self,
        width: u32,
        height: u32,
        refresh_hz: u32,
    ) -> Result<MonitorSnapshot, ParsecVddError>;
    fn remove_last_monitor(&self) -> Result<(), ParsecVddError>;
}

#[derive(Debug, Clone)]
pub struct WindowsParsecVddBackend {
    driver_dir: PathBuf,
    controller_exe: PathBuf,
    operation_timeout: Duration,
}

impl WindowsParsecVddBackend {
    pub fn new(driver_dir: PathBuf) -> Self {
        let controller_exe = driver_dir
            .parent()
            .unwrap_or_else(|| Path::new("."))
            .join(CONTROLLER_EXE);
        Self {
            driver_dir,
            controller_exe,
            operation_timeout: Duration::from_secs(15),
        }
    }

    fn wait_for_monitor_count(
        &self,
        before: usize,
        adding: bool,
    ) -> Result<Vec<MonitorSnapshot>, ParsecVddError> {
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
                return Err(ParsecVddError::new(
                    "PARSEC_VDD_TOPOLOGY_TIMEOUT",
                    format!(
                        "Parsec VDD monitor count did not {} from {} within {:?}",
                        if adding { "increase" } else { "decrease" },
                        before,
                        self.operation_timeout
                    ),
                ));
            }
            thread::sleep(Duration::from_millis(100));
        }
    }

    fn wait_for_stable_monitor_count(
        &self,
        expected: usize,
    ) -> Result<Vec<MonitorSnapshot>, ParsecVddError> {
        let started = Instant::now();
        let mut stable_since = None;
        loop {
            let monitors = self.enumerate_monitors()?;
            if monitors.len() == expected {
                let since = stable_since.get_or_insert_with(Instant::now);
                if since.elapsed() >= TOPOLOGY_QUIET_PERIOD {
                    return Ok(monitors);
                }
            } else {
                stable_since = None;
            }
            if started.elapsed() >= self.operation_timeout {
                return Err(ParsecVddError::new(
                    "PARSEC_VDD_TOPOLOGY_SETTLE_TIMEOUT",
                    format!(
                        "Parsec VDD monitor count did not remain at {expected} for {:?}",
                        TOPOLOGY_QUIET_PERIOD
                    ),
                ));
            }
            thread::sleep(Duration::from_millis(100));
        }
    }

    fn wait_for_driver(&self) -> Result<(), ParsecVddError> {
        let started = Instant::now();
        while started.elapsed() < self.operation_timeout {
            if self.driver_installed() {
                return Ok(());
            }
            thread::sleep(Duration::from_millis(100));
        }
        Err(ParsecVddError::new(
            "PARSEC_VDD_INSTALL_TIMEOUT",
            format!(
                "Parsec VDD device interface did not appear within {:?}",
                self.operation_timeout
            ),
        ))
    }

    #[cfg(windows)]
    fn run_nefconw(&self, args: &[&str]) -> Result<ExitStatus, ParsecVddError> {
        use std::os::windows::process::CommandExt;
        let installer = self.driver_dir.join(DEVICE_INSTALLER);
        Command::new(&installer)
            .current_dir(&self.driver_dir)
            .args(args)
            .creation_flags(CREATE_NO_WINDOW)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map_err(|err| {
                ParsecVddError::new(
                    "PARSEC_VDD_INSTALL_LAUNCH_FAILED",
                    format!("failed to launch {}: {err}", installer.display()),
                )
            })
    }

    #[cfg(windows)]
    fn run_controller_cli(&self, args: &[String]) -> Result<ExitStatus, ParsecVddError> {
        use std::os::windows::process::CommandExt;
        Command::new(&self.controller_exe)
            .current_dir(
                self.controller_exe
                    .parent()
                    .unwrap_or_else(|| Path::new(".")),
            )
            .arg("-cli")
            .args(args)
            .creation_flags(CREATE_NO_WINDOW)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .map_err(|err| {
                ParsecVddError::new(
                    "PARSEC_VDD_CONTROLLER_COMMAND_FAILED",
                    format!("failed to launch {}: {err}", self.controller_exe.display()),
                )
            })
    }

    #[cfg(windows)]
    fn ensure_controller(&self) -> Result<(), ParsecVddError> {
        use std::os::windows::process::CommandExt;
        if !self.controller_exe.is_file() {
            return Err(ParsecVddError::new(
                "PARSEC_VDD_CONTROLLER_NOT_FOUND",
                format!("{} is missing", self.controller_exe.display()),
            ));
        }
        let ready = self
            .run_controller_cli(&["ready".to_string()])
            .map(|status| status.code() == Some(0))
            .unwrap_or(false);
        if !ready {
            Command::new(&self.controller_exe)
                .current_dir(
                    self.controller_exe
                        .parent()
                        .unwrap_or_else(|| Path::new(".")),
                )
                .arg("-worker")
                .creation_flags(CREATE_NO_WINDOW)
                .stdin(Stdio::null())
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .spawn()
                .map_err(|err| {
                    ParsecVddError::new(
                        "PARSEC_VDD_CONTROLLER_START_FAILED",
                        format!("failed to start {}: {err}", self.controller_exe.display()),
                    )
                })?;
        }
        let started = Instant::now();
        while started.elapsed() < Duration::from_secs(10) {
            if self.run_controller_cli(&["ready".to_string()])?.code() == Some(0) {
                return Ok(());
            }
            thread::sleep(Duration::from_millis(200));
        }
        Err(ParsecVddError::new(
            "PARSEC_VDD_CONTROLLER_START_TIMEOUT",
            "px_display.exe did not become ready within 10 seconds",
        ))
    }
}

impl VirtualDisplayBackend for WindowsParsecVddBackend {
    fn verify_package(&self) -> Result<(), ParsecVddError> {
        for (relative, expected) in REQUIRED_FILES {
            let path = self.driver_dir.join(relative);
            let actual = sha256_file(&path)?;
            if !actual.eq_ignore_ascii_case(expected) {
                return Err(ParsecVddError::new(
                    "PARSEC_VDD_PACKAGE_HASH_MISMATCH",
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
            get_device_path(&PARSEC_VDD_INTERFACE_GUID).is_ok()
        }
        #[cfg(not(windows))]
        {
            false
        }
    }

    fn install_driver(&self) -> Result<(), ParsecVddError> {
        self.verify_package()?;
        if self.driver_installed() {
            return Ok(());
        }
        #[cfg(windows)]
        {
            let create = self.run_nefconw(&[
                "--create-device-node",
                "--class-name",
                "Display",
                "--class-guid",
                DISPLAY_CLASS_GUID,
                "--hardware-id",
                HARDWARE_ID,
            ])?;
            if !create.success() {
                return Err(ParsecVddError::new(
                    "PARSEC_VDD_CREATE_DEVICE_FAILED",
                    format!("nefconw create-device-node exited with {:?}", create.code()),
                ));
            }
            let inf_path = self.driver_dir.join(DRIVER_INF);
            let install = self.run_nefconw(&[
                "--install-driver",
                "--inf-path",
                inf_path.to_string_lossy().as_ref(),
            ])?;
            if !install.success() && self.wait_for_driver().is_err() {
                return Err(ParsecVddError::new(
                    "PARSEC_VDD_INSTALL_FAILED",
                    format!("nefconw install-driver exited with {:?}", install.code()),
                ));
            }
            self.wait_for_driver()
        }
        #[cfg(not(windows))]
        {
            Err(ParsecVddError::new(
                "UNSUPPORTED_PLATFORM",
                "Parsec VDD is supported on Windows only",
            ))
        }
    }

    fn enumerate_monitors(&self) -> Result<Vec<MonitorSnapshot>, ParsecVddError> {
        #[cfg(windows)]
        unsafe {
            enumerate_parsec_vdd_monitors()
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
    ) -> Result<MonitorSnapshot, ParsecVddError> {
        if !self.driver_installed() {
            return Err(ParsecVddError::new(
                "PARSEC_VDD_NOT_INSTALLED",
                "Parsec VDD is not installed",
            ));
        }
        let before = self.enumerate_monitors()?;
        if before.len() >= PARSEC_VDD_MAX_CONTROLLER_MONITORS {
            return Err(ParsecVddError::new(
                "DRIVER_MONITOR_LIMIT",
                format!(
                    "Parsec VDD already exposes {} monitors (driver limit {})",
                    before.len(),
                    PARSEC_VDD_MAX_CONTROLLER_MONITORS
                ),
            ));
        }
        #[cfg(windows)]
        {
            self.ensure_controller()?;
            let mut driver_index = None;
            let mut last_exit_code = None;
            for attempt in 1..=ADD_MAX_ATTEMPTS {
                let status = self.run_controller_cli(&["add".to_string()])?;
                last_exit_code = status.code();
                if let Some(index) = last_exit_code
                    .filter(|index| (0..PARSEC_VDD_MAX_CONTROLLER_MONITORS as i32).contains(index))
                {
                    driver_index = Some(index);
                    break;
                }

                // A failed command may still have changed driver topology. In
                // that case the manager's post-error reconciliation must own
                // the result; retrying here could create a duplicate screen.
                if self.enumerate_monitors()?.len() != before.len() {
                    return Err(ParsecVddError::new(
                        "PARSEC_VDD_ADD_RESULT_UNCERTAIN",
                        format!(
                            "px_display add exited with {:?} and topology changed",
                            last_exit_code
                        ),
                    ));
                }
                if attempt < ADD_MAX_ATTEMPTS {
                    thread::sleep(ADD_RETRY_DELAY);
                }
            }
            let driver_index = driver_index.ok_or_else(|| {
                ParsecVddError::new(
                    "PARSEC_VDD_ADD_FAILED",
                    format!(
                        "px_display add failed after {ADD_MAX_ATTEMPTS} attempts; last exit {:?}",
                        last_exit_code
                    ),
                )
            })?;
            let after = self.wait_for_monitor_count(before.len(), true)?;
            let before_names: HashSet<_> = before.iter().map(|m| m.device_name.as_str()).collect();
            let mut created = after
                .iter()
                .find(|m| !before_names.contains(m.device_name.as_str()))
                .cloned()
                .or_else(|| after.last().cloned())
                .ok_or_else(|| {
                    ParsecVddError::new(
                        "MONITOR_ENUMERATION_FAILED",
                        "Parsec VDD count increased but no monitor could be identified",
                    )
                })?;
            let mode = format!("{}x{}r{}", width, height, refresh_hz);
            let set_status = self.run_controller_cli(&[
                "set".to_string(),
                driver_index.to_string(),
                mode.clone(),
            ])?;
            if set_status.code() != Some(0) {
                let _ = self.run_controller_cli(&["remove".to_string(), driver_index.to_string()]);
                return Err(ParsecVddError::new(
                    "PARSEC_VDD_MODE_SET_FAILED",
                    format!(
                        "px_display set {driver_index} {mode} exited with {:?}",
                        set_status.code()
                    ),
                ));
            }
            created.width = width;
            created.height = height;
            created.refresh_hz = refresh_hz;
            Ok(created)
        }
        #[cfg(not(windows))]
        {
            Err(ParsecVddError::new(
                "UNSUPPORTED_PLATFORM",
                "Parsec VDD is supported on Windows only",
            ))
        }
    }

    fn remove_last_monitor(&self) -> Result<(), ParsecVddError> {
        let before = self.enumerate_monitors()?;
        if before.is_empty() {
            return Err(ParsecVddError::new(
                "NO_VIRTUAL_DISPLAY",
                "Parsec VDD has no active monitor to remove",
            ));
        }
        #[cfg(windows)]
        {
            self.ensure_controller()?;
            let status = self.run_controller_cli(&["remove".to_string()])?;
            if status.code() != Some(0) {
                return Err(ParsecVddError::new(
                    "PARSEC_VDD_REMOVE_FAILED",
                    format!("px_display remove exited with {:?}", status.code()),
                ));
            }
            let after = self.wait_for_monitor_count(before.len(), false)?;
            self.wait_for_stable_monitor_count(after.len()).map(|_| ())
        }
        #[cfg(not(windows))]
        {
            Err(ParsecVddError::new(
                "UNSUPPORTED_PLATFORM",
                "Parsec VDD is supported on Windows only",
            ))
        }
    }
}

fn sha256_file(path: &Path) -> Result<String, ParsecVddError> {
    let mut file = File::open(path).map_err(|err| {
        ParsecVddError::new(
            "PARSEC_VDD_PACKAGE_INCOMPLETE",
            format!("cannot open {}: {err}", path.display()),
        )
    })?;
    let mut hasher = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = file.read(&mut buffer).map_err(|err| {
            ParsecVddError::new(
                "PARSEC_VDD_PACKAGE_READ_FAILED",
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
const PARSEC_VDD_INTERFACE_GUID: GUID = GUID {
    Data1: 0x00b41627,
    Data2: 0x04c4,
    Data3: 0x429e,
    Data4: [0xa2, 0x6e, 0x02, 0x65, 0xcf, 0x50, 0xc8, 0xfa],
};

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
unsafe fn get_device_path(interface_guid: &GUID) -> Result<Vec<u16>, ParsecVddError> {
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
        return Err(ParsecVddError::new(
            "PARSEC_VDD_INTERFACE_NOT_FOUND",
            io::Error::last_os_error().to_string(),
        ));
    }
    let info = DeviceInfo(raw_info);
    let mut interface_data: SP_DEVICE_INTERFACE_DATA = std::mem::zeroed();
    interface_data.cbSize = std::mem::size_of::<SP_DEVICE_INTERFACE_DATA>() as u32;
    if SetupDiEnumDeviceInterfaces(info.0, null_mut(), interface_guid, 0, &mut interface_data)
        == FALSE
    {
        return Err(ParsecVddError::new(
            "PARSEC_VDD_INTERFACE_NOT_FOUND",
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
        return Err(ParsecVddError::new(
            "PARSEC_VDD_INTERFACE_QUERY_FAILED",
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
        return Err(ParsecVddError::new(
            "PARSEC_VDD_INTERFACE_QUERY_FAILED",
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
    Err(ParsecVddError::new(
        "PARSEC_VDD_INTERFACE_QUERY_FAILED",
        "device interface path was not NUL terminated",
    ))
}

#[cfg(windows)]
unsafe fn enumerate_parsec_vdd_monitors() -> Result<Vec<MonitorSnapshot>, ParsecVddError> {
    use std::ptr::null_mut;
    use winapi::shared::minwindef::FALSE;
    use winapi::um::wingdi::{
        DEVMODEW, DISPLAY_DEVICEW, DISPLAY_DEVICE_ACTIVE, DISPLAY_DEVICE_MIRRORING_DRIVER,
    };
    use winapi::um::winuser::{EnumDisplayDevicesW, EnumDisplaySettingsExW, ENUM_CURRENT_SETTINGS};
    let mut result = Vec::new();
    let mut index = 0_u32;
    loop {
        let mut device: DISPLAY_DEVICEW = std::mem::zeroed();
        device.cb = std::mem::size_of::<DISPLAY_DEVICEW>() as u32;
        if EnumDisplayDevicesW(null_mut(), index, &mut device, 0) == FALSE {
            break;
        }
        index += 1;
        if device.StateFlags & DISPLAY_DEVICE_ACTIVE == 0
            || device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER != 0
        {
            continue;
        }
        if !is_parsec_vdd_display_device(&wide_array_to_string(&device.DeviceString)) {
            continue;
        }
        let mut mode: DEVMODEW = std::mem::zeroed();
        mode.dmSize = std::mem::size_of::<DEVMODEW>() as u16;
        if EnumDisplaySettingsExW(
            device.DeviceName.as_ptr(),
            ENUM_CURRENT_SETTINGS,
            &mut mode,
            0,
        ) == FALSE
        {
            continue;
        }
        result.push(MonitorSnapshot {
            device_name: wide_array_to_string(&device.DeviceName),
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn recognizes_parsec_vdd_adapter_case_insensitively() {
        assert!(is_parsec_vdd_display_device(
            "Parsec Virtual Display Adapter\0"
        ));
        assert!(is_parsec_vdd_display_device(
            "parsec virtual display adapter"
        ));
        assert!(!is_parsec_vdd_display_device("NVIDIA GeForce RTX 4090"));
    }

    #[test]
    fn vendored_package_matches_pinned_hashes() {
        let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../..")
            .join("third_party")
            .join("parsec_vdd");
        WindowsParsecVddBackend::new(root).verify_package().unwrap();
    }

    #[test]
    fn product_controller_path_is_adjacent_to_driver_directory() {
        let backend = WindowsParsecVddBackend::new(PathBuf::from(r"C:\Pixels\parsec_vdd"));
        assert_eq!(
            backend.controller_exe,
            PathBuf::from(r"C:\Pixels\px_display.exe")
        );
    }
}
