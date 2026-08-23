use crate::console_settings::ConsoleLiveSettings;
use std::path::Path;
use std::process::Command;
use std::time::Duration;

const TURN_LISTENING_PORT: u16 = 20128;
const TURN_MIN_RELAY_PORT: u16 = 20200;
const TURN_MAX_RELAY_PORT: u16 = 20500;

fn local_media_port(media_server_url: &str) -> Result<Option<u16>, String> {
    let authority = media_server_url
        .trim()
        .strip_prefix("http://")
        .ok_or_else(|| {
            "[live].media_server_url must use http for the local px_media sidecar".to_string()
        })?
        .split('/')
        .next()
        .filter(|authority| !authority.is_empty())
        .ok_or_else(|| "[live].media_server_url has no host".to_string())?;
    if authority.contains('@') {
        return Err("[live].media_server_url must not contain user credentials".to_string());
    }

    let (host, port) = if let Some(rest) = authority.strip_prefix('[') {
        let Some((host, port_suffix)) = rest.split_once(']') else {
            return Err("[live].media_server_url has an invalid IPv6 host".to_string());
        };
        let port = parse_port(port_suffix.strip_prefix(':').unwrap_or(port_suffix))?;
        if !port_suffix.is_empty() && !port_suffix.starts_with(':') {
            return Err("[live].media_server_url has an invalid host/port".to_string());
        }
        (host, port)
    } else if let Some((host, port)) = authority.rsplit_once(':') {
        (host, parse_port(port)?)
    } else {
        (authority, 80)
    };
    let is_local = matches!(host, "127.0.0.1" | "localhost" | "::1" | "0.0.0.0");
    if !is_local {
        return Ok(None);
    }
    Ok(Some(port))
}

/// Whether Console owns a local px_media.exe for this configured media URL.
/// Remote ZLMediaKit deployments are deliberately not managed by the panel.
pub fn is_local_sidecar_url(media_server_url: &str) -> bool {
    local_media_port(media_server_url)
        .map(|port| port.is_some())
        .unwrap_or(false)
}

fn parse_port(port: &str) -> Result<u16, String> {
    if port.is_empty() {
        return Ok(80);
    }
    port.parse::<u16>()
        .map_err(|_| "[live].media_server_url has an invalid HTTP port".to_string())
}

fn synchronize_http_port(config_path: &Path, expected_port: u16) -> Result<(), String> {
    let config = std::fs::read_to_string(config_path)
        .map_err(|error| format!("read {} failed: {error}", config_path.display()))?;
    let newline = if config.contains("\r\n") {
        "\r\n"
    } else {
        "\n"
    };
    let mut in_http = false;
    let mut changed = false;
    let mut output = Vec::new();

    for line in config.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') && trimmed.ends_with(']') {
            in_http = trimmed.eq_ignore_ascii_case("[http]");
        }
        if in_http && !changed && trimmed.starts_with("port=") {
            output.push(format!("port={expected_port}"));
            changed = true;
        } else {
            output.push(line.to_string());
        }
    }
    if !changed {
        return Err("[http].port was not found in px_media/config.ini".to_string());
    }

    let updated = output.join(newline);
    if updated != config {
        std::fs::write(config_path, updated)
            .map_err(|error| format!("write {} failed: {error}", config_path.display()))?;
        tracing::info!(
            port = expected_port,
            "synchronized px_media HTTP port from Console configuration"
        );
    }
    Ok(())
}

async fn port_is_open(host: &str, port: u16) -> bool {
    tokio::time::timeout(
        Duration::from_millis(400),
        tokio::net::TcpStream::connect((host, port)),
    )
    .await
    .is_ok_and(|result| result.is_ok())
}

/// Starts the fixed ZLMediaKit sidecar only for the local deployment layout:
/// px_console.exe, px_media.exe and config.ini reside in one directory.
pub async fn ensure_started(settings: &ConsoleLiveSettings) {
    if !settings.auto_start_media_server {
        tracing::info!("px_media sidecar auto-start is disabled by [live]");
        return;
    }

    let port = match local_media_port(&settings.media_server_url) {
        Ok(Some(port)) => port,
        Ok(None) => {
            tracing::info!(url = %settings.media_server_url, "media server is remote; skip local px_media startup");
            return;
        }
        Err(error) => {
            tracing::error!("px_media sidecar startup skipped: {error}");
            return;
        }
    };

    let exe_path = match std::env::current_exe() {
        Ok(path) => path,
        Err(error) => {
            tracing::error!("cannot determine Console executable path for px_media: {error}");
            return;
        }
    };
    let Some(directory) = exe_path.parent() else {
        tracing::error!("cannot determine Console executable directory for px_media");
        return;
    };
    let media_exe = directory.join("px_media.exe");
    let media_config = directory.join("config.ini");
    if !media_exe.is_file() || !media_config.is_file() {
        tracing::error!(
            exe = %media_exe.display(),
            config = %media_config.display(),
            "px_media sidecar is not deployed beside px_console.exe"
        );
        return;
    }
    if let Err(error) = synchronize_http_port(&media_config, port) {
        tracing::error!("px_media sidecar startup skipped: {error}");
        return;
    }
    if port_is_open("127.0.0.1", port).await {
        tracing::info!(port, "px_media is already listening");
        return;
    }

    let mut command = Command::new(&media_exe);
    command.current_dir(directory);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }
    match command.spawn() {
        Ok(child) => tracing::info!(pid = child.id(), port, "started px_media sidecar"),
        Err(error) => {
            tracing::error!(exe = %media_exe.display(), "failed to start px_media: {error}");
            return;
        }
    }

    for _ in 0..20 {
        if port_is_open("127.0.0.1", port).await {
            tracing::info!(port, "px_media sidecar is ready");
            return;
        }
        tokio::time::sleep(Duration::from_millis(250)).await;
    }
    tracing::warn!(
        port,
        "px_media was started but is not listening yet; inspect its log/config.ini"
    );
}

