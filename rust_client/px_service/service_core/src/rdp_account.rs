//! Windows standard-account provisioning only. No session logoff, user deletion or profile cleanup API is used here.

use zeroize::Zeroizing;

#[derive(Clone, PartialEq, Eq)]
pub struct RdpAccountSpec {
    pub workspace_id: String,
    pub account_name: String,
    pub password: Zeroizing<String>,
    pub credential_version: u32,
    pub expected_sid: Option<String>,
}

impl std::fmt::Debug for RdpAccountSpec {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        formatter.debug_struct("RdpAccountSpec").field("workspace_id", &self.workspace_id)
            .field("credential_version", &self.credential_version).field("password", &"<redacted>").finish_non_exhaustive()
    }
}

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub struct RdpAccountIdentity {
    pub account_name: String,
    pub sid: String,
    pub credential_version: u32,
}

impl RdpAccountSpec {
    pub fn validate(&self) -> Result<(), String> {
        if self.workspace_id.is_empty() || self.workspace_id.len() > 128
            || !self.workspace_id.bytes().all(|c| c.is_ascii_alphanumeric() || matches!(c, b'-' | b'_'))
            || !self.account_name.starts_with("grdp_") || self.account_name.len() > 20
            || self.account_name.len() < 8
            || !self.account_name.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'_')
            || self.password.len() < 32 || self.password.len() > 256
            || self.password.contains('\0') || self.credential_version == 0
            || self.expected_sid.as_ref().is_some_and(|sid| !sid.starts_with("S-1-5-21-")) {
            return Err("RDP account configuration invalid".into());
        }
        Ok(())
    }

    fn marker(&self) -> String { format!("GammaRay RDP:{}:{}", self.workspace_id, self.credential_version) }
}

#[cfg(windows)]
mod platform {
    use super::*;
    use windows::core::{PCWSTR, PWSTR};
    use windows::Win32::Foundation::{HLOCAL, LocalFree};
    use windows::Win32::NetworkManagement::NetManagement::*;
    use windows::Win32::Security::{
        CreateWellKnownSid, LookupAccountNameW, LookupAccountSidW, PSID, SID_NAME_USE,
        WELL_KNOWN_SID_TYPE, WinBuiltinAdministratorsSid, WinBuiltinRemoteDesktopUsersSid, WinBuiltinUsersSid, WinBuiltinGuestsSid,
    };
    use windows::Win32::Security::Authorization::ConvertSidToStringSidW;

    struct NetBuffer(*mut u8);
    impl Drop for NetBuffer {
        fn drop(&mut self) { if !self.0.is_null() { unsafe { NetApiBufferFree(Some(self.0.cast())); } } }
    }
    struct LocalString(PWSTR);
    impl Drop for LocalString {
        fn drop(&mut self) { if !self.0.is_null() { unsafe { LocalFree(Some(HLOCAL(self.0.0.cast()))); } } }
    }
    fn wide(text: &str) -> Vec<u16> { text.encode_utf16().chain(Some(0)).collect() }
    fn failure(operation: &str, code: u32) -> String { format!("RDP account {operation} failed (Windows code {code})") }

    fn account_sid(name: &[u16]) -> Result<(Vec<u8>, String), String> {
        let mut sid = vec![0u8; 256];
        let mut sid_len = sid.len() as u32;
        let mut domain = vec![0u16; 256];
        let mut domain_len = domain.len() as u32;
        let mut kind = SID_NAME_USE::default();
        unsafe {
            LookupAccountNameW(PCWSTR::null(), PCWSTR(name.as_ptr()), Some(PSID(sid.as_mut_ptr().cast())), &mut sid_len,
                Some(PWSTR(domain.as_mut_ptr())), &mut domain_len, &mut kind)
                .map_err(|e| failure("resolve SID", e.code().0 as u32))?;
            let mut text = LocalString(PWSTR::null());
            ConvertSidToStringSidW(PSID(sid.as_mut_ptr().cast()), &mut text.0)
                .map_err(|e| failure("format SID", e.code().0 as u32))?;
            let value = text.0.to_string().map_err(|_| "RDP SID text invalid".to_string())?;
            Ok((sid, value))
        }
    }

