//! Persistent workspace identity is independent from Render lifetime. This module
//! never removes an account/profile or logs off a Windows session.

use crate::rdp_account::{RdpAccountIdentity, RdpAccountSpec};
use serde::{Deserialize, Serialize};
use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct WorkspaceIdentity {
    pub schema: u32,
    pub workspace_id: String,
    pub app_id: String,
    pub node_id: String,
    pub device_id: String,
    pub account: RdpAccountIdentity,
}

pub fn valid_identifier(value: &str) -> bool {
    !value.is_empty() && value.len() <= 128
        && value.bytes().all(|c| c.is_ascii_alphanumeric() || matches!(c, b'-' | b'_'))
}

impl WorkspaceIdentity {
    fn check_binding(&self, spec: &RdpAccountSpec, app_id: &str, node_id: &str, device_id: &str) -> Result<(), String> {
        if self.schema != 1 || self.workspace_id != spec.workspace_id || self.app_id != app_id
            || self.node_id != node_id || self.device_id != device_id || self.account.account_name != spec.account_name
            || self.account.credential_version == 0 || self.account.credential_version > spec.credential_version
            || !self.account.sid.starts_with("S-1-5-21-") {
            return Err("RDP persisted workspace identity/version mismatch".into());
        }
        Ok(())
    }
}

/// Constructor requires an installer-controlled parent directory. Windows creates
/// a protected admin/SYSTEM-only child atomically and verifies an existing ACL;
/// it never silently repairs a modified ACL or follows a reparse-point root.
pub struct WorkspaceStore {
    root: PathBuf,
}

impl WorkspaceStore {
    #[cfg(windows)]
    pub fn open(root: &Path) -> Result<Self, String> {
        platform::ensure_private_directory(root)?;
        Ok(Self { root: root.to_path_buf() })
    }

    pub fn root(&self) -> &Path { &self.root }

    #[cfg(windows)]
    pub fn provision(&self, spec: &RdpAccountSpec, app_id: &str, node_id: &str, device_id: &str) -> Result<WorkspaceIdentity, String> {
        self.provision_with(spec, app_id, node_id, device_id, crate::rdp_account::ensure_standard_account)
    }

    fn provision_with<F>(&self, spec: &RdpAccountSpec, app_id: &str, node_id: &str, device_id: &str, provision: F) -> Result<WorkspaceIdentity, String>
    where F: FnOnce(&RdpAccountSpec) -> Result<RdpAccountIdentity, String> {
        spec.validate()?;
        if ![app_id, node_id, device_id].into_iter().all(valid_identifier) {
            return Err("RDP workspace binding invalid".into());
        }
        // Serialize account maintenance across Service processes. Render has a
        // separate runtime lease: neither lock is a persisted PID heuristic.
        let mut options = OpenOptions::new();
        options.read(true).write(true).create(true).truncate(false);
        #[cfg(windows)] {
            use std::os::windows::fs::OpenOptionsExt;
            options.share_mode(0).custom_flags(0x00200000); // FILE_FLAG_OPEN_REPARSE_POINT
        }
        let lock = options.open(self.root.join(format!("{}.account.lock", spec.workspace_id)))
            .map_err(|_| "RDP workspace account is busy or its lock is inaccessible".to_string())?;
        reject_links(&lock)?;
        let identity_path = self.root.join(format!("{}.identity.json", spec.workspace_id));
        let previous = read_identity(&identity_path)?;
        let mut checked_spec = spec.clone();
        if let Some(previous) = previous.as_ref() {
            previous.check_binding(spec, app_id, node_id, device_id)?;
            if spec.expected_sid.as_ref().is_some_and(|sid| *sid != previous.account.sid) {
                return Err("RDP requested SID conflicts with persisted workspace".into());
            }
            checked_spec.expected_sid = Some(previous.account.sid.clone());
        }
        let account = provision(&checked_spec)?;
        if account.account_name != spec.account_name || account.credential_version != spec.credential_version
            || !account.sid.starts_with("S-1-5-21-")
            || checked_spec.expected_sid.as_ref().is_some_and(|sid| *sid != account.sid) {
            return Err("RDP account adapter returned a different identity".into());
        }
        let identity = WorkspaceIdentity { schema: 1, workspace_id: spec.workspace_id.clone(), app_id: app_id.into(),
            node_id: node_id.into(), device_id: device_id.into(), account };
        if previous.as_ref() != Some(&identity) {
            let bytes = serde_json::to_vec(&identity).map_err(|_| "RDP identity serialization failed".to_string())?;
            atomic_replace(&identity_path, &bytes)?;
        }
        drop(lock);
        Ok(identity)
    }
}

