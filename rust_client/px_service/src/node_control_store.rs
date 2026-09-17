use serde::{Deserialize, Serialize};
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use zeroize::{Zeroize, Zeroizing};

const CONFIG_DIRECTORY: &str = "node-control";
const CONFIG_FILE: &str = "configuration.dpapi";
const MAX_INPUT_BYTES: u64 = 16 * 1024;

#[derive(Debug)]
pub struct NodeControlConfiguration {
    pub endpoint: String,
    pub node_token: Zeroizing<String>,
    pub public_host: String,
}

impl Clone for NodeControlConfiguration {
    fn clone(&self) -> Self {
        Self {
            endpoint: self.endpoint.clone(),
            node_token: Zeroizing::new(self.node_token.to_string()),
            public_host: self.public_host.clone(),
        }
    }
}

impl PartialEq for NodeControlConfiguration {
    fn eq(&self, other: &Self) -> bool {
        self.endpoint == other.endpoint
            && self.node_token.as_str() == other.node_token.as_str()
            && self.public_host == other.public_host
    }
}

impl Eq for NodeControlConfiguration {}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct StoredConfiguration {
    schema_version: u32,
    endpoint: String,
    node_token: String,
    public_host: String,
}

impl Drop for StoredConfiguration {
    fn drop(&mut self) {
        self.node_token.zeroize();
    }
}

#[derive(Serialize)]
struct StoredConfigurationRef<'a> {
    schema_version: u32,
    endpoint: &'a str,
    node_token: &'a str,
    public_host: &'a str,
}

impl StoredConfiguration {
    fn into_runtime(mut self) -> Result<NodeControlConfiguration, String> {
        if self.schema_version != 1 {
            return Err("unsupported node-control configuration schema".into());
        }
        let runtime = NodeControlConfiguration {
            endpoint: std::mem::take(&mut self.endpoint),
            node_token: Zeroizing::new(std::mem::take(&mut self.node_token)),
            public_host: std::mem::take(&mut self.public_host),
        };
        runtime.validate()?;
        Ok(runtime)
    }
}

impl NodeControlConfiguration {
    pub fn validate(&self) -> Result<(), String> {
        validate_endpoint(&self.endpoint)?;
        validate_token(&self.node_token)?;
        validate_public_host(&self.public_host)
    }
}

#[derive(Clone, Debug)]
pub struct NodeControlStore {
    directory: PathBuf,
    file_path: PathBuf,
}

impl NodeControlStore {
    pub fn new(data_root: PathBuf) -> Self {
        let directory = data_root.join(CONFIG_DIRECTORY);
        Self {
            file_path: directory.join(CONFIG_FILE),
            directory,
        }
    }

    pub fn load(&self) -> Result<Option<NodeControlConfiguration>, String> {
        if !self.file_path.exists() {
            return Ok(None);
        }
        platform::ensure_private_directory(&self.directory)?;
        reject_reparse_point(&self.file_path)?;
        let encrypted = std::fs::read(&self.file_path)
            .map_err(|_| "cannot read protected node-control configuration".to_string())?;
        let mut plaintext = platform::unseal(&encrypted)?;
        let decoded = serde_json::from_slice::<StoredConfiguration>(&plaintext);
        plaintext.zeroize();
        decoded
            .map_err(|_| "invalid protected node-control configuration".to_string())?
            .into_runtime()
            .map(Some)
    }

    pub fn save(&self, configuration: &NodeControlConfiguration) -> Result<(), String> {
        configuration.validate()?;
        platform::ensure_private_directory(&self.directory)?;
        let stored = StoredConfigurationRef {
            schema_version: 1,
            endpoint: &configuration.endpoint,
            node_token: configuration.node_token.as_str(),
            public_host: &configuration.public_host,
        };
        let mut plaintext = serde_json::to_vec(&stored)
            .map_err(|_| "cannot serialize node-control configuration".to_string())?;
        let encrypted = platform::seal(&plaintext);
        plaintext.zeroize();
        let encrypted = encrypted?;
        let pending = self.file_path.with_extension("dpapi.pending");
        std::fs::write(&pending, encrypted)
            .map_err(|_| "cannot write protected node-control configuration".to_string())?;
        platform::replace_file(&pending, &self.file_path)
    }

