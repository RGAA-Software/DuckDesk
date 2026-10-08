use crate::{
    activate_single_server_relay, initialize_single_server, SingleServerLayout,
    SingleServerSetupInput,
};
use reqwest::Certificate;
use serde_json::{json, Value};
#[cfg(windows)]
use std::process::Command;
use std::{
    env,
    io::Write,
    net::{IpAddr, Ipv4Addr, UdpSocket},
    path::Path,
    time::Duration,
};
use tokio_util::sync::CancellationToken;
use zeroize::Zeroizing;

const DEFAULT_USERNAME: &str = "Pixels";
const DEFAULT_PASSWORD: &str = "Pixels@123";

pub async fn run_single_server_setup(
    layout: SingleServerLayout,
    expected_manifest_sha256: String,
    stop: CancellationToken,
) -> Result<(), Box<dyn std::error::Error>> {
    if layout.config_root.join("setup.complete").is_file() {
        return Ok(());
    }
    if stop.is_cancelled() {
        return Err("Single Server setup was cancelled".into());
    }
    if !layout.linux_container
        && (expected_manifest_sha256.len() != 64
            || !expected_manifest_sha256
                .bytes()
                .all(|character| character.is_ascii_digit() || (b'a'..=b'f').contains(&character)))
    {
        return Err("invalid package manifest hash".into());
    }

    let (deployment_id, console_origin) = if layout.config_root.join("console.env").is_file() {
        existing_setup_identity(&layout)?
    } else {
        let result = initialize_single_server(default_setup_input()?, &layout).await?;
        (result.deployment_id.to_string(), result.console_origin)
    };
    if !layout.linux_container {
        install_windows_services(&layout, &expected_manifest_sha256).await?;
    }
    if stop.is_cancelled() {
        return Err("Single Server setup was cancelled after service installation".into());
    }

    let pending_token = layout.config_root.join("relay-registration.token");
    let relay_token = if pending_token.is_file() {
        let mut token_bytes = px_private_files::private::read_private(&pending_token)?;
        Zeroizing::new(String::from_utf8(std::mem::take(&mut *token_bytes))?)
    } else {
        let relay_token = register_relay(&layout, &console_origin).await?;
        store_pending_token(&pending_token, relay_token.as_bytes())?;
        relay_token
    };
    activate_single_server_relay(&layout, &relay_token)?;
    if !layout.linux_container {
        install_windows_services(&layout, &expected_manifest_sha256).await?;
    }
    std::fs::remove_file(&pending_token)?;
    std::fs::write(layout.config_root.join("setup.complete"), b"ready\n")?;
    println!("Single Server ready: deployment={deployment_id} Console={console_origin}");
    Ok(())
}

fn default_setup_input() -> Result<SingleServerSetupInput, &'static str> {
    let public_host = match env::var("PIXELS_SERVER_ACCESS_HOST") {
        Ok(configured_host) if !configured_host.is_empty() => configured_host,
        Ok(_) | Err(env::VarError::NotPresent) => preferred_host()
            .ok_or("Cannot determine the server address; set PIXELS_SERVER_ACCESS_HOST")?,
        Err(_) => return Err("PIXELS_SERVER_ACCESS_HOST is invalid"),
    };
    let postgresql_host =
        env::var("PIXELS_SETUP_POSTGRESQL_HOST").unwrap_or_else(|_| "localhost".to_owned());
    Ok(setup_input(public_host, postgresql_host))
}

fn setup_input(public_host: String, postgresql_host: String) -> SingleServerSetupInput {
    SingleServerSetupInput {
        postgresql_host,
        postgresql_port: 5432,
        postgresql_administrator: DEFAULT_USERNAME.to_owned(),
        postgresql_password: DEFAULT_PASSWORD.to_owned(),
        public_host,
        initial_username: DEFAULT_USERNAME.to_owned(),
        initial_password: DEFAULT_PASSWORD.to_owned(),
    }
}

fn preferred_host() -> Option<String> {
    let route_probe = UdpSocket::bind((Ipv4Addr::UNSPECIFIED, 0)).ok()?;
    route_probe
        .connect((Ipv4Addr::new(192, 0, 2, 1), 80))
        .ok()?;
    match route_probe.local_addr().ok()?.ip() {
        IpAddr::V4(address) if !address.is_unspecified() && !address.is_loopback() => {
            Some(address.to_string())
        }
        _ => None,
    }
}

fn existing_setup_identity(layout: &SingleServerLayout) -> Result<(String, String), &'static str> {
    let contents = std::fs::read_to_string(layout.config_root.join("console.env"))
        .map_err(|_| "Existing Console configuration is unavailable")?;
    let field = |name: &str| {
        let prefix = format!("{name}=");
        let mut values = contents
            .lines()
            .filter_map(|line| line.strip_prefix(&prefix));
        match (values.next(), values.next()) {
            (Some(value), None) if !value.is_empty() => Ok(value.to_owned()),
            _ => Err("Existing Console identity is invalid"),
        }
    };
    Ok((
        field("PIXELS_DEPLOYMENT_ID")?,
        field("PIXELS_CONSOLE_PUBLIC_ORIGIN")?,
    ))
}