fn reject_links(file: &File) -> Result<(), String> {
    let metadata = file.metadata().map_err(|_| "RDP workspace file metadata unavailable".to_string())?;
    #[cfg(windows)] {
        use std::os::windows::fs::MetadataExt;
        if metadata.file_attributes() & 0x400 != 0 { return Err("RDP workspace reparse point refused".into()); }
    }
    if !metadata.is_file() { return Err("RDP workspace entry is not a regular file".into()); }
    Ok(())
}

fn read_identity(path: &Path) -> Result<Option<WorkspaceIdentity>, String> {
    let mut options = OpenOptions::new();
    options.read(true);
    #[cfg(windows)] {
        use std::os::windows::fs::OpenOptionsExt;
        options.custom_flags(0x00200000);
    }
    let file = match options.open(path) {
        Ok(file) => file,
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => return Ok(None),
        Err(_) => return Err("RDP persisted identity inaccessible".into()),
    };
    reject_links(&file)?;
    let mut bytes = Vec::new();
    file.take(16 * 1024 + 1).read_to_end(&mut bytes).map_err(|_| "RDP identity read failed".to_string())?;
    if bytes.len() > 16 * 1024 { return Err("RDP identity exceeds limit".into()); }
    serde_json::from_slice(&bytes).map(Some).map_err(|_| "RDP persisted identity damaged; refusing account replacement".into())
}

fn atomic_replace(path: &Path, bytes: &[u8]) -> Result<(), String> {
    let nonce = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map_err(|_| "RDP clock invalid".to_string())?.as_nanos();
    let temporary = path.with_extension(format!("{}.{}.pending", std::process::id(), nonce));
    let mut file = OpenOptions::new().write(true).create_new(true).open(&temporary)
        .map_err(|_| "RDP identity staging failed".to_string())?;
    let result = file.write_all(bytes).and_then(|_| file.sync_all()).map_err(|_| "RDP identity flush failed".to_string());
    drop(file);
    let result = result.and_then(|_| {
        #[cfg(windows)] { platform::replace_file(&temporary, path) }
        #[cfg(not(windows))] { std::fs::rename(&temporary, path).map_err(|_| "RDP identity replace failed".into()) }
    });
    if result.is_err() { let _ = std::fs::remove_file(&temporary); }
    result
}

/// Public certificate identity and private proxy configuration are bound to the
/// same runtime. Only the Service token that seals the file can unseal it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RdpBootstrapBinding {
    pub workspace_id: String,
    pub instance_id: String,
    pub node_id: String,
    pub device_id: String,
    pub proxy_port: u16,
    pub target_certificate_sha256: String,
    pub proxy_certificate_sha256: String,
}

impl RdpBootstrapBinding {
    pub fn entropy(&self) -> Result<Vec<u8>, String> {
        if ![self.workspace_id.as_str(), self.instance_id.as_str(), self.node_id.as_str(), self.device_id.as_str()]
            .into_iter().all(valid_identifier) || self.proxy_port == 0
            || ![self.target_certificate_sha256.as_str(), self.proxy_certificate_sha256.as_str()]
                .into_iter().all(|pin| pin.len() == 64 && pin.bytes().all(|byte| byte.is_ascii_hexdigit())) {
            return Err("RDP bootstrap identity invalid".into());
        }
        Ok(format!("GammaRay.RdpBootstrap.v1|{}|{}|{}|{}|{}|{}|{}", self.workspace_id, self.instance_id,
            self.node_id, self.device_id, self.proxy_port, self.target_certificate_sha256, self.proxy_certificate_sha256).into_bytes())
    }
}