    pub fn clear(&self) -> Result<(), String> {
        if !self.directory.exists() {
            return Ok(());
        }
        platform::ensure_private_directory(&self.directory)?;
        match std::fs::remove_file(&self.file_path) {
            Ok(()) => Ok(()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
            Err(_) => Err("cannot remove protected node-control configuration".into()),
        }
    }

    #[cfg(test)]
    fn file_path(&self) -> &Path {
        &self.file_path
    }
}

pub fn configure_from_stdin() -> Result<(), String> {
    let mut plaintext = Vec::new();
    std::io::stdin()
        .take(MAX_INPUT_BYTES + 1)
        .read_to_end(&mut plaintext)
        .map_err(|_| "cannot read node-control configuration from stdin".to_string())?;
    if plaintext.is_empty() || plaintext.len() as u64 > MAX_INPUT_BYTES {
        plaintext.zeroize();
        return Err("node-control configuration input must be between 1 byte and 16 KiB".into());
    }
    let decoded = serde_json::from_slice::<StoredConfiguration>(&plaintext);
    plaintext.zeroize();
    let configuration = decoded
        .map_err(|_| "invalid node-control configuration input".to_string())?
        .into_runtime()?;
    installed_store().save(&configuration)
}

pub fn clear_installed_configuration() -> Result<(), String> {
    installed_store().clear()
}

fn installed_store() -> NodeControlStore {
    NodeControlStore::new(service_core::windows_util::default_service_data_root())
}

fn validate_endpoint(value: &str) -> Result<(), String> {
    if value.trim() != value || value.len() > 2048 {
        return Err("node-control endpoint is invalid".into());
    }
    let endpoint = url::Url::parse(value).map_err(|_| "node-control endpoint is invalid")?;
    let loopback_development = endpoint.scheme() == "ws"
        && endpoint
            .host_str()
            .and_then(|host| host.parse::<std::net::IpAddr>().ok())
            .is_some_and(|address| address.is_loopback());
    if endpoint.scheme() != "wss" && !loopback_development {
        return Err(
            "node-control endpoint must use wss (ws is limited to loopback development)".into(),
        );
    }
    if endpoint.host().is_none()
        || !endpoint.username().is_empty()
        || endpoint.password().is_some()
        || endpoint.query().is_some()
        || endpoint.fragment().is_some()
        || endpoint.path() != "/api/console/node-control"
    {
        return Err("node-control endpoint must be an exact Console node-control URL".into());
    }
    Ok(())
}

fn validate_token(value: &str) -> Result<(), String> {
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err("node token must be 64 lowercase hexadecimal characters".into());
    }
    Ok(())
}

fn validate_public_host(value: &str) -> Result<(), String> {
    if value.is_empty()
        || value.trim() != value
        || value.contains(['/', '?', '#', '@'])
        || url::Host::parse(value).is_err()
    {
        return Err("node public host must be a hostname or IP address without a port".into());
    }
    match url::Host::parse(value).map_err(|_| "node public host is invalid")? {
        url::Host::Ipv4(address)
            if address.is_unspecified() || address.is_multicast() || address.is_broadcast() =>
        {
            Err("node public host must be a usable destination".into())
        }
        url::Host::Ipv6(address) if address.is_unspecified() || address.is_multicast() => {
            Err("node public host must be a usable destination".into())
        }
        _ => Ok(()),
    }
}

fn reject_reparse_point(path: &Path) -> Result<(), String> {
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        let metadata = std::fs::symlink_metadata(path)
            .map_err(|_| "node-control configuration is unavailable".to_string())?;
        if !metadata.is_file() || metadata.file_attributes() & 0x400 != 0 {
            return Err("node-control configuration reparse point refused".into());
        }
    }
    #[cfg(not(windows))]
    if !std::fs::symlink_metadata(path)
        .map_err(|_| "node-control configuration is unavailable".to_string())?
        .is_file()
    {
        return Err("node-control configuration must be a regular file".into());
    }
    Ok(())
}

