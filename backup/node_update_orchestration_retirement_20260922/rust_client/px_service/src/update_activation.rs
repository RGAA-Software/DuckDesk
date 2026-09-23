use crate::node_control_store::{platform, reject_reparse_point};
use crate::product_descriptor::{valid_release_identity, ProductDescriptor};
use chrono::{DateTime, Utc};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::io::{Read, Write};
use std::net::{SocketAddr, TcpStream};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};
use uuid::Uuid;
use zeroize::Zeroize;

const ACTIVATION_DIRECTORY: &str = "activation";
const ACTIVATION_FILE: &str = "current.dpapi";
const MAXIMUM_RECORD_BYTES: u64 = 64 * 1024;
const SERVICE_HEALTH_TIMEOUT: Duration = Duration::from_secs(30);
const SERVICE_HEALTH_RETRY: Duration = Duration::from_millis(250);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub(crate) enum ActivationPhase {
    Authorized,
    Applying,
    Installed,
    Failed,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub(crate) struct UpdateActivationRecord {
    pub schema_version: u32,
    pub release_id: Uuid,
    pub policy_revision: i64,
    pub task_id: Uuid,
    pub lease_id: Uuid,
    pub lease_until: DateTime<Utc>,
    pub product: String,
    pub distribution: String,
    pub release_namespace: String,
    pub oem_id: Option<String>,
    pub oem_profile_sha256: Option<String>,
    pub company: String,
    pub from_build_number: u32,
    pub to_build_number: u32,
    pub version: String,
    pub prepared_sha256: String,
    pub target_signer_sha256: String,
    pub rollback_sha256: Option<String>,
    pub rollback_signer_sha256: Option<String>,
    pub artifact_path: PathBuf,
    pub install_directory: PathBuf,
    pub service_port: u16,
    pub phase: ActivationPhase,
    pub error_code: Option<String>,
}

impl UpdateActivationRecord {
    pub(crate) fn validate(&self, data_root: &Path) -> Result<(), String> {
        let expected_prepared_root = data_root.join("updates").join("prepared");
        let valid_error = self.error_code.as_deref().is_none_or(|value| {
            !value.is_empty()
                && value.len() <= 64
                && value
                    .bytes()
                    .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'_')
        });
        if self.schema_version != 3
            || self.release_id.is_nil()
            || self.policy_revision < 1
            || self.task_id.is_nil()
            || self.lease_id.is_nil()
            || !matches!(self.product.as_str(), "cloud_node" | "remote")
            || !valid_release_identity(
                &self.distribution,
                Some(&self.release_namespace),
                self.oem_id.as_deref(),
                &self.company,
                self.oem_profile_sha256.as_deref(),
            )
            || self.from_build_number == 0
            || self.to_build_number <= self.from_build_number
            || self.version.is_empty()
            || self.version.len() > 64
            || self.prepared_sha256.len() != 64
            || !self
                .prepared_sha256
                .bytes()
                .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
            || !valid_sha256(&self.target_signer_sha256)
            || self.rollback_sha256.as_deref().is_some_and(|digest| {
                digest.len() != 64
                    || !digest
                        .bytes()
                        .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
            })
            || self
                .rollback_signer_sha256
                .as_deref()
                .is_some_and(|digest| !valid_sha256(digest))
            || self.rollback_sha256.is_some() != self.rollback_signer_sha256.is_some()
            || !self.artifact_path.is_absolute()
            || !self.artifact_path.starts_with(expected_prepared_root)
            || !self.install_directory.is_absolute()
            || self.service_port == 0
            || !valid_error
            || (self.phase == ActivationPhase::Failed) != self.error_code.is_some()
        {
            return Err("update activation record is invalid".into());
        }
        Ok(())
    }
}

#[derive(Clone, Debug)]
pub(crate) struct UpdateActivationStore {
    data_root: PathBuf,
    directory: PathBuf,
    file_path: PathBuf,
}

impl UpdateActivationStore {
    pub(crate) fn new(data_root: PathBuf) -> Self {
        let directory = data_root.join("updates").join(ACTIVATION_DIRECTORY);
        Self {
            data_root,
            file_path: directory.join(ACTIVATION_FILE),
            directory,
        }
    }