    fn group_name(kind: WELL_KNOWN_SID_TYPE) -> Result<Vec<u16>, String> {
        let mut sid = [0u8; 256];
        let mut sid_len = sid.len() as u32;
        let mut name = vec![0u16; 256];
        let mut name_len = name.len() as u32;
        let mut domain = [0u16; 256];
        let mut domain_len = domain.len() as u32;
        let mut use_type = SID_NAME_USE::default();
        unsafe {
            CreateWellKnownSid(kind, None, Some(PSID(sid.as_mut_ptr().cast())), &mut sid_len)
                .map_err(|e| failure("resolve group SID", e.code().0 as u32))?;
            LookupAccountSidW(PCWSTR::null(), PSID(sid.as_mut_ptr().cast()), Some(PWSTR(name.as_mut_ptr())), &mut name_len,
                Some(PWSTR(domain.as_mut_ptr())), &mut domain_len, &mut use_type)
                .map_err(|e| failure("resolve group name", e.code().0 as u32))?;
        }
        name.truncate(name_len as usize);
        name.push(0);
        Ok(name)
    }

    fn reject_administrator(name: &[u16]) -> Result<(), String> {
        let administrators = String::from_utf16_lossy(&group_name(WinBuiltinAdministratorsSid)?);
        let guests = String::from_utf16_lossy(&group_name(WinBuiltinGuestsSid)?);
        let mut buffer = NetBuffer(std::ptr::null_mut());
        let mut count = 0;
        let mut total = 0;
        let result = unsafe { NetUserGetLocalGroups(PCWSTR::null(), PCWSTR(name.as_ptr()), 0, 1,
            &mut buffer.0, u32::MAX, &mut count, &mut total) };
        if result != 0 { return Err(failure("inspect group membership", result)); }
        if count > 0 && buffer.0.is_null() { return Err("RDP group response invalid".into()); }
        for index in 0..count as usize {
            let group = unsafe { &*buffer.0.cast::<LOCALGROUP_USERS_INFO_0>().add(index) };
            let group_name = unsafe { group.lgrui0_name.to_string() }.map_err(|_| "RDP group name invalid".to_string())?;
            if group_name.eq_ignore_ascii_case(administrators.trim_end_matches('\0')) {
                return Err("RDP workspace account has administrator membership; access refused".into());
            }
            if group_name.eq_ignore_ascii_case(guests.trim_end_matches('\0')) {
                return Err("RDP workspace account has Guest membership; access refused".into());
            }
        }
        Ok(())
    }

