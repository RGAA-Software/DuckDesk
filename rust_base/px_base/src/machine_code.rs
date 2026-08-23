//! Stable machine code derived from real hardware identifiers.
//!
//! Ported from gopico-pc (`gopico-pc-core/src/license/machine_code.rs`) so the
//! Console server reports the same `xxxx-xxxx` machine code scheme.
//!
//! Factors (best effort — every factor that can be collected is mixed in):
//! 1. Physical NIC MAC addresses (WMI `Get-NetAdapter -Physical`; fallback: sysinfo NICs).
//! 2. Physical disk serial numbers (WMI `Win32_DiskDrive`; fallback: sysinfo disk name+size).
//! 3. CPU description (vendor / brand / core count).
//!
//! Deliberately NO OS-level ids (MachineGuid etc.) — the code is bound to the
//! physical hardware only, so reinstalling the OS does not invalidate licenses.
//!
//! The final code is a deterministic `xxxx-xxxx` 8-digit decimal code derived
//! from the MD5 digest of a canonical, order-insensitive serialization of the
//! normalized factors. Any factor change produces a different code, so a
//! license stays bound to the machine it was issued for. Network/IP changes
//! do NOT affect the code.
//!
//! The hashing logic (`MachineFactors::machine_code`) is a pure function so it
//! can be unit-tested without touching real hardware.

use digest::Digest;
use md5::Md5;

/// Length of the generated machine code (`xxxx-xxxx`).
pub const MACHINE_CODE_LEN: usize = 9;

/// Raw hardware/OS factors used to derive the machine code.
///
/// Values are stored as collected (not normalized); normalization happens in
/// [`MachineFactors::machine_code`] so that casing / separators / ordering of
/// the source data never affect the resulting code.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct MachineFactors {
    /// MAC addresses of (preferably physical) network adapters. May be empty.
    pub macs: Vec<String>,
    /// Serial numbers of physical disks. May be empty.
    pub disk_serials: Vec<String>,
    /// CPU description "{vendor}-{brand}-{core_count}".
    pub cpu_desc: String,
}

impl MachineFactors {
    /// Collect factors from the current machine (spawns WMI/PowerShell on Windows).
    pub fn collect() -> Self {
        Self {
            macs: collect_macs(),
            disk_serials: collect_disk_serials(),
            cpu_desc: collect_cpu_desc(),
        }
    }

    /// True when at least one identifying factor was collected.
    pub fn has_any(&self) -> bool {
        self.macs.iter().any(|m| !m.trim().is_empty())
            || self.disk_serials.iter().any(|s| !s.trim().is_empty())
            || !self.cpu_desc.trim().is_empty()
    }

    /// Pure: derive the stable `xxxx-xxxx` 8-digit machine code from the factors.
    pub fn machine_code(&self) -> String {
        let canonical = format!(
            "v3|mac={}|disk={}|cpu={}",
            normalize_list(&self.macs, normalize_mac).join(","),
            normalize_list(&self.disk_serials, normalize_id).join(","),
            self.cpu_desc.trim().to_lowercase(),
        );
        let digest = Md5::digest(canonical.as_bytes());
        let bytes: [u8; 8] = digest[..8].try_into().expect("MD5 digest is 16 bytes");
        short_code_from_u64(u64::from_be_bytes(bytes))
    }
}

fn short_code_from_u64(n: u64) -> String {
    let m = n % 100_000_000;
    format!("{:04}-{:04}", m / 10_000, m % 10_000)
}

/// Generate the machine code for this machine (`xxxx-xxxx`, 8 digits).
pub fn generate_machine_code() -> String {
    MachineFactors::collect().machine_code()
}

/// Collect factors for diagnostics/tooling.
pub fn inspect_machine_factors() -> MachineFactors {
    MachineFactors::collect()
}

// ---------------------------------------------------------------------------
// Normalization (pure functions)
// ---------------------------------------------------------------------------

/// Normalize an identifier: trim + uppercase (ids are case-insensitive).
fn normalize_id(raw: &str) -> String {
    raw.trim().to_uppercase()
}