/// Starts the bundled Coturn sidecar from the adjacent turnserver.conf. The
/// server address is still supplied by Console, because it is selected from `server_w3c_ip`, which is
/// automatically resolved to a local IPv4 address when it is left empty.
///
/// Long-term credentials are deliberately enabled even before Console-issued
/// TURN REST credentials are wired in, so this sidecar can never become an
/// anonymous relay merely because it is bundled with Console.
pub async fn ensure_turn_started(server_ip: &str) {
    let server_ip = server_ip.trim();
    if server_ip.parse::<std::net::IpAddr>().is_err() {
        tracing::error!(
            server_ip,
            "px_turn startup skipped: Console address is not an IP address"
        );
        return;
    }

    let exe_path = match std::env::current_exe() {
        Ok(path) => path,
        Err(error) => {
            tracing::error!("cannot determine Console executable path for px_turn: {error}");
            return;
        }
    };
    let Some(directory) = exe_path.parent() else {
        tracing::error!("cannot determine Console executable directory for px_turn");
        return;
    };
    let turn_exe = directory.join("px_turn.exe");
    let turn_config = directory.join("turnserver.conf");
    if !turn_exe.is_file() || !turn_config.is_file() {
        tracing::error!(
            exe = %turn_exe.display(),
            config = %turn_config.display(),
            "px_turn sidecar or its configuration is not deployed beside px_console.exe"
        );
        return;
    }
    if port_is_open(server_ip, TURN_LISTENING_PORT).await {
        tracing::info!(
            server_ip,
            port = TURN_LISTENING_PORT,
            "px_turn is already listening"
        );
        return;
    }

    let mut command = Command::new(&turn_exe);
    command.current_dir(directory).args([
        "-c",
        "./turnserver.conf",
        "-L",
        server_ip,
        "-E",
        server_ip,
    ]);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }
    match command.spawn() {
        Ok(child) => tracing::info!(
            pid = child.id(),
            server_ip,
            port = TURN_LISTENING_PORT,
            min_relay_port = TURN_MIN_RELAY_PORT,
            max_relay_port = TURN_MAX_RELAY_PORT,
            "started px_turn sidecar"
        ),
        Err(error) => {
            tracing::error!(exe = %turn_exe.display(), "failed to start px_turn: {error}");
            return;
        }
    }

    for _ in 0..20 {
        if port_is_open(server_ip, TURN_LISTENING_PORT).await {
            tracing::info!(
                server_ip,
                port = TURN_LISTENING_PORT,
                "px_turn sidecar is ready"
            );
            return;
        }
        tokio::time::sleep(Duration::from_millis(250)).await;
    }
    tracing::warn!(
        server_ip,
        port = TURN_LISTENING_PORT,
        "px_turn was started but is not listening yet; inspect px_turn.log"
    );
}

#[cfg(test)]
mod tests {
    use super::{local_media_port, synchronize_http_port};

    #[test]
    fn resolves_only_local_media_urls() {
        assert_eq!(
            local_media_port("http://127.0.0.1:18080").unwrap(),
            Some(18080)
        );
        assert_eq!(
            local_media_port("http://media.internal:8080").unwrap(),
            None
        );
    }

    #[test]
    fn replaces_only_first_http_port() {
        let dir = std::env::temp_dir().join(format!("px_media_sidecar_{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let config = dir.join("config.ini");
        std::fs::write(&config, "[http]\nport=8080\n[rtmp]\nport=1935\n").unwrap();
        synchronize_http_port(&config, 18080).unwrap();
        assert_eq!(
            std::fs::read_to_string(&config).unwrap(),
            "[http]\nport=18080\n[rtmp]\nport=1935"
        );
        let _ = std::fs::remove_file(config);
        let _ = std::fs::remove_dir(dir);
    }
}