impl WorkspaceStore {
    #[cfg(windows)]
    pub fn stage_bootstrap(&self, binding: &RdpBootstrapBinding, private_config: &[u8]) -> Result<PathBuf, String> {
        if private_config.is_empty() || private_config.len() > 64 * 1024 { return Err("RDP proxy configuration exceeds limit".into()); }
        let sealed = platform::seal(private_config, &binding.entropy()?)?;
        let path = self.root.join(format!("{}.bootstrap", binding.instance_id));
        let mut file = OpenOptions::new().write(true).create_new(true).open(&path)
            .map_err(|_| "RDP bootstrap already exists or is inaccessible".to_string())?;
        let result = file.write_all(&sealed).and_then(|_| file.sync_all());
        drop(file);
        if result.is_err() {
            let _ = std::fs::remove_file(&path);
            return Err("RDP encrypted bootstrap write failed".into());
        }
        Ok(path)
    }
}

#[cfg(windows)]
mod platform {
    use super::*;
    use std::os::windows::ffi::OsStrExt;
    use windows::core::PCWSTR;
    use windows::Win32::Foundation::{HLOCAL, LocalFree};
    use windows::Win32::Security::{ACL, DACL_SECURITY_INFORMATION, GetSecurityDescriptorDacl, PSECURITY_DESCRIPTOR, SECURITY_ATTRIBUTES};
    use windows::Win32::Security::Authorization::{ConvertStringSecurityDescriptorToSecurityDescriptorW, GetNamedSecurityInfoW, SE_FILE_OBJECT};
    use windows::Win32::Storage::FileSystem::{CreateDirectoryW, MoveFileExW, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH};
    use windows::Win32::Security::Cryptography::{CryptProtectData, CRYPT_INTEGER_BLOB, CRYPTPROTECT_UI_FORBIDDEN};

    struct ProtectedBlob(CRYPT_INTEGER_BLOB);
    impl Drop for ProtectedBlob {
        fn drop(&mut self) {
            if !self.0.pbData.is_null() {
                unsafe {
                    zeroize::Zeroize::zeroize(std::slice::from_raw_parts_mut(self.0.pbData, self.0.cbData as usize));
                    LocalFree(Some(HLOCAL(self.0.pbData.cast())));
                }
            }
        }
    }

    pub fn seal(bytes: &[u8], entropy: &[u8]) -> Result<Vec<u8>, String> {
        let input = CRYPT_INTEGER_BLOB { cbData: bytes.len() as u32, pbData: bytes.as_ptr().cast_mut() };
        let entropy = CRYPT_INTEGER_BLOB { cbData: entropy.len() as u32, pbData: entropy.as_ptr().cast_mut() };
        let mut output = ProtectedBlob(CRYPT_INTEGER_BLOB::default());
        // Deliberately no CRYPTPROTECT_LOCAL_MACHINE: other local users must not decrypt this payload.
        unsafe { CryptProtectData(&input, PCWSTR::null(), Some(&entropy), None, None, CRYPTPROTECT_UI_FORBIDDEN, &mut output.0) }
            .map_err(|_| "RDP Service-token DPAPI encryption failed".to_string())?;
        if output.0.pbData.is_null() || output.0.cbData == 0 { return Err("RDP DPAPI returned no ciphertext".into()); }
        Ok(unsafe { std::slice::from_raw_parts(output.0.pbData, output.0.cbData as usize) }.to_vec())
    }

    struct Descriptor(PSECURITY_DESCRIPTOR);
    impl Drop for Descriptor {
        fn drop(&mut self) { if !self.0.0.is_null() { unsafe { LocalFree(Some(HLOCAL(self.0.0))); } } }
    }
    fn wide_path(path: &Path) -> Vec<u16> { path.as_os_str().encode_wide().chain(Some(0)).collect() }