    pub(crate) fn load(&self) -> Result<Option<UpdateActivationRecord>, String> {
        if !self.file_path.exists() {
            return Ok(None);
        }
        platform::ensure_private_directory(&self.directory)?;
        reject_reparse_point(&self.file_path)?;
        let metadata = std::fs::metadata(&self.file_path)
            .map_err(|_| "cannot inspect protected update activation record".to_string())?;
        if metadata.len() == 0 || metadata.len() > MAXIMUM_RECORD_BYTES {
            return Err("protected update activation record size is invalid".into());
        }
        let encrypted = std::fs::read(&self.file_path)
            .map_err(|_| "cannot read protected update activation record".to_string())?;
        let mut plaintext = platform::unseal(&encrypted)?;
        let decoded = serde_json::from_slice::<UpdateActivationRecord>(&plaintext);
        plaintext.zeroize();
        let record =
            decoded.map_err(|_| "protected update activation record is invalid".to_string())?;
        record.validate(&self.data_root)?;
        Ok(Some(record))
    }

    pub(crate) fn save(&self, record: &UpdateActivationRecord) -> Result<(), String> {
        record.validate(&self.data_root)?;
        platform::ensure_private_directory(&self.directory)?;
        let mut plaintext = serde_json::to_vec(record)
            .map_err(|_| "cannot serialize update activation record".to_string())?;
        if plaintext.len() as u64 > MAXIMUM_RECORD_BYTES {
            plaintext.zeroize();
            return Err("update activation record is too large".into());
        }
        let encrypted = platform::seal(&plaintext);
        plaintext.zeroize();
        let pending = self.file_path.with_extension("dpapi.pending");
        std::fs::write(&pending, encrypted?)
            .map_err(|_| "cannot write protected update activation record".to_string())?;
        platform::replace_file(&pending, &self.file_path)
    }

    pub(crate) fn remove(&self) -> Result<(), String> {
        match std::fs::remove_file(&self.file_path) {
            Ok(()) => Ok(()),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(()),
            Err(_) => Err("cannot remove protected update activation record".into()),
        }
    }

    pub(crate) fn mark_phase(
        &self,
        mut record: UpdateActivationRecord,
        phase: ActivationPhase,
        error_code: Option<&str>,
    ) -> Result<UpdateActivationRecord, String> {
        record.phase = phase;
        record.error_code = error_code.map(str::to_string);
        self.save(&record)?;
        Ok(record)
    }
}

pub(crate) fn run_authorized_update() -> Result<(), String> {
    let data_root = service_core::windows_util::default_service_data_root();
    run_authorized_update_with(&SystemActivationRuntime, data_root)
}

fn run_authorized_update_with(
    runtime: &impl ActivationRuntime,
    data_root: PathBuf,
) -> Result<(), String> {
    let store = UpdateActivationStore::new(data_root.clone());
    let record = store
        .load()?
        .ok_or_else(|| "authorized update record is missing".to_string())?;
    if record.phase != ActivationPhase::Authorized || record.lease_until <= Utc::now() {
        return Err("authorized update record is stale or already consumed".into());
    }
    let record = store.mark_phase(record, ActivationPhase::Applying, None)?;
    let task_id = record.task_id;
    let result = apply_authorized_update(runtime, &store, &data_root, record);
    if let Err(error) = result {
        if let Some(current) = store.load()? {
            if current.task_id == task_id && current.phase == ActivationPhase::Applying {
                store.mark_phase(current, ActivationPhase::Failed, Some("activation_failed"))?;
            }
        }
        return Err(error);
    }
    Ok(())
}

trait ActivationRuntime {
    fn verify_installer(
        &self,
        installer_path: &Path,
        expected_sha256: &str,
        expected_signer_sha256: &str,
    ) -> Result<(), String>;
    fn snapshot_rollback_installer(
        &self,
        current_installer_path: &Path,
        task_installer_path: &Path,
        expected_sha256: &str,
        expected_signer_sha256: &str,
    ) -> Result<(), String>;
    fn run_installer(&self, installer_path: &Path) -> Result<bool, String>;
    fn installed_product_matches(&self, record: &UpdateActivationRecord, build_number: u32)
        -> bool;
    fn service_ready(&self, port: u16) -> bool;
}

struct SystemActivationRuntime;

impl ActivationRuntime for SystemActivationRuntime {
    fn verify_installer(
        &self,
        installer_path: &Path,
        expected_sha256: &str,
        expected_signer_sha256: &str,
    ) -> Result<(), String> {
        reject_reparse_point(installer_path)?;
        verify_sha256(installer_path, expected_sha256)?;
        verify_authenticode(installer_path, expected_signer_sha256)
    }