#[cfg(windows)]
mod platform {
    use super::*;
    use std::os::windows::ffi::OsStrExt;
    use windows::core::{PCWSTR, PWSTR};
    use windows::Win32::Foundation::{LocalFree, HLOCAL};
    use windows::Win32::Security::Authorization::{
        ConvertSecurityDescriptorToStringSecurityDescriptorW,
        ConvertStringSecurityDescriptorToSecurityDescriptorW,
    };
    use windows::Win32::Security::Cryptography::{
        CryptProtectData, CryptUnprotectData, CRYPTPROTECT_LOCAL_MACHINE,
        CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
    };
    use windows::Win32::Security::{
        GetFileSecurityW, GetSecurityDescriptorControl, DACL_SECURITY_INFORMATION,
        PSECURITY_DESCRIPTOR, SECURITY_ATTRIBUTES, SE_DACL_PROTECTED,
    };
    use windows::Win32::Storage::FileSystem::{
        CreateDirectoryW, MoveFileExW, MOVEFILE_REPLACE_EXISTING, MOVEFILE_WRITE_THROUGH,
    };

    struct ProtectedBlob(CRYPT_INTEGER_BLOB);

    impl Drop for ProtectedBlob {
        fn drop(&mut self) {
            if !self.0.pbData.is_null() {
                unsafe {
                    std::slice::from_raw_parts_mut(self.0.pbData, self.0.cbData as usize).zeroize();
                    LocalFree(Some(HLOCAL(self.0.pbData.cast())));
                }
            }
        }
    }

    struct Descriptor(PSECURITY_DESCRIPTOR);

    impl Drop for Descriptor {
        fn drop(&mut self) {
            if !self.0 .0.is_null() {
                unsafe {
                    LocalFree(Some(HLOCAL(self.0 .0)));
                }
            }
        }
    }

    struct LocalWideString(PWSTR);

    impl Drop for LocalWideString {
        fn drop(&mut self) {
            if !self.0.is_null() {
                unsafe {
                    LocalFree(Some(HLOCAL(self.0.as_ptr().cast())));
                }
            }
        }
    }

    fn canonical_dacl(descriptor: PSECURITY_DESCRIPTOR) -> Result<String, String> {
        let mut text = LocalWideString(PWSTR::null());
        unsafe {
            ConvertSecurityDescriptorToStringSecurityDescriptorW(
                descriptor,
                1,
                DACL_SECURITY_INFORMATION,
                &mut text.0,
                None,
            )
        }
        .map_err(|_| "node-control directory ACL is invalid".to_string())?;
        let text = unsafe { text.0.to_string() }
            .map_err(|_| "node-control directory ACL encoding is invalid".to_string())?;
        let ace_offset = text
            .find('(')
            .ok_or_else(|| "node-control directory has no restrictive ACL".to_string())?;
        let ace_text = &text[ace_offset..];
        let mut aces: Vec<&str> = ace_text.split_inclusive(')').collect();
        if !text.starts_with("D:")
            || aces.is_empty()
            || aces.iter().map(|ace| ace.len()).sum::<usize>() != ace_text.len()
            || aces
                .iter()
                .any(|ace| !ace.starts_with('(') || !ace.ends_with(')'))
        {
            return Err("node-control directory ACL is invalid".into());
        }
        aces.sort_unstable();
        Ok(aces.concat())
    }

    fn file_security_descriptor(name: &[u16]) -> Result<Vec<u8>, String> {
        let mut required = 0_u32;
        let _ = unsafe {
            GetFileSecurityW(
                PCWSTR(name.as_ptr()),
                DACL_SECURITY_INFORMATION.0,
                None,
                0,
                &mut required,
            )
        };
        if required == 0 {
            return Err("node-control directory ACL is unavailable".into());
        }
        let mut bytes = vec![0_u8; required as usize];
        let descriptor = PSECURITY_DESCRIPTOR(bytes.as_mut_ptr().cast());
        let loaded = unsafe {
            GetFileSecurityW(
                PCWSTR(name.as_ptr()),
                DACL_SECURITY_INFORMATION.0,
                Some(descriptor),
                required,
                &mut required,
            )
        };
        if !loaded.as_bool() {
            return Err("node-control directory ACL is unavailable".into());
        }
        Ok(bytes)
    }

