//! Installer-supplied public trust material. Credentials are never deployment
//! settings: they arrive exclusively in the authenticated Console start command.

use crate::rdp_account::RdpAccountSpec;
use crate::rdp_workspace::{valid_identifier, RdpBootstrapBinding, WorkspaceStore};
use serde::Deserialize;
use std::io::Read;
use std::path::{Path, PathBuf};
use zeroize::Zeroizing;

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RdpDeployment {
    pub schema: u32,
    pub freerdp_revision: String,
    pub target_domain: String,
    pub target_certificate_sha256: String,
    pub proxy_certificate_sha256: String,
}

impl RdpDeployment {
    pub fn load(directory: &Path) -> Result<Self, String> {
        let file = std::fs::File::open(directory.join("deployment.json"))
            .map_err(|_| "RDP trusted deployment manifest is not installed".to_string())?;
        let mut bytes = Vec::new();
        file.take(8193).read_to_end(&mut bytes).map_err(|_| "RDP deployment read failed".to_string())?;
        if bytes.len() > 8192 { return Err("RDP deployment exceeds limit".into()); }
        let deployment: Self = serde_json::from_slice(&bytes).map_err(|_| "RDP deployment invalid".to_string())?;
        deployment.validate()?;
        for name in ["freerdp-proxy.exe", "proxy.crt", "proxy.key", "proxy/proxy-gammaray-policy-plugin.dll"] {
            if !directory.join(name).is_file() { return Err("RDP required runtime/security component is missing".into()); }
        }
        Ok(deployment)
    }

    fn validate(&self) -> Result<(), String> {
        if self.schema != 1 || self.freerdp_revision != "aa8650b300aa4cabd85d9c72b431301509b9043f"
            || self.target_domain.is_empty() || self.target_domain.len() > 15
            || !self.target_domain.bytes().all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
            || ![self.target_certificate_sha256.as_str(), self.proxy_certificate_sha256.as_str()].into_iter()
                .all(|pin| pin.len() == 64 && pin.bytes().all(|byte| byte.is_ascii_hexdigit())) {
            return Err("RDP deployment version, local domain or certificate identity invalid".into());
        }
        Ok(())
    }

    fn configuration(&self, directory: &Path, account: &RdpAccountSpec, port: u16) -> Result<Zeroizing<String>, String> {
        self.validate()?;
        account.validate()?;
        let cert = directory.join("proxy.crt").to_string_lossy().to_string();
        let key = directory.join("proxy.key").to_string_lossy().to_string();
        if port == 0 || !directory.is_absolute() || [&cert, &key, account.password.as_str()].into_iter()
            .any(|value| value.chars().any(|c| matches!(c, '\r' | '\n' | '\0'))) {
            return Err("RDP proxy configuration contains an invalid value".into());
        }
        // FreeRDP 3.31 corrected the old demo's swapped audio INI keys.
        // The pinned ABI and mandatory policy both enforce microphone=false.
        Ok(Zeroizing::new(format!(concat!(
            "[Server]\nHost=127.0.0.1\nPort={}\n[Target]\nHost=127.0.0.1\nPort=3389\nFixedTarget=true\nUser={}\nDomain={}\nPassword={}\n",
            // RDPDR is required for Windows audio. The mandatory policy module
            // permits handshake/zero devices only, rejecting all device I/O.
            "[Channels]\nGFX=true\nDisplayControl=true\nClipboard=true\nAudioInput=false\nAudioOutput=true\nDeviceRedirection=true\n",
            "VideoRedirection=false\nCameraRedirection=false\nRemoteApp=false\nPassthroughIsBlacklist=false\n",
            "Passthrough=drdynvc,cliprdr,rdpsnd,rdpdr,Microsoft::Windows::RDS::Graphics,Microsoft::Windows::RDS::DisplayControl,AUDIO_PLAYBACK_DVC,AUDIO_PLAYBACK_LOSSY_DVC\n",
            "[Input]\nKeyboard=true\nMouse=true\nMultitouch=false\n[Security]\nServerTlsSecurity=true\nServerNlaSecurity=true\n",
            "ServerRdpSecurity=false\nClientTlsSecurity=true\nClientNlaSecurity=true\nClientRdpSecurity=false\nClientAllowFallbackToTls=false\n",
            "[Plugins]\nModules=gammaray-policy\nRequired=gammaray-policy\n[Certificates]\nCertificateFile={}\nPrivateKeyFile={}\n"
        ), port, account.account_name, self.target_domain, account.password.as_str(), cert, key)))
    }
}

/// Removes only the exact encrypted, per-start handoff file on cancellation or
/// failure. The Render consumes it on success. Workspace identity is persistent.
pub struct StagedRdpBootstrap {
    path: PathBuf,
    pub binding: RdpBootstrapBinding,
}
impl Drop for StagedRdpBootstrap {
    fn drop(&mut self) {
        let _ = std::fs::remove_file(&self.path);
        // Render deletes plaintext once the proxy is Ready. Also clean this
        // exact launch's temporary configuration after timeout/start failure.
        let _ = std::fs::remove_file(self.path.with_extension("proxy.ini"));
    }
}