    fn snapshot_rollback_installer(
        &self,
        current_installer_path: &Path,
        task_installer_path: &Path,
        expected_sha256: &str,
        expected_signer_sha256: &str,
    ) -> Result<(), String> {
        self.verify_installer(
            current_installer_path,
            expected_sha256,
            expected_signer_sha256,
        )?;
        std::fs::copy(current_installer_path, task_installer_path)
            .map_err(|_| "cannot snapshot the previous installer".to_string())?;
        self.verify_installer(task_installer_path, expected_sha256, expected_signer_sha256)
    }

    fn run_installer(&self, installer_path: &Path) -> Result<bool, String> {
        std::process::Command::new(installer_path)
            .arg("/S")
            .status()
            .map(|status| status.success())
            .map_err(|_| "cannot start the verified update installer".to_string())
    }

    fn installed_product_matches(
        &self,
        record: &UpdateActivationRecord,
        build_number: u32,
    ) -> bool {
        installed_product_matches(record, build_number)
    }

    fn service_ready(&self, port: u16) -> bool {
        wait_for_service_health(port, SERVICE_HEALTH_TIMEOUT)
    }
}

fn apply_authorized_update(
    runtime: &impl ActivationRuntime,
    store: &UpdateActivationStore,
    data_root: &Path,
    record: UpdateActivationRecord,
) -> Result<(), String> {
    runtime.verify_installer(
        &record.artifact_path,
        &record.prepared_sha256,
        &record.target_signer_sha256,
    )?;

    let current_rollback = rollback_installer_path(
        data_root,
        &record.product,
        &record.distribution,
        record.oem_id.as_deref(),
    )?;
    let rollback_directory = current_rollback
        .parent()
        .ok_or_else(|| "rollback installer has no parent directory".to_string())?;
    let task_rollback = rollback_directory.join(format!("{}.previous.exe", record.task_id));
    let rollback_available =
        if let (Some(expected_rollback_sha256), Some(expected_rollback_signer)) =
            (&record.rollback_sha256, &record.rollback_signer_sha256)
        {
            runtime.snapshot_rollback_installer(
                &current_rollback,
                &task_rollback,
                expected_rollback_sha256,
                expected_rollback_signer,
            )?;
            true
        } else {
            false
        };

    let installed = runtime.run_installer(&record.artifact_path)?
        && runtime.installed_product_matches(&record, record.to_build_number)
        && runtime.service_ready(record.service_port);
    if installed {
        store.mark_phase(record, ActivationPhase::Installed, None)?;
        return Ok(());
    }

    let rollback_succeeded = rollback_available
        && runtime.run_installer(&task_rollback).unwrap_or(false)
        && runtime.installed_product_matches(&record, record.from_build_number)
        && runtime.service_ready(record.service_port);
    let error_code = if rollback_succeeded {
        "installer_failed"
    } else {
        "rollback_failed"
    };
    store.mark_phase(record, ActivationPhase::Failed, Some(error_code))?;
    Err(error_code.into())
}

fn installed_product_matches(record: &UpdateActivationRecord, build_number: u32) -> bool {
    let expected_signer = if build_number == record.to_build_number {
        Some(record.target_signer_sha256.as_str())
    } else if build_number == record.from_build_number {
        record.rollback_signer_sha256.as_deref()
    } else {
        None
    };
    ProductDescriptor::load(&record.install_directory.join("product-manifest.json")).is_ok_and(
        |descriptor| {
            descriptor.product == record.product
                && descriptor.distribution == record.distribution
                && descriptor.release_namespace.as_deref() == Some(&record.release_namespace)
                && descriptor.oem_id == record.oem_id
                && descriptor.oem_profile_sha256 == record.oem_profile_sha256
                && descriptor.company == record.company
                && descriptor.product_version_code == build_number
                && descriptor.signer_certificate_sha256.as_deref() == expected_signer
        },
    )
}

fn wait_for_service_health(port: u16, timeout: Duration) -> bool {
    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        if service_protocol_ready(port) {
            return true;
        }
        std::thread::sleep(SERVICE_HEALTH_RETRY);
    }
    false
}