    fn dacl_is_protected(descriptor: PSECURITY_DESCRIPTOR) -> Result<bool, String> {
        let mut control = 0_u16;
        let mut revision = 0_u32;
        unsafe { GetSecurityDescriptorControl(descriptor, &mut control, &mut revision) }
            .map_err(|_| "node-control directory ACL control is invalid".to_string())?;
        Ok(control & SE_DACL_PROTECTED.0 != 0)
    }

    pub fn seal(bytes: &[u8]) -> Result<Vec<u8>, String> {
        let input = CRYPT_INTEGER_BLOB {
            cbData: bytes.len() as u32,
            pbData: bytes.as_ptr().cast_mut(),
        };
        let mut output = ProtectedBlob(CRYPT_INTEGER_BLOB::default());
        unsafe {
            CryptProtectData(
                &input,
                PCWSTR::null(),
                None,
                None,
                None,
                CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN,
                &mut output.0,
            )
        }
        .map_err(|_| "cannot encrypt node-control configuration".to_string())?;
        if output.0.pbData.is_null() || output.0.cbData == 0 {
            return Err("node-control encryption returned no data".into());
        }
        Ok(
            unsafe { std::slice::from_raw_parts(output.0.pbData, output.0.cbData as usize) }
                .to_vec(),
        )
    }

    pub fn unseal(bytes: &[u8]) -> Result<Vec<u8>, String> {
        let input = CRYPT_INTEGER_BLOB {
            cbData: bytes.len() as u32,
            pbData: bytes.as_ptr().cast_mut(),
        };
        let mut output = ProtectedBlob(CRYPT_INTEGER_BLOB::default());
        unsafe {
            CryptUnprotectData(
                &input,
                None,
                None,
                None,
                None,
                CRYPTPROTECT_UI_FORBIDDEN,
                &mut output.0,
            )
        }
        .map_err(|_| "cannot decrypt node-control configuration".to_string())?;
        if output.0.pbData.is_null() || output.0.cbData == 0 {
            return Err("node-control decryption returned no data".into());
        }
        Ok(
            unsafe { std::slice::from_raw_parts(output.0.pbData, output.0.cbData as usize) }
                .to_vec(),
        )
    }

    pub fn ensure_private_directory(path: &Path) -> Result<(), String> {
        if !path.is_absolute() {
            return Err("node-control directory must be absolute".into());
        }
        let sddl: Vec<u16> = "D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"
            .encode_utf16()
            .chain(Some(0))
            .collect();
        let mut descriptor = Descriptor(PSECURITY_DESCRIPTOR::default());
        unsafe {
            ConvertStringSecurityDescriptorToSecurityDescriptorW(
                PCWSTR(sddl.as_ptr()),
                1,
                &mut descriptor.0,
                None,
            )
        }
        .map_err(|_| "node-control security descriptor creation failed".to_string())?;
        let name: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        let security = SECURITY_ATTRIBUTES {
            nLength: std::mem::size_of::<SECURITY_ATTRIBUTES>() as u32,
            lpSecurityDescriptor: descriptor.0 .0,
            bInheritHandle: false.into(),
        };
        if !path.exists() {
            unsafe { CreateDirectoryW(PCWSTR(name.as_ptr()), Some(&security)) }
                .map_err(|_| "cannot create protected node-control directory".to_string())?;
        }
        use std::os::windows::fs::MetadataExt;
        let metadata = std::fs::symlink_metadata(path)
            .map_err(|_| "protected node-control directory is unavailable".to_string())?;
        if !metadata.is_dir() || metadata.file_attributes() & 0x400 != 0 {
            return Err("node-control directory reparse point refused".into());
        }
        let mut actual = file_security_descriptor(&name)?;
        let actual = PSECURITY_DESCRIPTOR(actual.as_mut_ptr().cast());
        if !dacl_is_protected(actual)? || canonical_dacl(actual)? != canonical_dacl(descriptor.0)? {
            return Err("node-control directory ACL changed; access refused".into());
        }
        Ok(())
    }