#[cfg(windows)]
pub fn prepare_runtime(directory: &Path, spec: &RdpAccountSpec, instance_id: &str, app_id: &str, node_id: &str,
    device_id: &str) -> Result<StagedRdpBootstrap, String> {
    if !valid_identifier(instance_id) { return Err("RDP runtime instance invalid".into()); }
    let deployment = RdpDeployment::load(directory)?;
    let store = WorkspaceStore::open(&directory.join("workspaces"))?;
    store.provision(spec, app_id, node_id, device_id)?;
    let listener = std::net::TcpListener::bind((std::net::Ipv4Addr::LOCALHOST, 0))
        .map_err(|_| "RDP loopback port reservation failed".to_string())?;
    let proxy_port = listener.local_addr().map_err(|_| "RDP loopback port unavailable".to_string())?.port();
    let binding = RdpBootstrapBinding { workspace_id: spec.workspace_id.clone(), instance_id: instance_id.into(), node_id: node_id.into(),
        device_id: device_id.into(), proxy_port, target_certificate_sha256: deployment.target_certificate_sha256.clone(),
        proxy_certificate_sha256: deployment.proxy_certificate_sha256.clone() };
    let config = deployment.configuration(directory, spec, proxy_port)?;
    let path = store.stage_bootstrap(&binding, config.as_bytes())?;
    // Upstream binds itself; Render verifies listener ownership against its child
    // PID, so an intervening port bind cannot impersonate successful readiness.
    drop(listener);
    Ok(StagedRdpBootstrap { path, binding })
}

#[cfg(test)]
mod tests {
    use super::*;
    fn deployment() -> RdpDeployment {
        RdpDeployment { schema: 1, freerdp_revision: "aa8650b300aa4cabd85d9c72b431301509b9043f".into(),
            target_domain: "RDP-NODE".into(), target_certificate_sha256: "a".repeat(64), proxy_certificate_sha256: "b".repeat(64) }
    }
    fn account() -> RdpAccountSpec {
        RdpAccountSpec { workspace_id: "workspace".into(), account_name: "grdp_testaccount".into(),
            password: Zeroizing::new("aA1!01234567890123456789012345678901".into()), credential_version: 1, expected_sid: None }
    }
    #[test]
    fn configuration_requires_policy_fixed_target_and_nla_without_host_devices() {
        let config = deployment().configuration(&std::env::temp_dir(), &account(), 13389).unwrap();
        for required in ["Required=gammaray-policy", "FixedTarget=true", "ClientAllowFallbackToTls=false", "DeviceRedirection=true",
            "Host=127.0.0.1", "ServerNlaSecurity=true", "ClientNlaSecurity=true", "AudioInput=false\nAudioOutput=true",
            ",AUDIO_PLAYBACK_DVC,AUDIO_PLAYBACK_LOSSY_DVC\n"] {
            assert!(config.contains(required));
        }
    }
    #[test]
    fn newline_injection_and_unpinned_dependencies_are_rejected() {
        let mut account = account(); account.password.push_str("\nFixedTarget=false");
        assert!(deployment().configuration(&std::env::temp_dir(), &account, 13389).is_err());
        let mut deployment = deployment(); deployment.freerdp_revision = "latest".into();
        assert!(deployment.validate().is_err());
    }
    #[test]
    fn dropping_staged_launch_removes_only_its_own_temporary_files() {
        let nonce = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos();
        let root = std::env::temp_dir().join(format!("gammaray-rdp-stage-{}-{nonce}", std::process::id()));
        std::fs::create_dir(&root).unwrap();
        let path = root.join("instance.bootstrap");
        let private_path = path.with_extension("proxy.ini");
        let preserved = [root.join("other.bootstrap"), root.join("other.proxy.ini"), root.join("workspace.identity.json")];
        for file in [&path, &private_path].into_iter().chain(preserved.iter()) {
            std::fs::write(file, b"non-secret test fixture").unwrap();
        }
        let binding = RdpBootstrapBinding { workspace_id: "workspace".into(), instance_id: "instance".into(), node_id: "node".into(),
            device_id: "device".into(), proxy_port: 13389, target_certificate_sha256: "a".repeat(64), proxy_certificate_sha256: "b".repeat(64) };
        drop(StagedRdpBootstrap { path: path.clone(), binding: binding.clone() });
        assert!(!path.exists());
        assert!(!private_path.exists());
        // Missing files and repeated cleanup remain harmless.
        drop(StagedRdpBootstrap { path, binding });
        for file in preserved {
            assert_eq!(std::fs::read(&file).unwrap(), b"non-secret test fixture");
            std::fs::remove_file(file).unwrap();
        }
        std::fs::remove_dir(root).unwrap();
    }
}