fn service_protocol_ready(port: u16) -> bool {
    let address = SocketAddr::from(([127, 0, 0, 1], port));
    let Ok(mut stream) = TcpStream::connect_timeout(&address, Duration::from_millis(500)) else {
        return false;
    };
    let _ = stream.set_read_timeout(Some(Duration::from_secs(2)));
    let _ = stream.set_write_timeout(Some(Duration::from_secs(2)));
    let request = format!(
        "GET / HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: MDEyMzQ1Njc4OWFiY2RlZg==\r\nSec-WebSocket-Version: 13\r\n\r\n"
    );
    if stream.write_all(request.as_bytes()).is_err() {
        return false;
    }
    let mut response = [0_u8; 512];
    let Ok(read) = stream.read(&mut response) else {
        return false;
    };
    response[..read].starts_with(b"HTTP/1.1 101")
}

pub(crate) fn trusted_rollback_sha256(
    data_root: &Path,
    product: &str,
    distribution: &str,
    oem_id: Option<&str>,
    expected_signer_sha256: &str,
) -> Result<Option<String>, String> {
    let installer_path = rollback_installer_path(data_root, product, distribution, oem_id)?;
    if !installer_path.exists() {
        return Ok(None);
    }
    reject_reparse_point(&installer_path)?;
    verify_authenticode(&installer_path, expected_signer_sha256)?;
    Ok(Some(sha256(&installer_path)?))
}

fn rollback_installer_path(
    data_root: &Path,
    product: &str,
    distribution: &str,
    oem_id: Option<&str>,
) -> Result<PathBuf, String> {
    if !matches!(product, "cloud_node" | "remote")
        || !matches!(distribution, "official" | "customer" | "oem")
        || (distribution == "oem") != oem_id.is_some()
        || oem_id.is_some_and(|oem_id| {
            !(3..=32).contains(&oem_id.len())
                || !oem_id
                    .bytes()
                    .all(|byte| byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'-')
                || oem_id.starts_with('-')
                || oem_id.ends_with('-')
                || oem_id.contains("--")
        })
    {
        return Err("rollback installer identity is invalid".into());
    }
    let update_root = data_root.join("updates");
    platform::ensure_private_directory(&update_root)?;
    let rollback_directory = update_root.join("rollback");
    let metadata = std::fs::symlink_metadata(&rollback_directory)
        .map_err(|_| "rollback installer directory is unavailable".to_string())?;
    if !metadata.is_dir() {
        return Err("rollback installer directory is invalid".into());
    }
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x0400;
        if metadata.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
            return Err("rollback installer directory reparse point refused".into());
        }
    }
    let release_owner = oem_id.unwrap_or("pixels");
    Ok(rollback_directory.join(format!(
        "{product}-{distribution}-{release_owner}-current.exe"
    )))
}

fn verify_sha256(path: &Path, expected: &str) -> Result<(), String> {
    if sha256(path)? != expected {
        return Err("prepared update installer changed after verification".into());
    }
    Ok(())
}

fn valid_sha256(value: &str) -> bool {
    value.len() == 64 && value.bytes().all(|byte| byte.is_ascii_hexdigit())
}

fn sha256(path: &Path) -> Result<String, String> {
    let mut file = std::fs::File::open(path)
        .map_err(|_| "cannot reopen the prepared update installer".to_string())?;
    let mut digest = Sha256::new();
    let mut buffer = vec![0_u8; 128 * 1024];
    loop {
        let read = file
            .read(&mut buffer)
            .map_err(|_| "cannot hash the prepared update installer".to_string())?;
        if read == 0 {
            break;
        }
        digest.update(&buffer[..read]);
    }
    Ok(hex::encode(digest.finalize()))
}

