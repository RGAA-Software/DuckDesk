//! Local RDP TLS material is machine state, never a distributable private key.
use crate::rdp_deployment::RdpDeployment;
use crate::rdp_workspace::{atomic_replace, WorkspaceStore};
use base64::{engine::general_purpose::STANDARD, Engine};
use rcgen::PublicKeyData;
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use zeroize::Zeroizing;

pub fn state_directory(runtime_directory: &Path) -> Result<PathBuf, String> {
    let program_data =
        std::env::var_os("ProgramData").ok_or("Windows ProgramData directory unavailable")?;
    let normalized_runtime = runtime_directory
        .canonicalize()
        .map_err(|_| "RDP runtime directory unavailable")?
        .to_string_lossy()
        .replace('/', "\\")
        .to_lowercase();
    let installation_key = format!("{:x}", Sha256::digest(normalized_runtime.as_bytes()));
    Ok(PathBuf::from(program_data)
        .join("Pixels")
        .join("RdpHost")
        .join(installation_key))
}

#[cfg(windows)]
pub fn initialize(
    runtime_directory: &Path,
    target_domain: String,
    target_pin: String,
) -> Result<RdpDeployment, String> {
    let state_root = state_directory(runtime_directory)?;
    initialize_at(runtime_directory, &state_root, target_domain, target_pin)
}

#[cfg(windows)]
fn initialize_at(
    runtime_directory: &Path,
    state_root: &Path,
    target_domain: String,
    target_pin: String,
) -> Result<RdpDeployment, String> {
    for component in ["px_rdp_proxy.exe", "proxy/px_rdp_policy.dll"] {
        if !runtime_directory.join(component).is_file() {
            return Err("RDP required runtime component is missing".into());
        }
    }
    let parent = state_root
        .parent()
        .ok_or("RDP state directory has no parent")?;
    std::fs::create_dir_all(parent).map_err(|_| "RDP state parent creation failed")?;
    let store = WorkspaceStore::open(state_root)?;
    use std::os::windows::fs::OpenOptionsExt;
    let _setup_lock = std::fs::OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .share_mode(0)
        .custom_flags(0x00200000)
        .open(store.root().join("host.lock"))
        .map_err(|_| "RDP host initialization is busy")?;
    let certificate_path = state_root.join("px_rdp_proxy.crt");
    let private_key_path = state_root.join("px_rdp_proxy.key");
    let proxy_pin = match (certificate_path.is_file(), private_key_path.is_file()) {
        (false, false) => {
            let identity =
                rcgen::generate_simple_self_signed(vec!["localhost".into(), "127.0.0.1".into()])
                    .map_err(|_| "RDP proxy TLS identity generation failed")?;
            let private_key =
                Zeroizing::new(identity.signing_key.serialize_pem().replace("\r\n", "\n"));
            atomic_replace(&private_key_path, private_key.as_bytes())?;
            atomic_replace(
                &certificate_path,
                identity.cert.pem().replace("\r\n", "\n").as_bytes(),
            )?;
            format!("{:x}", Sha256::digest(identity.cert.der().as_ref()))
        }
        (true, true) => {
            let certificate = std::fs::read_to_string(&certificate_path)
                .map_err(|_| "RDP proxy certificate read failed")?;
            let private_key = Zeroizing::new(
                std::fs::read_to_string(&private_key_path)
                    .map_err(|_| "RDP proxy key read failed")?,
            );
            let signing_key = rcgen::KeyPair::from_pem(&private_key)
                .map_err(|_| "RDP proxy key invalid; refusing replacement")?;
            let certificate_der = certificate_der(&certificate)?;
            let (_, parsed_certificate) = x509_parser::parse_x509_certificate(&certificate_der)
                .map_err(|_| "RDP proxy certificate invalid; refusing replacement")?;
            if parsed_certificate.public_key().raw != signing_key.subject_public_key_info() {
                return Err("RDP proxy certificate/key mismatch; refusing replacement".into());
            }
            // The pinned FreeRDP PEM reader uses Windows text-mode fread with
            // a byte-count from ftell. LF prevents its CRLF short-read failure.
            // Normalize encoding only, after verifying the exact key identity.
            let normalized_certificate = certificate.replace("\r\n", "\n");
            let normalized_key = Zeroizing::new(private_key.replace("\r\n", "\n"));
            if normalized_certificate != certificate {
                atomic_replace(&certificate_path, normalized_certificate.as_bytes())?;
            }
            if normalized_key.as_str() != private_key.as_str() {
                atomic_replace(&private_key_path, normalized_key.as_bytes())?;
            }
            certificate_pin(&certificate)?
        }
        (false, true) if !state_root.join("px_rdp_deployment.json").exists() => {
            // Resume an interrupted first initialization using its persisted key.
            // A published identity may never be regenerated this way.
            let private_key = Zeroizing::new(
                std::fs::read_to_string(&private_key_path)
                    .map_err(|_| "RDP proxy key read failed")?,
            );
            let signing_key = rcgen::KeyPair::from_pem(&private_key)
                .map_err(|_| "RDP proxy key invalid; refusing replacement")?;
            let certificate =
                rcgen::CertificateParams::new(vec!["localhost".into(), "127.0.0.1".into()])
                    .map_err(|_| "RDP proxy certificate parameters invalid")?
                    .self_signed(&signing_key)
                    .map_err(|_| "RDP proxy certificate recovery failed")?;
            atomic_replace(
                &private_key_path,
                Zeroizing::new(private_key.replace("\r\n", "\n")).as_bytes(),
            )?;
            atomic_replace(
                &certificate_path,
                certificate.pem().replace("\r\n", "\n").as_bytes(),
            )?;
            format!("{:x}", Sha256::digest(certificate.der().as_ref()))
        }
        _ => return Err("RDP proxy identity incomplete; refusing silent key replacement".into()),
    };
    let deployment = RdpDeployment {
        schema: 1,
        freerdp_revision: "aa8650b300aa4cabd85d9c72b431301509b9043f".into(),
        target_domain,
        target_certificate_sha256: target_pin,
        proxy_certificate_sha256: proxy_pin,
    };
    deployment.validate()?;
    let manifest_path = state_root.join("px_rdp_deployment.json");
    if manifest_path.exists() {
        let previous = RdpDeployment::load_state(runtime_directory, state_root)?;
        if previous.proxy_certificate_sha256 != deployment.proxy_certificate_sha256 {
            return Err("RDP persisted proxy identity changed; refusing replacement".into());
        }
    }
    let manifest =
        serde_json::to_vec(&deployment).map_err(|_| "RDP host manifest serialization failed")?;
    atomic_replace(&manifest_path, &manifest)?;
    RdpDeployment::load_state(runtime_directory, state_root)
}