    pub fn ensure(spec: &RdpAccountSpec) -> Result<RdpAccountIdentity, String> {
        spec.validate()?;
        let mut name = wide(&spec.account_name);
        let mut password = Zeroizing::new(wide(&spec.password));
        let mut comment = wide(&spec.marker());
        let mut existing = NetBuffer(std::ptr::null_mut());
        let status = unsafe { NetUserGetInfo(PCWSTR::null(), PCWSTR(name.as_ptr()), 1, &mut existing.0) };
        let mut applied_version = spec.credential_version;
        if status == 2221 { // NERR_UserNotFound
            if spec.expected_sid.is_some() { return Err("RDP account missing; refusing to replace its persisted SID".into()); }
            let info = USER_INFO_1 {
                usri1_name: PWSTR(name.as_mut_ptr()), usri1_password: PWSTR(password.as_mut_ptr()),
                usri1_priv: USER_PRIV_USER, usri1_comment: PWSTR(comment.as_mut_ptr()),
                usri1_flags: UF_SCRIPT | UF_DONT_EXPIRE_PASSWD,
                ..Default::default()
            };
            let status = unsafe { NetUserAdd(PCWSTR::null(), 1, (&info as *const USER_INFO_1).cast(), None) };
            if status != 0 { return Err(failure("create standard user", status)); }
        } else if status == 0 && !existing.0.is_null() {
            let info = unsafe { &*existing.0.cast::<USER_INFO_1>() };
            let marker = unsafe { info.usri1_comment.to_string() }.map_err(|_| "RDP account marker invalid".to_string())?;
            let prefix = format!("GammaRay RDP:{}:", spec.workspace_id);
            applied_version = marker.strip_prefix(&prefix).and_then(|version| version.parse::<u32>().ok())
                .filter(|version| *version > 0 && *version <= spec.credential_version)
                .ok_or_else(|| "RDP account ownership/version mismatch; refusing to modify an existing account".to_string())?;
            if info.usri1_priv == USER_PRIV_ADMIN || (info.usri1_flags & UF_ACCOUNTDISABLE).0 != 0 {
                return Err("RDP account is privileged or disabled; access refused".into());
            }
        } else { return Err(failure("inspect user", status)); }
        let (mut sid, sid_text) = account_sid(&name)?;
        if spec.expected_sid.as_ref().is_some_and(|expected| *expected != sid_text) {
            return Err("RDP account SID changed; access refused".into());
        }
        reject_administrator(&name)?;
        // NetUserAdd does not enroll a local user into BUILTIN\Users. Without
        // that membership NetUserGetInfo reports USER_PRIV_GUEST on the next
        // retry, despite creation requesting USER_PRIV_USER. Repair only after
        // the managed marker, exact SID and non-admin/non-Guest checks above.
        for kind in [WinBuiltinUsersSid, WinBuiltinRemoteDesktopUsersSid] {
            let group = group_name(kind)?;
            let member = LOCALGROUP_MEMBERS_INFO_0 { lgrmi0_sid: PSID(sid.as_mut_ptr().cast()) };
            let status = unsafe { NetLocalGroupAddMembers(PCWSTR::null(), PCWSTR(group.as_ptr()), 0,
                (&member as *const LOCALGROUP_MEMBERS_INFO_0).cast(), 1) };
            if status != 0 && status != 1378 { return Err(failure("grant standard workspace group membership", status)); }
        }
        let mut verified = NetBuffer(std::ptr::null_mut());
        let status = unsafe { NetUserGetInfo(PCWSTR::null(), PCWSTR(name.as_ptr()), 1, &mut verified.0) };
        if status != 0 || verified.0.is_null() { return Err(failure("verify standard user", status)); }
        if unsafe { &*verified.0.cast::<USER_INFO_1>() }.usri1_priv != USER_PRIV_USER {
            return Err("RDP account did not become a standard user".into());
        }
        if applied_version != spec.credential_version {
            let info = USER_INFO_1003 { usri1003_password: PWSTR(password.as_mut_ptr()) };
            let status = unsafe { NetUserSetInfo(PCWSTR::null(), PCWSTR(name.as_ptr()), 1003, (&info as *const USER_INFO_1003).cast(), None) };
            if status != 0 { return Err(failure("apply credential version", status)); }
            let info = USER_INFO_1007 { usri1007_comment: PWSTR(comment.as_mut_ptr()) };
            let status = unsafe { NetUserSetInfo(PCWSTR::null(), PCWSTR(name.as_ptr()), 1007, (&info as *const USER_INFO_1007).cast(), None) };
            if status != 0 { return Err(failure("record credential version", status)); }
        }
        Ok(RdpAccountIdentity { account_name: spec.account_name.clone(), sid: sid_text, credential_version: spec.credential_version })
    }
}

#[cfg(windows)]
pub use platform::ensure as ensure_standard_account;

#[cfg(test)]
mod tests {
    use super::*;
    fn sample() -> RdpAccountSpec {
        RdpAccountSpec { workspace_id: "workspace-1".into(), account_name: "grdp_testaccount".into(),
            password: Zeroizing::new("aA1!01234567890123456789012345678901".into()), credential_version: 1, expected_sid: None }
    }
    #[test]
    fn never_accepts_administrator_or_unmanaged_names() {
        for name in ["Administrator", "usbtest2", "grdp_a/b", "grdp_", "grdp_name;command"] {
            let mut spec = sample(); spec.account_name = name.into(); assert!(spec.validate().is_err());
        }
    }
    #[test]
    fn validates_identity_and_version_without_touching_windows() {
        let mut spec = sample(); assert!(spec.validate().is_ok());
        spec.credential_version = 0; assert!(spec.validate().is_err());
        spec.credential_version = 1; spec.expected_sid = Some("S-1-5-32-544".into()); assert!(spec.validate().is_err());
    }
}
