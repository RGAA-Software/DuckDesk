use serde::{Deserialize, Serialize};
use service_core::MsgAuthInfo;
use std::io;
#[cfg(test)]
use std::path::Path;
use std::path::PathBuf;
use zeroize::Zeroize;

const AUTH_FILE_NAME: &str = "console_auth.dpapi";

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
struct StoredAuthInfo {
    device_id: String,
    console_host: String,
    console_port: i32,
    console_ssl: bool,
    #[serde(default)]
    node_access_host: String,
    auth_id: String,
    auth_name: String,
    machine_code: String,
    appkey: String,
    role: i32,
    days: i32,
    max_streams: i32,
    end_timestamp_ms: i64,
}

impl From<&MsgAuthInfo> for StoredAuthInfo {
    fn from(value: &MsgAuthInfo) -> Self {
        Self {
            device_id: value.device_id.clone(),
            console_host: value.console_host.clone(),
            console_port: value.console_port,
            console_ssl: value.console_ssl,
            node_access_host: value.node_access_host.clone(),
            auth_id: value.auth_id.clone(),
            auth_name: value.auth_name.clone(),
            machine_code: value.machine_code.clone(),
            appkey: value.appkey.clone(),
            role: value.role,
            days: value.days,
            max_streams: value.max_streams,
            end_timestamp_ms: value.end_timestamp_ms,
        }
    }
}

impl From<StoredAuthInfo> for MsgAuthInfo {
    fn from(value: StoredAuthInfo) -> Self {
        Self {
            device_id: value.device_id,
            console_host: value.console_host,
            console_port: value.console_port,
            console_ssl: value.console_ssl,
            node_access_host: value.node_access_host,
            auth_id: value.auth_id,
            auth_name: value.auth_name,
            machine_code: value.machine_code,
            appkey: value.appkey,
            role: value.role,
            days: value.days,
            max_streams: value.max_streams,
            end_timestamp_ms: value.end_timestamp_ms,
        }
    }
}

#[derive(Clone, Debug)]
pub struct NodeAuthStore {
    file_path: PathBuf,
}

impl NodeAuthStore {
    pub fn new(data_root: PathBuf) -> Self {
        Self {
            file_path: data_root.join(AUTH_FILE_NAME),
        }
    }

    pub fn load(&self) -> Result<Option<MsgAuthInfo>, String> {
        let encrypted = match std::fs::read(&self.file_path) {
            Ok(value) => value,
            Err(error) if error.kind() == io::ErrorKind::NotFound => return Ok(None),
            Err(_) => return Err("cannot read protected node authorization".into()),
        };
        let mut plaintext = match unseal(&encrypted) {
            Ok(value) => value,
            Err(_) => {
                self.quarantine_invalid()?;
                return Ok(None);
            }
        };
        let stored = serde_json::from_slice::<StoredAuthInfo>(&plaintext);
        plaintext.zeroize();
        let stored = match stored {
            Ok(value) if !value.device_id.is_empty() && !value.appkey.is_empty() => value,
            _ => {
                self.quarantine_invalid()?;
                return Ok(None);
            }
        };
        Ok(Some(stored.into()))
    }

    pub fn save(&self, auth: &MsgAuthInfo) -> Result<(), String> {
        if auth.device_id.is_empty() || auth.appkey.is_empty() {
            return Err("refuse to store incomplete node authorization".into());
        }
        let mut plaintext = serde_json::to_vec(&StoredAuthInfo::from(auth))
            .map_err(|_| "cannot serialize node authorization")?;
        let encrypted = seal(&plaintext);
        plaintext.zeroize();
        let encrypted = encrypted?;
        std::fs::create_dir_all(
            self.file_path
                .parent()
                .ok_or("node authorization path has no parent")?,
        )
        .map_err(|_| "cannot create node authorization directory")?;
        let pending = self.file_path.with_extension("dpapi.pending");
        std::fs::write(&pending, encrypted)
            .map_err(|_| "cannot write protected node authorization")?;
        std::fs::rename(&pending, &self.file_path)
            .map_err(|_| "cannot publish protected node authorization".to_string())
    }

    pub fn clear(&self) -> Result<(), String> {
        match std::fs::remove_file(&self.file_path) {
            Ok(()) => Ok(()),
            Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(()),
            Err(_) => Err("cannot remove protected node authorization".into()),
        }
    }

    fn quarantine_invalid(&self) -> Result<(), String> {
        let invalid = self.file_path.with_extension("dpapi.invalid");
        if invalid.exists() {
            std::fs::remove_file(&invalid)
                .map_err(|_| "cannot rotate invalid node authorization")?;
        }
        std::fs::rename(&self.file_path, invalid)
            .map_err(|_| "cannot quarantine invalid node authorization".to_string())
    }

    #[cfg(test)]
    fn file_path(&self) -> &Path {
        &self.file_path
    }
}

#[cfg(windows)]
fn seal(bytes: &[u8]) -> Result<Vec<u8>, String> {
    use windows::core::PCWSTR;
    use windows::Win32::Foundation::{LocalFree, HLOCAL};
    use windows::Win32::Security::Cryptography::{
        CryptProtectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
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
            CRYPTPROTECT_UI_FORBIDDEN,
            &mut output.0,
        )
    }
    .map_err(|_| "cannot encrypt node authorization")?;
    if output.0.pbData.is_null() || output.0.cbData == 0 {
        return Err("node authorization encryption returned no data".into());
    }
    Ok(unsafe { std::slice::from_raw_parts(output.0.pbData, output.0.cbData as usize) }.to_vec())
}

#[cfg(windows)]
fn unseal(bytes: &[u8]) -> Result<Vec<u8>, String> {
    use windows::Win32::Foundation::{LocalFree, HLOCAL};
    use windows::Win32::Security::Cryptography::{
        CryptUnprotectData, CRYPTPROTECT_UI_FORBIDDEN, CRYPT_INTEGER_BLOB,
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
    .map_err(|_| "cannot decrypt node authorization")?;
    if output.0.pbData.is_null() || output.0.cbData == 0 {
        return Err("node authorization decryption returned no data".into());
    }
    Ok(unsafe { std::slice::from_raw_parts(output.0.pbData, output.0.cbData as usize) }.to_vec())
}

#[cfg(not(windows))]
fn seal(_: &[u8]) -> Result<Vec<u8>, String> {
    Err("node authorization storage requires Windows DPAPI".into())
}

#[cfg(not(windows))]
fn unseal(_: &[u8]) -> Result<Vec<u8>, String> {
    Err("node authorization storage requires Windows DPAPI".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn sample() -> MsgAuthInfo {
        MsgAuthInfo {
            device_id: "device".into(),
            console_host: "console".into(),
            console_port: 4600,
            console_ssl: true,
            node_access_host: "203.0.113.8".into(),
            auth_id: "id".into(),
            auth_name: "name".into(),
            machine_code: "machine".into(),
            appkey: "appkey".into(),
            role: 2,
            days: 1,
            max_streams: 3,
            end_timestamp_ms: 4,
        }
    }

    #[test]
    fn encrypted_authorization_round_trip_never_contains_plain_appkey() {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let store = NodeAuthStore::new(std::env::temp_dir().join(format!("px_auth_store_{nonce}")));
        let auth = sample();
        store.save(&auth).unwrap();
        assert_eq!(store.load().unwrap(), Some(auth));
        assert!(!std::fs::read(store.file_path())
            .unwrap()
            .windows("appkey".len())
            .any(|value| value == b"appkey"));
        store.clear().unwrap();
        let _ = std::fs::remove_dir(store.file_path().parent().unwrap());
    }
}