/// Normalize a MAC address to 12 uppercase hex chars without separators.
/// Non-hex input that does not reduce to 12 hex chars is kept (trimmed/uppercased).
fn normalize_mac(raw: &str) -> String {
    let hexed: String = raw
        .chars()
        .filter(|c| c.is_ascii_hexdigit())
        .collect::<String>()
        .to_uppercase();
    if hexed.len() == 12 {
        hexed
    } else {
        raw.trim().to_uppercase()
    }
}

/// Normalize a list of factors: normalize each, drop blanks and all-zero MACs,
/// sort and dedup so collection order never affects the result.
fn normalize_list(items: &[String], normalize: fn(&str) -> String) -> Vec<String> {
    let mut out: Vec<String> = items
        .iter()
        .map(|s| normalize(s))
        .filter(|s| !s.is_empty() && s != "000000000000")
        .collect();
    out.sort();
    out.dedup();
    out
}

// ---------------------------------------------------------------------------
// Factor collection (IO)
// ---------------------------------------------------------------------------

fn collect_macs() -> Vec<String> {
    #[cfg(windows)]
    {
        // Prefer physical adapters only (skips VPN/VM/loopback virtual NICs).
        if let Some(out) =
            run_powershell("Get-NetAdapter -Physical | ForEach-Object { $_.MacAddress }")
        {
            let macs: Vec<String> = out
                .lines()
                .map(|l| l.trim().to_string())
                .filter(|l| !l.is_empty())
                .collect();
            if !macs.is_empty() {
                return macs;
            }
        }
    }
    #[cfg(not(windows))]
    {
        let mut macs = Vec::new();
        if let Ok(entries) = std::fs::read_dir("/sys/class/net") {
            for entry in entries.flatten() {
                if entry.file_name() == "lo" {
                    continue;
                }
                if let Ok(addr) = std::fs::read_to_string(entry.path().join("address")) {
                    let addr = addr.trim().to_string();
                    if !addr.is_empty() {
                        macs.push(addr);
                    }
                }
            }
        }
        if !macs.is_empty() {
            return macs;
        }
    }
    sysinfo_macs()
}

/// Fallback: all non-loopback MACs visible to sysinfo.
fn sysinfo_macs() -> Vec<String> {
    let networks = sysinfo::Networks::new_with_refreshed_list();
    networks
        .list()
        .iter()
        .filter(|(name, _)| !name.to_lowercase().contains("loopback"))
        .map(|(_, data)| format!("{}", data.mac_address()))
        .filter(|m| !m.is_empty())
        .collect()
}

fn collect_disk_serials() -> Vec<String> {
    #[cfg(windows)]
    {
        if let Some(out) =
            run_powershell("Get-CimInstance Win32_DiskDrive | ForEach-Object { $_.SerialNumber }")
        {
            let serials: Vec<String> = out
                .lines()
                .map(|l| l.trim().to_string())
                .filter(|l| !l.is_empty())
                .collect();
            if !serials.is_empty() {
                return serials;
            }
        }
    }
    #[cfg(not(windows))]
    {
        if let Ok(out) = std::process::Command::new("lsblk")
            .args(["-dn", "-o", "SERIAL"])
            .output()
        {
            if out.status.success() {
                let serials: Vec<String> = String::from_utf8_lossy(&out.stdout)
                    .lines()
                    .map(|l| l.trim().to_string())
                    .filter(|l| !l.is_empty())
                    .collect();
                if !serials.is_empty() {
                    return serials;
                }
            }
        }
    }
    // Last-resort fallback: disk name + size (less stable than a real serial,
    // but still hardware-derived and better than nothing).
    sysinfo::Disks::new_with_refreshed_list()
        .list()
        .iter()
        .map(|d| format!("{}:{}", d.name().to_string_lossy(), d.total_space()))
        .collect()
}