#[cfg(windows)]
async fn install_windows_services(
    layout: &SingleServerLayout,
    expected_manifest_sha256: &str,
) -> Result<(), String> {
    let package_root = layout.package_root.clone();
    let config_root = layout.config_root.clone();
    let data_root = layout.data_root.clone();
    let install_root = layout
        .runtime_root
        .parent()
        .ok_or("invalid Windows install root")?
        .to_path_buf();
    let expected_hash = expected_manifest_sha256.to_owned();
    tokio::task::spawn_blocking(move || {
        let output = Command::new("powershell.exe")
            .args(["-NoProfile", "-ExecutionPolicy", "Bypass", "-File"])
            .arg(package_root.join("install.ps1"))
            .arg("-PackageRoot")
            .arg(&package_root)
            .arg("-ExpectedManifestSha256")
            .arg(expected_hash)
            .arg("-ConfigRoot")
            .arg(config_root)
            .arg("-DataRoot")
            .arg(data_root)
            .arg("-InstallRoot")
            .arg(install_root)
            .output()
            .map_err(|_| "Windows service installer unavailable".to_owned())?;
        if output.status.success() {
            Ok(())
        } else {
            let reason = String::from_utf8_lossy(&output.stderr);
            Err(format!(
                "Windows service installation failed: {}",
                reason.trim()
            ))
        }
    })
    .await
    .map_err(|_| "Windows service installer task failed".to_owned())?
}

#[cfg(unix)]
async fn install_windows_services(
    _layout: &SingleServerLayout,
    _expected_manifest_sha256: &str,
) -> Result<(), String> {
    Ok(())
}

fn store_pending_token(path: &Path, token: &[u8]) -> Result<(), std::io::Error> {
    let mut options = std::fs::OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options.open(path)?;
    file.write_all(token)?;
    file.sync_all()?;
    Ok(())
}

async fn register_relay(
    layout: &SingleServerLayout,
    origin: &str,
) -> Result<Zeroizing<String>, &'static str> {
    let certificate_bytes = std::fs::read(layout.config_root.join("console-ca.crt"))
        .map_err(|_| "Console TLS root unavailable")?;
    let certificate =
        Certificate::from_pem(&certificate_bytes).map_err(|_| "Console TLS root invalid")?;
    let client = reqwest::Client::builder()
        .add_root_certificate(certificate)
        .timeout(Duration::from_secs(5))
        .build()
        .map_err(|_| "Console setup client unavailable")?;
    let console_base = "https://127.0.0.1:4600";
    let mut session_response = None;
    for _attempt in 0..30 {
        if let Ok(response) = client
            .post(format!("{console_base}/api/console/sessions"))
            .header("origin", origin)
            .header("x-pixels-client-type", "admin_web")
            .json(&json!({"username":DEFAULT_USERNAME,"password":DEFAULT_PASSWORD}))
            .send()
            .await
        {
            session_response = Some(response);
            break;
        }
        tokio::time::sleep(Duration::from_secs(2)).await;
    }
    let session_response = session_response.ok_or("Console did not become ready")?;
    if !session_response.status().is_success() {
        return Err("Console administrator login rejected");
    }
    let session: Value = session_response
        .json()
        .await
        .map_err(|_| "Console administrator session invalid")?;
    let token = session["token"]
        .as_str()
        .ok_or("Console administrator token missing")?;
    let public_host = url::Url::parse(origin)
        .ok()
        .and_then(|parsed| parsed.host_str().map(str::to_owned))
        .ok_or("Console origin invalid")?;
    let created_response = client
        .post(format!("{console_base}/api/console/managed/relays"))
        .header("origin", origin)
        .header("x-pixels-client-type", "admin_web")
        .bearer_auth(token)
        .json(&json!({"name":"Single Server Relay","public_host":public_host,"public_port":4605}))
        .send()
        .await
        .map_err(|_| "Relay registration request failed")?;
    if !created_response.status().is_success() {
        return Err("Relay registration rejected");
    }
    let created: Value = created_response
        .json()
        .await
        .map_err(|_| "Relay registration response invalid")?;
    let relay_id = created["relay"]["id"]
        .as_str()
        .ok_or("Relay identity missing")?;
    let revision = created["relay"]["revision"]
        .as_i64()
        .ok_or("Relay revision missing")?;
    let relay_token = created["relay_token"]
        .as_str()
        .ok_or("Relay token missing")?;
    let configuration_response = client
        .patch(format!(
            "{console_base}/api/console/managed/relays/{relay_id}"
        ))
        .header("origin", origin)
        .header("x-pixels-client-type", "admin_web")
        .bearer_auth(token)
        .json(&json!({
            "revision": revision,
            "configuration": {"draining": false, "disabled": false}
        }))
        .send()
        .await
        .map_err(|_| "Relay activation request failed")?;
    if !configuration_response.status().is_success() {
        return Err("Relay activation rejected");
    }
    Ok(Zeroizing::new(relay_token.to_owned()))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn automatic_setup_uses_requested_defaults() {
        let input = setup_input("192.168.31.6".to_owned(), "localhost".to_owned());
        assert_eq!(input.postgresql_port, 5432);
        assert_eq!(input.postgresql_administrator, "Pixels");
        assert_eq!(input.postgresql_password, "Pixels@123");
        assert_eq!(input.initial_username, "Pixels");
        assert_eq!(input.initial_password, "Pixels@123");
    }
}
