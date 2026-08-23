use crate::console_settings::ConsoleLiveSettings;
use crate::rtc::model::ManagedTurnServerConfig;
use serde::Serialize;
use std::path::Path;
use std::process::Command;
use std::sync::{LazyLock, RwLock};
use std::time::Duration;
use sysinfo::{ProcessesToUpdate, Signal, System};

static TURN_OPERATION_LOCK: tokio::sync::Mutex<()> = tokio::sync::Mutex::const_new(());
static TURN_STATUS: LazyLock<RwLock<TurnSidecarStatus>> =
    LazyLock::new(|| RwLock::new(TurnSidecarStatus::default()));

#[derive(Debug, Clone, Default, Serialize)]
pub struct TurnSidecarStatus {
    pub enabled: bool,
    pub running: bool,
    pub pid: Option<u32>,
    pub listen_ip: String,
    pub public_host: String,
    pub port: u16,
    pub relay_min_port: u16,
    pub relay_max_port: u16,
    pub revision: u64,
    pub last_error: String,
}

pub fn turn_status() -> TurnSidecarStatus {
    TURN_STATUS
        .read()
        .expect("TURN sidecar status lock poisoned")
        .clone()
}

fn set_turn_status(status: TurnSidecarStatus) {
    *TURN_STATUS
        .write()
        .expect("TURN sidecar status lock poisoned") = status;
}

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

/// Applies the managed Coturn configuration and starts the adjacent
/// `px_turn.exe`. The REST secret is written only to a generated file under
/// storage; it is never placed on the command line or in a log field.
pub async fn apply_turn_config(
    settings: &ManagedTurnServerConfig,
    revision: u64,
    rest_secret_base64: &str,
    storage_dir: &Path,
    restart: bool,
) -> Result<TurnSidecarStatus, String> {
    let _operation = TURN_OPERATION_LOCK.lock().await;
    let exe_path = match std::env::current_exe() {
        Ok(path) => path,
        Err(error) => return Err(format!("cannot determine Console executable path for px_turn: {error}")),
    };
    let directory = exe_path
        .parent()
        .ok_or_else(|| "cannot determine Console executable directory for px_turn".to_string())?;
    let turn_exe = directory.join("px_turn.exe");
    if !turn_exe.is_file() {
        return Err(format!(
            "px_turn sidecar is not deployed beside px_console.exe: {}",
            turn_exe.display()
        ));
    }

    let mut status = TurnSidecarStatus {
        enabled: settings.enabled,
        running: false,
        pid: None,
        listen_ip: settings.listen_ip.clone(),
        public_host: settings.public_host.clone(),
        port: settings.port,
        relay_min_port: settings.relay_min_port,
        relay_max_port: settings.relay_max_port,
        revision,
        last_error: String::new(),
    };

    if !settings.enabled {
        stop_adjacent_turn_processes(&turn_exe).await;
        set_turn_status(status.clone());
        tracing::info!(revision, "managed px_turn sidecar is disabled");
        return Ok(status);
    }
    if rest_secret_base64.trim().is_empty() {
        return Err("TURN REST secret is empty".to_string());
    }
    std::fs::create_dir_all(storage_dir)
        .map_err(|error| format!("create TURN runtime storage failed: {error}"))?;
    let generated_config = storage_dir.join("turnserver.generated.conf");
    write_turn_runtime_config(&generated_config, settings, rest_secret_base64, storage_dir)?;

    let probe_host = if settings.listen_ip == "0.0.0.0" || settings.listen_ip == "::" {
        "127.0.0.1"
    } else {
        settings.listen_ip.as_str()
    };
    if restart {
        stop_adjacent_turn_processes(&turn_exe).await;
    } else if port_is_open(probe_host, settings.port).await {
        status.running = true;
        set_turn_status(status.clone());
        tracing::info!(port = settings.port, revision, "px_turn is already listening");
        return Ok(status);
    }

    let mut command = Command::new(&turn_exe);
    command
        .current_dir(directory)
        .arg("-c")
        .arg(&generated_config);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }
    let child = command.spawn().map_err(|error| {
        format!(
            "failed to start managed px_turn sidecar {}: {error}",
            turn_exe.display()
        )
    })?;
    status.pid = Some(child.id());
    tracing::info!(
            pid = child.id(),
            listen_ip = %settings.listen_ip,
            public_host = %settings.public_host,
            port = settings.port,
            min_relay_port = settings.relay_min_port,
            max_relay_port = settings.relay_max_port,
            revision,
            "started px_turn sidecar"
    );

    for _ in 0..20 {
        if port_is_open(probe_host, settings.port).await {
            status.running = true;
            set_turn_status(status.clone());
            tracing::info!(
                port = settings.port,
                revision,
                "px_turn sidecar is ready"
            );
            return Ok(status);
        }
        tokio::time::sleep(Duration::from_millis(250)).await;
    }
    status.last_error = format!(
        "px_turn was started but did not listen on {}:{} within 5 seconds",
        probe_host, settings.port
    );
    set_turn_status(status.clone());
    Err(status.last_error)
}