#[cfg(windows)]
fn verify_authenticode(path: &Path, expected_signer_sha256: &str) -> Result<(), String> {
    use std::os::windows::ffi::OsStrExt;
    use windows::core::PCWSTR;
    use windows::Win32::Foundation::HWND;
    use windows::Win32::Security::WinTrust::{
        WTHelperGetProvCertFromChain, WTHelperGetProvSignerFromChain,
        WTHelperProvDataFromStateData, WinVerifyTrust, WINTRUST_ACTION_GENERIC_VERIFY_V2,
        WINTRUST_DATA, WINTRUST_DATA_0, WINTRUST_FILE_INFO, WTD_CACHE_ONLY_URL_RETRIEVAL,
        WTD_CHOICE_FILE, WTD_REVOKE_NONE, WTD_STATEACTION_CLOSE, WTD_STATEACTION_VERIFY,
        WTD_UICONTEXT_INSTALL, WTD_UI_NONE,
    };

    let wide_path = path
        .as_os_str()
        .encode_wide()
        .chain(Some(0))
        .collect::<Vec<_>>();
    let mut file = WINTRUST_FILE_INFO {
        cbStruct: std::mem::size_of::<WINTRUST_FILE_INFO>() as u32,
        pcwszFilePath: PCWSTR(wide_path.as_ptr()),
        ..Default::default()
    };
    let mut trust = WINTRUST_DATA {
        cbStruct: std::mem::size_of::<WINTRUST_DATA>() as u32,
        dwUIChoice: WTD_UI_NONE,
        fdwRevocationChecks: WTD_REVOKE_NONE,
        dwUnionChoice: WTD_CHOICE_FILE,
        Anonymous: WINTRUST_DATA_0 {
            pFile: std::ptr::from_mut(&mut file),
        },
        dwStateAction: WTD_STATEACTION_VERIFY,
        dwProvFlags: WTD_CACHE_ONLY_URL_RETRIEVAL,
        dwUIContext: WTD_UICONTEXT_INSTALL,
        ..Default::default()
    };
    let mut action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    let status = unsafe {
        WinVerifyTrust(
            HWND::default(),
            std::ptr::from_mut(&mut action),
            std::ptr::from_mut(&mut trust).cast(),
        )
    };
    let signer_check = if status == 0 {
        let provider_data = unsafe { WTHelperProvDataFromStateData(trust.hWVTStateData) };
        let signer = if provider_data.is_null() {
            std::ptr::null_mut()
        } else {
            unsafe { WTHelperGetProvSignerFromChain(provider_data, 0, false, 0) }
        };
        let provider_certificate = if signer.is_null() {
            std::ptr::null_mut()
        } else {
            unsafe { WTHelperGetProvCertFromChain(signer, 0) }
        };
        if provider_certificate.is_null() || unsafe { (*provider_certificate).pCert.is_null() } {
            Err("update installer signer certificate is unavailable".to_string())
        } else {
            let certificate_context = unsafe { &*(*provider_certificate).pCert };
            if certificate_context.pbCertEncoded.is_null() || certificate_context.cbCertEncoded == 0
            {
                Err("update installer signer certificate is empty".to_string())
            } else {
                let certificate_bytes = unsafe {
                    std::slice::from_raw_parts(
                        certificate_context.pbCertEncoded,
                        certificate_context.cbCertEncoded as usize,
                    )
                };
                let actual_signer_sha256 = hex::encode(Sha256::digest(certificate_bytes));
                if actual_signer_sha256.eq_ignore_ascii_case(expected_signer_sha256) {
                    Ok(())
                } else {
                    Err("update installer Authenticode signer pin mismatch".to_string())
                }
            }
        }
    } else {
        Err(format!(
            "update installer Authenticode verification failed: 0x{:08X}",
            status as u32
        ))
    };
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    unsafe {
        WinVerifyTrust(
            HWND::default(),
            std::ptr::from_mut(&mut action),
            std::ptr::from_mut(&mut trust).cast(),
        );
    }
    signer_check
}