    pub fn replace_file(source: &Path, target: &Path) -> Result<(), String> {
        let source: Vec<u16> = source.as_os_str().encode_wide().chain(Some(0)).collect();
        let target: Vec<u16> = target.as_os_str().encode_wide().chain(Some(0)).collect();
        unsafe {
            MoveFileExW(
                PCWSTR(source.as_ptr()),
                PCWSTR(target.as_ptr()),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH,
            )
        }
        .map_err(|_| "cannot publish protected node-control configuration".to_string())
    }
}

#[cfg(not(windows))]
mod platform {
    use super::*;

    pub fn seal(_: &[u8]) -> Result<Vec<u8>, String> {
        Err("node-control storage requires Windows DPAPI".into())
    }

    pub fn unseal(_: &[u8]) -> Result<Vec<u8>, String> {
        Err("node-control storage requires Windows DPAPI".into())
    }

    pub fn ensure_private_directory(_: &Path) -> Result<(), String> {
        Err("node-control storage requires Windows DPAPI".into())
    }

    pub fn replace_file(_: &Path, _: &Path) -> Result<(), String> {
        Err("node-control storage requires Windows DPAPI".into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn configuration() -> NodeControlConfiguration {
        NodeControlConfiguration {
            endpoint: "wss://console.example.com/api/console/node-control".into(),
            node_token: Zeroizing::new("a".repeat(64)),
            public_host: "render.example.com".into(),
        }
    }

    #[test]
    fn validation_accepts_exact_secure_and_loopback_development_endpoints() {
        configuration().validate().unwrap();
        let mut loopback = configuration();
        loopback.endpoint = "ws://127.0.0.1:8080/api/console/node-control".into();
        loopback.validate().unwrap();
    }

    #[test]
    fn validation_rejects_fallback_routes_queries_credentials_and_bad_tokens() {
        for endpoint in [
            "wss://console.example.com/console/service",
            "wss://console.example.com/cms/service",
            "wss://console.example.com/api/console/node-control?token=secret",
            "wss://user@console.example.com/api/console/node-control",
            "ws://console.example.com/api/console/node-control",
        ] {
            let mut value = configuration();
            value.endpoint = endpoint.into();
            assert!(value.validate().is_err(), "accepted {endpoint}");
        }
        let mut value = configuration();
        value.node_token = Zeroizing::new("A".repeat(64));
        assert!(value.validate().is_err());
    }

    #[test]
    fn validation_rejects_url_or_unusable_public_host() {
        for host in [
            "https://render.example.com",
            "render.example.com:4601",
            "0.0.0.0",
            "239.1.1.1",
            " user.example.com",
        ] {
            let mut value = configuration();
            value.public_host = host.into();
            assert!(value.validate().is_err(), "accepted {host}");
        }
    }

    #[cfg(windows)]
    #[test]
    fn protected_configuration_round_trip_does_not_store_plain_token() {
        let nonce = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let directory = std::env::temp_dir().join(format!("pixels_node_control_{nonce}"));
        std::fs::create_dir(&directory).unwrap();
        let store = NodeControlStore::new(directory.clone());
        let value = configuration();
        store.save(&value).unwrap();
        assert_eq!(store.load().unwrap(), Some(value));
        let encrypted = std::fs::read(store.file_path()).unwrap();
        assert!(!encrypted
            .windows(64)
            .any(|bytes| bytes
                == b"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        store.clear().unwrap();
        std::fs::remove_dir(directory.join(CONFIG_DIRECTORY)).unwrap();
        std::fs::remove_dir(directory).unwrap();
    }
}