    pub fn ensure_private_directory(path: &Path) -> Result<(), String> {
        if !path.is_absolute() { return Err("RDP private directory must be absolute".into()); }
        let sddl: Vec<u16> = "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)".encode_utf16().chain(Some(0)).collect();
        let mut expected = Descriptor(PSECURITY_DESCRIPTOR::default());
        unsafe { ConvertStringSecurityDescriptorToSecurityDescriptorW(PCWSTR(sddl.as_ptr()), 1, &mut expected.0, None) }
            .map_err(|_| "RDP private directory security descriptor failed".to_string())?;
        let name = wide_path(path);
        let security = SECURITY_ATTRIBUTES { nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: expected.0.0, bInheritHandle: false.into() };
        if !path.exists() {
            unsafe { CreateDirectoryW(PCWSTR(name.as_ptr()), Some(&security)) }
                .map_err(|_| "RDP private directory creation failed (installer-controlled parent required)".to_string())?;
        }
        use std::os::windows::fs::MetadataExt;
        let metadata = std::fs::symlink_metadata(path).map_err(|_| "RDP private directory unavailable".to_string())?;
        if !metadata.is_dir() || metadata.file_attributes() & 0x400 != 0 {
            return Err("RDP private directory reparse point refused".into());
        }
        let mut actual = Descriptor(PSECURITY_DESCRIPTOR::default());
        let status = unsafe { GetNamedSecurityInfoW(PCWSTR(name.as_ptr()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            None, None, None, None, &mut actual.0) };
        if status.0 != 0 { return Err("RDP private directory ACL unavailable".into()); }
        fn acl_bytes(descriptor: &Descriptor) -> Result<Vec<u8>, String> {
            let mut present = false.into();
            let mut defaulted = false.into();
            let mut acl: *mut ACL = std::ptr::null_mut();
            unsafe { GetSecurityDescriptorDacl(descriptor.0, &mut present, &mut acl, &mut defaulted) }
                .map_err(|_| "RDP private directory ACL invalid".to_string())?;
            if !present.as_bool() || acl.is_null() { return Err("RDP private directory has no restrictive ACL".into()); }
            Ok(unsafe { std::slice::from_raw_parts(acl.cast::<u8>(), (*acl).AclSize as usize) }.to_vec())
        }
        if acl_bytes(&actual)? != acl_bytes(&expected)? { return Err("RDP private directory ACL changed; access refused".into()); }
        Ok(())
    }

    pub fn replace_file(source: &Path, target: &Path) -> Result<(), String> {
        let source = wide_path(source);
        let target = wide_path(target);
        unsafe { MoveFileExW(PCWSTR(source.as_ptr()), PCWSTR(target.as_ptr()), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) }
            .map_err(|_| "RDP identity atomic replace failed".to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use zeroize::Zeroizing;

    struct TestStore(WorkspaceStore);
    impl TestStore {
        fn new() -> Self {
            let nonce = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
            let root = std::env::temp_dir().join(format!("gammaray-rdp-store-{}-{nonce}", std::process::id()));
            std::fs::create_dir(&root).unwrap();
            Self(WorkspaceStore { root })
        }
    }
    impl Drop for TestStore { fn drop(&mut self) { let _ = std::fs::remove_dir_all(&self.0.root); } }
    fn sample() -> RdpAccountSpec {
        RdpAccountSpec { workspace_id: "workspace-1".into(), account_name: "grdp_testaccount".into(),
            password: Zeroizing::new("aA1!01234567890123456789012345678901".into()), credential_version: 1, expected_sid: None }
    }
    fn identity(spec: &RdpAccountSpec) -> Result<RdpAccountIdentity, String> {
        Ok(RdpAccountIdentity { account_name: spec.account_name.clone(), sid: "S-1-5-21-1-2-3-1001".into(), credential_version: spec.credential_version })
    }
    #[test]
    fn restart_keeps_sid_and_never_persists_password() {
        let store = TestStore::new();
        let spec = sample();
        let first = store.0.provision_with(&spec, "app", "node", "device", identity).unwrap();
        let restarted = WorkspaceStore { root: store.0.root.clone() };
        let second = restarted.provision_with(&spec, "app", "node", "device", |checked| {
            assert_eq!(checked.expected_sid.as_deref(), Some("S-1-5-21-1-2-3-1001")); identity(checked)
        }).unwrap();
        assert_eq!(first, second);
        let bytes = std::fs::read_to_string(store.0.root.join("workspace-1.identity.json")).unwrap();
        assert!(!bytes.contains(spec.password.as_str()));
    }
    #[test]
    fn rebinding_or_rollback_fails_before_windows_adapter() {
        let store = TestStore::new();
        let mut spec = sample(); spec.credential_version = 2;
        store.0.provision_with(&spec, "app", "node", "device", identity).unwrap();
        for (app, node, device) in [("other", "node", "device"), ("app", "other", "device"), ("app", "node", "other")] {
            assert!(store.0.provision_with(&spec, app, node, device, |_| panic!("must not touch Windows")).is_err());
        }
        spec.credential_version = 1;
        assert!(store.0.provision_with(&spec, "app", "node", "device", |_| panic!("must not touch Windows")).is_err());
    }
    #[test]
    fn damaged_record_is_not_recreated_and_adapter_failure_keeps_identity() {
        let store = TestStore::new(); let spec = sample();
        store.0.provision_with(&spec, "app", "node", "device", identity).unwrap();
        let path = store.0.root.join("workspace-1.identity.json");
        let original = std::fs::read(&path).unwrap();
        assert!(store.0.provision_with(&spec, "app", "node", "device", |_| Err("simulated Windows failure".into())).is_err());
        assert_eq!(std::fs::read(&path).unwrap(), original);
        std::fs::write(&path, b"damaged").unwrap();
        assert!(store.0.provision_with(&spec, "app", "node", "device", |_| panic!("must not touch Windows")).is_err());
    }
    #[test]
    fn adapter_cannot_silently_replace_a_persisted_sid() {
        let store = TestStore::new(); let spec = sample();
        store.0.provision_with(&spec, "app", "node", "device", identity).unwrap();
        assert!(store.0.provision_with(&spec, "app", "node", "device", |checked| {
            let mut account = identity(checked)?; account.sid = "S-1-5-21-1-2-3-2002".into(); Ok(account)
        }).is_err());
    }
    #[cfg(windows)]
    #[test]
    fn concurrent_service_cannot_modify_same_workspace() {
        use std::os::windows::fs::OpenOptionsExt;
        let store = TestStore::new(); let spec = sample();
        let lock = OpenOptions::new().read(true).write(true).create(true).truncate(false).share_mode(0)
            .open(store.0.root.join("workspace-1.account.lock")).unwrap();
        assert!(store.0.provision_with(&spec, "app", "node", "device", |_| panic!("must not touch Windows")).is_err());
        drop(lock);
        assert!(store.0.provision_with(&spec, "app", "node", "device", identity).is_ok());
    }

    #[test]
    fn bootstrap_entropy_binds_runtime_node_and_both_certificates() {
        let mut binding = RdpBootstrapBinding { workspace_id: "workspace".into(), instance_id: "instance".into(), node_id: "node".into(),
            device_id: "device".into(), proxy_port: 13389, target_certificate_sha256: "a".repeat(64), proxy_certificate_sha256: "b".repeat(64) };
        let original = binding.entropy().unwrap();
        binding.instance_id = "new-instance".into(); assert_ne!(binding.entropy().unwrap(), original);
        binding.node_id = "../../node".into(); assert!(binding.entropy().is_err());
        binding.node_id = "node".into(); binding.target_certificate_sha256 = "g".repeat(64); assert!(binding.entropy().is_err());
    }

    #[cfg(windows)]
    #[test]
    fn bootstrap_file_contains_ciphertext_not_private_configuration() {
        let store = TestStore::new();
        let binding = RdpBootstrapBinding { workspace_id: "workspace".into(), instance_id: "instance".into(), node_id: "node".into(),
            device_id: "device".into(), proxy_port: 13389, target_certificate_sha256: "a".repeat(64), proxy_certificate_sha256: "b".repeat(64) };
        let secret = b"Password=private-bootstrap-test-password";
        let path = store.0.stage_bootstrap(&binding, secret).unwrap();
        let ciphertext = std::fs::read(&path).unwrap();
        assert!(!ciphertext.windows(secret.len()).any(|window| window == secret));
        assert!(store.0.stage_bootstrap(&binding, secret).is_err()); // Never overwrite a live runtime's bootstrap.
    }
}