fn collect_cpu_desc() -> String {
    let mut system = sysinfo::System::new();
    system.refresh_cpu_all();
    if let Some(cpu) = system.cpus().first() {
        format!(
            "{}-{}-{}",
            cpu.vendor_id(),
            cpu.brand(),
            system.cpus().len()
        )
    } else {
        String::new()
    }
}

/// Run a PowerShell snippet, returning stdout on success.
/// Uses CREATE_NO_WINDOW so the GUI app never flashes a console.
#[cfg(windows)]
fn run_powershell(script: &str) -> Option<String> {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    let output = std::process::Command::new("powershell.exe")
        .args([
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            script,
        ])
        .creation_flags(CREATE_NO_WINDOW)
        .output()
        .ok()?;
    if !output.status.success() {
        return None;
    }
    String::from_utf8(output.stdout).ok()
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_factors() -> MachineFactors {
        MachineFactors {
            macs: vec!["AA-BB-CC-DD-EE-FF".into(), "11-22-33-44-55-66".into()],
            disk_serials: vec!["S3YJNB0K123456".into(), "WD-WX41A1234567".into()],
            cpu_desc: "GenuineIntel-Intel(R) Core(TM) i7-12700-20".into(),
        }
    }

    fn assert_short_code(code: &str) {
        assert_eq!(code.len(), MACHINE_CODE_LEN, "code: {code}");
        assert!(
            code.chars().enumerate().all(|(i, c)| {
                if i == 4 {
                    c == '-'
                } else {
                    c.is_ascii_digit()
                }
            }),
            "must be dddd-dddd: {code}"
        );
    }

    // ----- pure hashing behavior -----

    #[test]
    fn code_is_xdigit_hyphen_format() {
        assert_short_code(&sample_factors().machine_code());
    }

    #[test]
    fn code_is_deterministic_for_same_factors() {
        assert_eq!(
            sample_factors().machine_code(),
            sample_factors().machine_code()
        );
    }

    #[test]
    fn changing_macs_changes_code() {
        let mut f = sample_factors();
        let before = f.machine_code();
        f.macs = vec!["DE-AD-BE-EF-00-01".into()];
        assert_ne!(before, f.machine_code());
    }

    #[test]
    fn adding_a_mac_changes_code() {
        let mut f = sample_factors();
        let before = f.machine_code();
        f.macs.push("77-88-99-AA-BB-CC".into());
        assert_ne!(before, f.machine_code());
    }

    #[test]
    fn changing_disk_serials_changes_code() {
        let mut f = sample_factors();
        let before = f.machine_code();
        f.disk_serials = vec!["OTHER-SERIAL-999".into()];
        assert_ne!(before, f.machine_code());
    }

    #[test]
    fn changing_cpu_desc_changes_code() {
        let mut f = sample_factors();
        let before = f.machine_code();
        f.cpu_desc = "AuthenticAMD-AMD Ryzen 9 7950X-32".into();
        assert_ne!(before, f.machine_code());
    }

    #[test]
    fn mac_order_does_not_matter() {
        let mut f = sample_factors();
        let before = f.machine_code();
        f.macs.reverse();
        assert_eq!(before, f.machine_code());
    }

    #[test]
    fn disk_serial_order_does_not_matter() {
        let mut f = sample_factors();
        let before = f.machine_code();
        f.disk_serials.reverse();
        assert_eq!(before, f.machine_code());
    }

    #[test]
    fn mac_separators_and_case_are_normalized() {
        let a = MachineFactors {
            macs: vec!["aa-bb-cc-dd-ee-ff".into()],
            ..Default::default()
        }
        .machine_code();
        let b = MachineFactors {
            macs: vec!["AA:BB:CC:DD:EE:FF".into()],
            ..Default::default()
        }
        .machine_code();
        let c = MachineFactors {
            macs: vec!["AABBCCDDEEFF".into()],
            ..Default::default()
        }
        .machine_code();
        assert_eq!(a, b);
        assert_eq!(b, c);
    }

    #[test]
    fn duplicate_factors_are_deduped() {
        let mut f = sample_factors();
        let before = f.machine_code();
        f.macs.push("aa-bb-cc-dd-ee-ff".into()); // dup of existing, different case
        f.disk_serials.push("s3yjnb0k123456 ".into()); // dup w/ case+space
        assert_eq!(before, f.machine_code());
    }

    #[test]
    fn blank_and_zero_factors_are_ignored() {
        let mut f = sample_factors();
        let before = f.machine_code();
        f.macs.push("".into());
        f.macs.push("   ".into());
        f.macs.push("00:00:00:00:00:00".into());
        f.disk_serials.push("".into());
        assert_eq!(before, f.machine_code());
    }

    #[test]
    fn disk_serial_case_and_whitespace_normalized() {
        let a = MachineFactors {
            disk_serials: vec!["ABC-123".into()],
            ..Default::default()
        }
        .machine_code();
        let b = MachineFactors {
            disk_serials: vec!["  abc-123 ".into()],
            ..Default::default()
        }
        .machine_code();
        assert_eq!(a, b);
    }

    #[test]
    fn empty_factors_still_produce_valid_code() {
        let code = MachineFactors::default().machine_code();
        assert_short_code(&code);
        // ... and it is deterministic / different from a real factor set.
        assert_eq!(code, MachineFactors::default().machine_code());
        assert_ne!(code, sample_factors().machine_code());
    }

    #[test]
    fn has_any_reflects_factor_presence() {
        assert!(!MachineFactors::default().has_any());
        assert!(sample_factors().has_any());
        assert!(MachineFactors {
            disk_serials: vec!["x".into()],
            ..Default::default()
        }
        .has_any());
        assert!(!MachineFactors {
            macs: vec!["  ".into()],
            ..Default::default()
        }
        .has_any());
    }

    // ----- normalization helpers -----

    #[test]
    fn normalize_mac_handles_common_formats() {
        assert_eq!(normalize_mac("AA-BB-CC-DD-EE-FF"), "AABBCCDDEEFF");
        assert_eq!(normalize_mac("aa:bb:cc:dd:ee:ff"), "AABBCCDDEEFF");
        assert_eq!(normalize_mac("aabbccddeeff"), "AABBCCDDEEFF");
        // Garbage that does not reduce to 12 hex chars is kept (trimmed/upper).
        assert_eq!(normalize_mac(" weird "), "WEIRD");
    }

    #[test]
    fn normalize_list_sorts_dedups_and_drops_zero_mac() {
        let items = vec![
            "bb-bb-bb-bb-bb-bb".to_string(),
            "AA-AA-AA-AA-AA-AA".to_string(),
            "aa:aa:aa:aa:aa:aa".to_string(),
            "00-00-00-00-00-00".to_string(),
            "".to_string(),
        ];
        assert_eq!(
            normalize_list(&items, normalize_mac),
            vec!["AAAAAAAAAAAA".to_string(), "BBBBBBBBBBBB".to_string()]
        );
    }

    // ----- real machine collection (IO) -----

    #[test]
    fn collect_returns_real_factors() {
        let f = MachineFactors::collect();
        assert!(f.has_any(), "expected at least one hardware factor");
        #[cfg(windows)]
        {
            assert!(!f.macs.is_empty(), "expected at least one NIC MAC");
            for mac in &f.macs {
                let hexed: String = mac.chars().filter(|c| c.is_ascii_hexdigit()).collect();
                assert_eq!(hexed.len(), 12, "bad MAC: {mac}");
            }
            assert!(
                !f.disk_serials.is_empty(),
                "expected at least one disk serial"
            );
        }
    }

    #[test]
    fn collect_is_stable_across_calls() {
        let a = MachineFactors::collect().machine_code();
        let b = MachineFactors::collect().machine_code();
        assert_eq!(a, b, "machine code must be stable across collections");
    }

    #[test]
    fn generate_machine_code_matches_collect() {
        let code = generate_machine_code();
        assert_short_code(&code);
        assert_eq!(code, MachineFactors::collect().machine_code());
    }
}