fn write_turn_runtime_config(
    path: &Path,
    settings: &ManagedTurnServerConfig,
    rest_secret_base64: &str,
    storage_dir: &Path,
) -> Result<(), String> {
    let mut lines = vec![
        format!("listening-port={}", settings.port),
        format!("min-port={}", settings.relay_min_port),
        format!("max-port={}", settings.relay_max_port),
        "use-auth-secret".to_string(),
        format!("static-auth-secret={}", rest_secret_base64.trim()),
        format!("realm={}", settings.realm),
        "stale-nonce".to_string(),
        "fingerprint".to_string(),
        "no-multicast-peers".to_string(),
        // Coturn 4.17 keeps the CLI disabled unless `cli` is explicitly set.
        // The legacy no-cli/no-dtls switches are deprecated and emit errors.
        "no-tls".to_string(),
        format!("log-file={}", storage_dir.join("px_turn.log").display()),
        "simple-log".to_string(),
    ];
    if settings.listen_ip != "0.0.0.0" && settings.listen_ip != "::" {
        lines.push(format!("listening-ip={}", settings.listen_ip));
        lines.push(format!("relay-ip={}", settings.listen_ip));
    }
    if let Ok(public_ip) = settings.public_host.parse::<std::net::IpAddr>() {
        if settings.listen_ip != "0.0.0.0"
            && settings.listen_ip != "::"
            && settings.listen_ip != settings.public_host
        {
            lines.push(format!("external-ip={public_ip}/{}", settings.listen_ip));
        } else {
            lines.push(format!("external-ip={public_ip}"));
        }
    }
    if !settings.enable_udp {
        lines.push("no-udp".to_string());
    }
    if !settings.enable_tcp {
        lines.push("no-tcp".to_string());
    }
    std::fs::write(path, lines.join("\n"))
        .map_err(|error| format!("write generated TURN configuration failed: {error}"))
}

async fn stop_adjacent_turn_processes(turn_exe: &Path) {
    let mut system = System::new_all();
    system.refresh_processes(ProcessesToUpdate::All, true);
    let own_pid = std::process::id();
    let mut killed = Vec::new();
    for (pid, process) in system.processes() {
        if pid.as_u32() == own_pid {
            continue;
        }
        if process.exe().is_some_and(|exe| same_path(exe, turn_exe)) {
            let _ = process.kill_with(Signal::Kill);
            killed.push(pid.as_u32());
        }
    }
    if killed.is_empty() {
        return;
    }
    for _ in 0..20 {
        tokio::time::sleep(Duration::from_millis(100)).await;
        let mut refreshed = System::new_all();
        refreshed.refresh_processes(ProcessesToUpdate::All, true);
        if killed.iter().all(|pid| {
            refreshed
                .process(sysinfo::Pid::from_u32(*pid))
                .is_none()
        }) {
            return;
        }
    }
}

fn same_path(left: &Path, right: &Path) -> bool {
    left.to_string_lossy()
        .eq_ignore_ascii_case(&right.to_string_lossy())
}

#[cfg(test)]
mod tests {
    use super::{local_media_port, synchronize_http_port, write_turn_runtime_config};
    use crate::rtc::model::ManagedTurnServerConfig;

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

    #[test]
    fn generated_turn_config_uses_rest_secret_without_cli_credentials() {
        let dir = std::env::temp_dir().join(format!("px_turn_sidecar_{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("turnserver.generated.conf");
        let settings = ManagedTurnServerConfig {
            listen_ip: "10.0.0.8".to_string(),
            public_host: "203.0.113.8".to_string(),
            ..Default::default()
        };
        write_turn_runtime_config(&path, &settings, "test-secret", &dir).unwrap();
        let config = std::fs::read_to_string(&path).unwrap();
        assert!(config.contains("use-auth-secret"));
        assert!(config.contains("static-auth-secret=test-secret"));
        assert!(config.contains("external-ip=203.0.113.8/10.0.0.8"));
        assert!(!config.contains("user="));
        let _ = std::fs::remove_dir_all(dir);
    }
}