fn certificate_pin(certificate_pem: &str) -> Result<String, String> {
    Ok(format!(
        "{:x}",
        Sha256::digest(certificate_der(certificate_pem)?)
    ))
}

fn certificate_der(certificate_pem: &str) -> Result<Vec<u8>, String> {
    if !certificate_pem.starts_with("-----BEGIN CERTIFICATE-----")
        || !certificate_pem
            .trim_end()
            .ends_with("-----END CERTIFICATE-----")
    {
        return Err("RDP proxy certificate PEM invalid".into());
    }
    let encoded_certificate = certificate_pem
        .lines()
        .filter(|line| !line.starts_with("-----"))
        .collect::<String>();
    STANDARD
        .decode(encoded_certificate)
        .map_err(|_| "RDP proxy certificate encoding invalid".into())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn proxy_certificate_pin_is_the_exact_der_identity() {
        let identity = rcgen::generate_simple_self_signed(vec!["localhost".into()]).unwrap();
        assert_eq!(
            certificate_pin(&identity.cert.pem()).unwrap(),
            format!("{:x}", Sha256::digest(identity.cert.der().as_ref()))
        );
        assert!(certificate_pin("invalid certificate").is_err());
    }

    #[cfg(windows)]
    #[test]
    fn interrupted_first_initialization_resumes_without_replacing_its_key() {
        let fixture = tempfile::tempdir().unwrap();
        let runtime_directory = fixture.path().join("runtime");
        std::fs::create_dir_all(runtime_directory.join("proxy")).unwrap();
        for component in ["px_rdp_proxy.exe", "proxy/px_rdp_policy.dll"] {
            std::fs::write(runtime_directory.join(component), []).unwrap();
        }
        let state_root = fixture.path().join("private-host");
        let state_store = WorkspaceStore::open(&state_root).unwrap();
        let original_key = Zeroizing::new(rcgen::KeyPair::generate().unwrap().serialize_pem());
        atomic_replace(
            &state_store.root().join("px_rdp_proxy.key"),
            original_key.as_bytes(),
        )
        .unwrap();
        initialize_at(
            &runtime_directory,
            &state_root,
            "MC-90".into(),
            "a".repeat(64),
        )
        .unwrap();
        let recovered_key =
            Zeroizing::new(std::fs::read_to_string(state_root.join("px_rdp_proxy.key")).unwrap());
        assert!(original_key.replace("\r\n", "\n") == recovered_key.as_str());
        assert!(!recovered_key.contains('\r'));
    }

    #[cfg(windows)]
    #[test]
    fn host_restart_and_rds_pin_refresh_preserve_proxy_key_and_workspace_files() {
        let fixture = tempfile::tempdir().unwrap();
        let runtime_directory = fixture.path().join("runtime");
        std::fs::create_dir_all(runtime_directory.join("proxy")).unwrap();
        for component in ["px_rdp_proxy.exe", "proxy/px_rdp_policy.dll"] {
            std::fs::write(runtime_directory.join(component), []).unwrap();
        }
        let state_root = fixture.path().join("private-host");
        let first = initialize_at(
            &runtime_directory,
            &state_root,
            "MC-90".into(),
            "a".repeat(64),
        )
        .unwrap();
        let original_key = std::fs::read(state_root.join("px_rdp_proxy.key")).unwrap();
        let workspace = WorkspaceStore::open(&state_root.join("workspaces")).unwrap();
        std::fs::write(
            workspace.root().join("persistent.identity.json"),
            b"preserved",
        )
        .unwrap();
        let refreshed = initialize_at(
            &runtime_directory,
            &state_root,
            "MC-90".into(),
            "b".repeat(64),
        )
        .unwrap();
        assert_eq!(
            first.proxy_certificate_sha256,
            refreshed.proxy_certificate_sha256
        );
        assert_eq!(
            original_key,
            std::fs::read(state_root.join("px_rdp_proxy.key")).unwrap()
        );
        assert_eq!(
            std::fs::read(workspace.root().join("persistent.identity.json")).unwrap(),
            b"preserved"
        );
        assert_eq!(refreshed.target_certificate_sha256, "b".repeat(64));
    }

    #[cfg(windows)]
    #[test]
    fn damaged_host_identity_never_silently_rotates_the_private_key() {
        let fixture = tempfile::tempdir().unwrap();
        let runtime_directory = fixture.path().join("runtime");
        std::fs::create_dir_all(runtime_directory.join("proxy")).unwrap();
        for component in ["px_rdp_proxy.exe", "proxy/px_rdp_policy.dll"] {
            std::fs::write(runtime_directory.join(component), []).unwrap();
        }
        let state_root = fixture.path().join("private-host");
        initialize_at(
            &runtime_directory,
            &state_root,
            "MC-90".into(),
            "a".repeat(64),
        )
        .unwrap();
        let original_key = std::fs::read(state_root.join("px_rdp_proxy.key")).unwrap();
        std::fs::write(state_root.join("px_rdp_proxy.crt"), b"damaged").unwrap();
        assert!(initialize_at(
            &runtime_directory,
            &state_root,
            "MC-90".into(),
            "a".repeat(64)
        )
        .is_err());
        assert_eq!(
            original_key,
            std::fs::read(state_root.join("px_rdp_proxy.key")).unwrap()
        );
    }

    #[cfg(windows)]
    #[test]
    fn pem_line_endings_are_lf_and_normalization_preserves_der_and_private_key_identity() {
        let fixture = tempfile::tempdir().unwrap();
        let runtime_directory = fixture.path().join("runtime");
        std::fs::create_dir_all(runtime_directory.join("proxy")).unwrap();
        for component in ["px_rdp_proxy.exe", "proxy/px_rdp_policy.dll"] {
            std::fs::write(runtime_directory.join(component), []).unwrap();
        }
        let state_root = fixture.path().join("private-host");
        let original = initialize_at(
            &runtime_directory,
            &state_root,
            "MC-90".into(),
            "a".repeat(64),
        )
        .unwrap();
        let key_path = state_root.join("px_rdp_proxy.key");
        let certificate_path = state_root.join("px_rdp_proxy.crt");
        let original_key = Zeroizing::new(std::fs::read_to_string(&key_path).unwrap());
        let original_certificate = std::fs::read_to_string(&certificate_path).unwrap();
        assert!(!original_key.contains('\r'));
        assert!(!original_certificate.contains('\r'));
        atomic_replace(
            &key_path,
            Zeroizing::new(original_key.replace('\n', "\r\n")).as_bytes(),
        )
        .unwrap();
        atomic_replace(
            &certificate_path,
            original_certificate.replace('\n', "\r\n").as_bytes(),
        )
        .unwrap();
        let normalized = initialize_at(
            &runtime_directory,
            &state_root,
            "MC-90".into(),
            "a".repeat(64),
        )
        .unwrap();
        assert_eq!(
            original.proxy_certificate_sha256,
            normalized.proxy_certificate_sha256
        );
        assert!(original_key.as_bytes() == std::fs::read(&key_path).unwrap());
        assert_eq!(
            original_certificate.as_bytes(),
            std::fs::read(&certificate_path).unwrap()
        );
    }
}