#[cfg(not(windows))]
fn verify_authenticode(_: &Path, _: &str) -> Result<(), String> {
    Err("update installer Authenticode verification requires Windows".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::collections::VecDeque;
    use std::net::TcpListener;
    use std::thread;

    fn record(root: &Path) -> UpdateActivationRecord {
        UpdateActivationRecord {
            schema_version: 3,
            release_id: Uuid::from_u128(1),
            policy_revision: 2,
            task_id: Uuid::from_u128(2),
            lease_id: Uuid::from_u128(3),
            lease_until: Utc::now() + chrono::TimeDelta::minutes(10),
            product: "cloud_node".into(),
            distribution: "official".into(),
            release_namespace: "pixels.official".into(),
            oem_id: None,
            oem_profile_sha256: None,
            company: "Pixels".into(),
            from_build_number: 10,
            to_build_number: 11,
            version: "1.0.11".into(),
            prepared_sha256: "a".repeat(64),
            target_signer_sha256: "c".repeat(64),
            rollback_sha256: Some("b".repeat(64)),
            rollback_signer_sha256: Some("d".repeat(64)),
            artifact_path: root.join("updates/prepared/installer.exe"),
            install_directory: PathBuf::from("C:/Program Files/Pixels Cloud Node"),
            service_port: 4602,
            phase: ActivationPhase::Authorized,
            error_code: None,
        }
    }

    #[derive(Default)]
    struct FakeActivationRuntime {
        verification_error: RefCell<Option<String>>,
        installer_results: RefCell<VecDeque<Result<bool, String>>>,
        product_match_results: RefCell<VecDeque<bool>>,
        service_health_results: RefCell<VecDeque<bool>>,
        verified_installers: RefCell<Vec<PathBuf>>,
        verified_signers: RefCell<Vec<String>>,
        rollback_snapshots: RefCell<Vec<(PathBuf, PathBuf, String, String)>>,
        executed_installers: RefCell<Vec<PathBuf>>,
    }

    impl FakeActivationRuntime {
        fn with_results(
            installer_results: impl IntoIterator<Item = Result<bool, String>>,
            product_match_results: impl IntoIterator<Item = bool>,
            service_health_results: impl IntoIterator<Item = bool>,
        ) -> Self {
            Self {
                installer_results: RefCell::new(installer_results.into_iter().collect()),
                product_match_results: RefCell::new(product_match_results.into_iter().collect()),
                service_health_results: RefCell::new(service_health_results.into_iter().collect()),
                ..Self::default()
            }
        }
    }

    impl ActivationRuntime for FakeActivationRuntime {
        fn verify_installer(
            &self,
            installer_path: &Path,
            _expected_sha256: &str,
            expected_signer_sha256: &str,
        ) -> Result<(), String> {
            self.verified_installers
                .borrow_mut()
                .push(installer_path.to_path_buf());
            self.verified_signers
                .borrow_mut()
                .push(expected_signer_sha256.to_string());
            if let Some(error) = self.verification_error.borrow().clone() {
                return Err(error);
            }
            Ok(())
        }

        fn snapshot_rollback_installer(
            &self,
            current_installer_path: &Path,
            task_installer_path: &Path,
            expected_sha256: &str,
            expected_signer_sha256: &str,
        ) -> Result<(), String> {
            self.rollback_snapshots.borrow_mut().push((
                current_installer_path.to_path_buf(),
                task_installer_path.to_path_buf(),
                expected_sha256.to_string(),
                expected_signer_sha256.to_string(),
            ));
            Ok(())
        }

        fn run_installer(&self, installer_path: &Path) -> Result<bool, String> {
            self.executed_installers
                .borrow_mut()
                .push(installer_path.to_path_buf());
            self.installer_results
                .borrow_mut()
                .pop_front()
                .expect("test installer result")
        }

        fn installed_product_matches(
            &self,
            _record: &UpdateActivationRecord,
            _build_number: u32,
        ) -> bool {
            self.product_match_results
                .borrow_mut()
                .pop_front()
                .expect("test product match result")
        }

        fn service_ready(&self, _port: u16) -> bool {
            self.service_health_results
                .borrow_mut()
                .pop_front()
                .expect("test service health result")
        }
    }

    #[cfg(windows)]
    fn saved_activation(
        activation_record: UpdateActivationRecord,
        data_root: &Path,
    ) -> UpdateActivationStore {
        std::fs::create_dir(data_root).unwrap();
        let update_root = data_root.join("updates");
        platform::ensure_private_directory(&update_root).unwrap();
        platform::ensure_private_directory(&update_root.join("rollback")).unwrap();
        let store = UpdateActivationStore::new(data_root.to_path_buf());
        store.save(&activation_record).unwrap();
        store
    }

    #[test]
    fn record_validation_binds_identity_paths_and_terminal_error() {
        let root = PathBuf::from("C:/Users/Public/Pixels/px_data");
        let valid = record(&root);
        valid.validate(&root).unwrap();
        let mut escaped = valid.clone();
        escaped.artifact_path = root.join("other/installer.exe");
        assert!(escaped.validate(&root).is_err());
        let mut wrong_distribution = valid.clone();
        wrong_distribution.distribution = "development".into();
        assert!(wrong_distribution.validate(&root).is_err());
        let mut cross_oem = valid.clone();
        cross_oem.distribution = "oem".into();
        cross_oem.release_namespace = "oem.acme-cloud".into();
        cross_oem.oem_id = Some("other-cloud".into());
        cross_oem.oem_profile_sha256 = Some("e".repeat(64));
        cross_oem.company = "Acme Systems".into();
        assert!(cross_oem.validate(&root).is_err());
        let mut oem = valid.clone();
        oem.distribution = "oem".into();
        oem.release_namespace = "oem.acme-cloud".into();
        oem.oem_id = Some("acme-cloud".into());
        oem.oem_profile_sha256 = Some("e".repeat(64));
        oem.company = "Acme Systems".into();
        assert!(oem.validate(&root).is_ok());
        let mut missing_rollback_signer = valid.clone();
        missing_rollback_signer.rollback_signer_sha256 = None;
        assert!(missing_rollback_signer.validate(&root).is_err());
        let mut invalid_target_signer = valid.clone();
        invalid_target_signer.target_signer_sha256 = "not-a-certificate-pin".into();
        assert!(invalid_target_signer.validate(&root).is_err());
        let mut failed_without_error = valid;
        failed_without_error.phase = ActivationPhase::Failed;
        assert!(failed_without_error.validate(&root).is_err());
    }

    #[test]
    fn rollback_cache_path_is_isolated_by_exact_oem_identity() {
        let temporary_directory = tempfile::tempdir().unwrap();
        let data_root = temporary_directory.path().join("service-data");
        platform::ensure_private_directory(&data_root).unwrap();
        let update_root = data_root.join("updates");
        platform::ensure_private_directory(&update_root).unwrap();
        platform::ensure_private_directory(&update_root.join("rollback")).unwrap();

        let official_path =
            rollback_installer_path(&data_root, "remote", "official", None).unwrap();
        let oem_path =
            rollback_installer_path(&data_root, "remote", "oem", Some("acme-cloud")).unwrap();

        assert!(official_path.ends_with("remote-official-pixels-current.exe"));
        assert!(oem_path.ends_with("remote-oem-acme-cloud-current.exe"));
        assert!(rollback_installer_path(&data_root, "remote", "oem", None).is_err());
        assert!(
            rollback_installer_path(&data_root, "remote", "customer", Some("acme-cloud")).is_err()
        );
    }

    #[test]
    fn installed_product_match_rejects_another_oem_domain() {
        let temporary_directory = tempfile::tempdir().unwrap();
        let install_directory = temporary_directory.path().join("installed");
        std::fs::create_dir(&install_directory).unwrap();
        let mut activation_record = record(temporary_directory.path());
        activation_record.product = "remote".into();
        activation_record.distribution = "oem".into();
        activation_record.release_namespace = "oem.acme-cloud".into();
        activation_record.oem_id = Some("acme-cloud".into());
        activation_record.oem_profile_sha256 = Some("e".repeat(64));
        activation_record.company = "Acme Systems".into();
        activation_record.install_directory = install_directory.clone();

        let product_manifest = |oem_id: &str| {
            serde_json::json!({
                "schema_version": 3,
                "product": "remote",
                "distribution": "oem",
                "release_namespace": format!("oem.{oem_id}"),
                "oem_id": oem_id,
                "oem_profile_sha256": "e".repeat(64),
                "edition": "REMOTE",
                "company": "Acme Systems",
                "product_version": "1.0.11",
                "product_version_code": 11,
                "signer_certificate_sha256": "c".repeat(64),
                "capabilities": [
                    "browser_remote",
                    "desktop_client",
                    "desktop_host",
                    "file_transfer",
                    "joystick",
                    "rdp_client",
                    "rdp_host",
                    "system_information",
                    "virtual_display"
                ]
            })
        };
        std::fs::write(
            install_directory.join("product-manifest.json"),
            serde_json::to_vec(&product_manifest("acme-cloud")).unwrap(),
        )
        .unwrap();
        assert!(installed_product_matches(&activation_record, 11));

        std::fs::write(
            install_directory.join("product-manifest.json"),
            serde_json::to_vec(&product_manifest("other-cloud")).unwrap(),
        )
        .unwrap();
        assert!(!installed_product_matches(&activation_record, 11));
    }

    #[test]
    fn service_health_requires_a_websocket_upgrade_response() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = thread::spawn(move || {
            let (mut connection, _) = listener.accept().unwrap();
            let mut request = [0_u8; 512];
            let read = connection.read(&mut request).unwrap();
            assert!(request[..read].starts_with(b"GET / HTTP/1.1"));
            connection
                .write_all(
                    b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n",
                )
                .unwrap();
        });
        assert!(service_protocol_ready(port));
        server.join().unwrap();
    }

    #[cfg(windows)]
    #[test]
    fn successful_activation_records_installed_only_after_identity_and_health() {
        let temporary_directory = tempfile::tempdir().unwrap();
        let data_root = temporary_directory.path().join("service-data");
        let activation_record = record(&data_root);
        let task_id = activation_record.task_id;
        let store = saved_activation(activation_record.clone(), &data_root);
        let runtime = FakeActivationRuntime::with_results([Ok(true)], [true], [true]);

        run_authorized_update_with(&runtime, data_root).unwrap();

        let persisted = store.load().unwrap().unwrap();
        assert_eq!(persisted.phase, ActivationPhase::Installed);
        assert_eq!(persisted.task_id, task_id);
        assert_eq!(
            runtime.verified_installers.borrow().as_slice(),
            &[activation_record.artifact_path]
        );
        assert_eq!(runtime.rollback_snapshots.borrow().len(), 1);
        assert_eq!(
            runtime.verified_signers.borrow().as_slice(),
            &["c".repeat(64)]
        );
        assert_eq!(runtime.rollback_snapshots.borrow()[0].3, "d".repeat(64));
        assert_eq!(runtime.executed_installers.borrow().len(), 1);
    }

    #[cfg(windows)]
    #[test]
    fn failed_installer_restores_the_verified_previous_package() {
        let temporary_directory = tempfile::tempdir().unwrap();
        let data_root = temporary_directory.path().join("service-data");
        let activation_record = record(&data_root);
        let store = saved_activation(activation_record, &data_root);
        let runtime =
            FakeActivationRuntime::with_results([Ok(true), Ok(true)], [false, true], [true]);

        let error = run_authorized_update_with(&runtime, data_root).unwrap_err();

        assert_eq!(error, "installer_failed");
        let persisted = store.load().unwrap().unwrap();
        assert_eq!(persisted.phase, ActivationPhase::Failed);
        assert_eq!(persisted.error_code.as_deref(), Some("installer_failed"));
        assert_eq!(runtime.executed_installers.borrow().len(), 2);
    }

    #[cfg(windows)]
    #[test]
    fn failed_rollback_is_a_distinct_terminal_result() {
        let temporary_directory = tempfile::tempdir().unwrap();
        let data_root = temporary_directory.path().join("service-data");
        let activation_record = record(&data_root);
        let store = saved_activation(activation_record, &data_root);
        let runtime = FakeActivationRuntime::with_results(
            [Ok(false), Err("rollback process refused to start".into())],
            [],
            [],
        );

        let error = run_authorized_update_with(&runtime, data_root).unwrap_err();

        assert_eq!(error, "rollback_failed");
        let persisted = store.load().unwrap().unwrap();
        assert_eq!(persisted.phase, ActivationPhase::Failed);
        assert_eq!(persisted.error_code.as_deref(), Some("rollback_failed"));
        assert_eq!(runtime.executed_installers.borrow().len(), 2);
    }

    #[cfg(windows)]
    #[test]
    fn changed_prepared_installer_never_executes_or_replaces_the_previous_package() {
        let temporary_directory = tempfile::tempdir().unwrap();
        let data_root = temporary_directory.path().join("service-data");
        let activation_record = record(&data_root);
        let store = saved_activation(activation_record, &data_root);
        let runtime = FakeActivationRuntime::default();
        runtime.verification_error.replace(Some(
            "prepared update installer changed after verification".into(),
        ));

        let error = run_authorized_update_with(&runtime, data_root).unwrap_err();

        assert_eq!(
            error,
            "prepared update installer changed after verification"
        );
        let persisted = store.load().unwrap().unwrap();
        assert_eq!(persisted.phase, ActivationPhase::Failed);
        assert_eq!(persisted.error_code.as_deref(), Some("activation_failed"));
        assert!(runtime.rollback_snapshots.borrow().is_empty());
        assert!(runtime.executed_installers.borrow().is_empty());
    }

    #[cfg(windows)]
    #[test]
    fn expired_authorization_is_not_consumed() {
        let temporary_directory = tempfile::tempdir().unwrap();
        let data_root = temporary_directory.path().join("service-data");
        let mut activation_record = record(&data_root);
        activation_record.lease_until = Utc::now() - chrono::TimeDelta::seconds(1);
        let store = saved_activation(activation_record, &data_root);
        let runtime = FakeActivationRuntime::default();

        let error = run_authorized_update_with(&runtime, data_root).unwrap_err();

        assert_eq!(
            error,
            "authorized update record is stale or already consumed"
        );
        assert_eq!(
            store.load().unwrap().unwrap().phase,
            ActivationPhase::Authorized
        );
        assert!(runtime.verified_installers.borrow().is_empty());
        assert!(runtime.executed_installers.borrow().is_empty());
    }
}
