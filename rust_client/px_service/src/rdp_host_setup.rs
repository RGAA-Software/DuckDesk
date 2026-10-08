//! Bounded, hidden host discovery. No account/session/RDS-policy mutations.
use base64::{engine::general_purpose::STANDARD, Engine};
use serde::Deserialize;
use std::{path::PathBuf, process::Stdio, time::Duration};
use tokio::{io::AsyncReadExt, process::Command};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RdpHostFacts {
    target_domain: String,
    target_certificate_sha256: String,
}

pub async fn initialize() -> Result<(), String> {
    let powershell_script = include_str!("rdp_host_probe.ps1");
    let script_bytes = powershell_script
        .encode_utf16()
        .flat_map(u16::to_le_bytes)
        .collect::<Vec<_>>();
    let system_root =
        std::env::var_os("SystemRoot").ok_or("Windows SystemRoot directory unavailable")?;
    let powershell =
        PathBuf::from(system_root).join("System32/WindowsPowerShell/v1.0/powershell.exe");
    let mut command = Command::new(powershell);
    command
        .args([
            "-NoLogo",
            "-NoProfile",
            "-NonInteractive",
            "-EncodedCommand",
            &STANDARD.encode(script_bytes),
        ])
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .kill_on_drop(true)
        .creation_flags(0x0800_0000);
    let mut child = command
        .spawn()
        .map_err(|_| "RDP host discovery could not start")?;
    let mut output = child
        .stdout
        .take()
        .ok_or("RDP host discovery stdout unavailable")?
        .take(8193);
    let mut diagnostics = child
        .stderr
        .take()
        .ok_or("RDP host discovery stderr unavailable")?
        .take(8193);
    let mut output_bytes = Vec::new();
    let mut diagnostic_bytes = Vec::new();
    let result = tokio::time::timeout(Duration::from_secs(15), async {
        tokio::try_join!(
            child.wait(),
            output.read_to_end(&mut output_bytes),
            diagnostics.read_to_end(&mut diagnostic_bytes)
        )
    })
    .await;
    let status = match result {
        Ok(Ok((status, _, _))) => status,
        Ok(Err(_)) => return Err("RDP host discovery output failed".into()),
        Err(_) => {
            child
                .start_kill()
                .map_err(|_| "RDP host discovery termination failed")?;
            tokio::time::timeout(Duration::from_secs(2), child.wait())
                .await
                .map_err(|_| "RDP host discovery termination was not confirmed")?
                .map_err(|_| "RDP host discovery termination failed")?;
            return Err("RDP host discovery timed out".into());
        }
    };
    if !status.success() || output_bytes.len() > 8192 || diagnostic_bytes.len() > 8192 {
        // The probe emits only host/certificate metadata, never credentials.
        return Err(format!(
            "RDP host discovery rejected: {}",
            String::from_utf8_lossy(&diagnostic_bytes).trim()
        ));
    }
    let facts: RdpHostFacts = serde_json::from_slice(&output_bytes)
        .map_err(|_| "RDP host discovery returned invalid metadata")?;
    let executable = std::env::current_exe().map_err(|_| "RDP host executable unavailable")?;
    let runtime_directory = executable
        .parent()
        .ok_or("RDP host executable has no directory")?
        .join("rdp");
    tokio::task::spawn_blocking(move || {
        service_core::rdp_host_identity::initialize(
            &runtime_directory,
            facts.target_domain,
            facts.target_certificate_sha256,
        )
    })
    .await
    .map_err(|_| "RDP host identity initialization task failed")??;
    Ok(())
}
