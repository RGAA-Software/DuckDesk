use std::{env, net::SocketAddr, sync::Arc, time::Duration};
use zeroize::Zeroizing;

#[derive(Clone)]
pub struct ControlPlaneConfig {
    pub url: String,
    pub token: Arc<Zeroizing<String>>,
    pub product_version_code: u32,
}

#[derive(Clone)]
pub struct RelayConfig {
    pub listen: SocketAddr,
    pub app_key: Vec<u8>,
    pub control_key: Vec<u8>,
    pub max_connections: usize,
    pub max_rooms: usize,
    pub outbound_queue: usize,
    pub max_message_bytes: usize,
    pub connection_idle_timeout: Duration,
    pub control_plane: Option<ControlPlaneConfig>,
}

impl RelayConfig {
    pub fn from_environment() -> Result<Self, String> {
        let listen = required("PIXELS_RELAY_LISTEN")?
            .parse::<SocketAddr>()
            .map_err(|_| "PIXELS_RELAY_LISTEN must be a socket address".to_string())?;
        let app_key = required("PIXELS_RELAY_APP_KEY")?.into_bytes();
        if app_key.len() < 16 || app_key.len() > 512 {
            return Err("PIXELS_RELAY_APP_KEY must contain 16-512 bytes".to_string());
        }
        let control_key = required("PIXELS_RELAY_CONTROL_KEY")?.into_bytes();
        if control_key.len() < 32 || control_key.len() > 512 {
            return Err("PIXELS_RELAY_CONTROL_KEY must contain 32-512 bytes".to_string());
        }
        let control_url = required("PIXELS_RELAY_CONSOLE_CONTROL_URL")?;
        let parsed_control_url = url::Url::parse(&control_url).map_err(|_| {
            "PIXELS_RELAY_CONSOLE_CONTROL_URL must be an absolute ws/wss URL".to_string()
        })?;
        if !matches!(parsed_control_url.scheme(), "ws" | "wss")
            || parsed_control_url.host_str().is_none()
            || !parsed_control_url.username().is_empty()
            || parsed_control_url.password().is_some()
            || parsed_control_url.query().is_some()
            || parsed_control_url.fragment().is_some()
        {
            return Err(
                "PIXELS_RELAY_CONSOLE_CONTROL_URL must be an absolute ws/wss URL without credentials, query or fragment".to_string(),
            );
        }
        let relay_token = Zeroizing::new(required("PIXELS_RELAY_NODE_TOKEN")?);
        if relay_token.len() != 64
            || !relay_token.bytes().all(|byte_value| {
                byte_value.is_ascii_digit() || (b'a'..=b'f').contains(&byte_value)
            })
        {
            return Err(
                "PIXELS_RELAY_NODE_TOKEN must contain exactly 64 lowercase hexadecimal characters"
                    .to_string(),
            );
        }
        Ok(Self {
            listen,
            app_key,
            control_key,
            max_connections: bounded("PIXELS_RELAY_MAX_CONNECTIONS", 4_096, 2, 100_000)?,
            max_rooms: bounded("PIXELS_RELAY_MAX_ROOMS", 2_048, 1, 50_000)?,
            outbound_queue: bounded("PIXELS_RELAY_OUTBOUND_QUEUE", 256, 8, 8_192)?,
            max_message_bytes: bounded(
                "PIXELS_RELAY_MAX_MESSAGE_BYTES",
                8 * 1024 * 1024,
                1_024,
                64 * 1024 * 1024,
            )?,
            connection_idle_timeout: Duration::from_secs(bounded(
                "PIXELS_RELAY_CONNECTION_IDLE_SECONDS",
                10,
                3,
                300,
            )? as u64),
            control_plane: Some(ControlPlaneConfig {
                url: parsed_control_url.to_string(),
                token: Arc::new(relay_token),
                product_version_code: package_version_code()?,
            }),
        })
    }
}

fn package_version_code() -> Result<u32, String> {
    let mut components = env!("CARGO_PKG_VERSION").split('.');
    let major = components
        .next()
        .and_then(|value| value.parse::<u32>().ok());
    let minor = components
        .next()
        .and_then(|value| value.parse::<u32>().ok());
    let patch = components
        .next()
        .and_then(|value| value.parse::<u32>().ok());
    match (major, minor, patch, components.next()) {
        (Some(major), Some(minor), Some(patch), None)
            if major <= 999 && minor <= 999 && patch <= 999 =>
        {
            Ok(major * 1_000_000 + minor * 1_000 + patch)
        }
        _ => Err("Relay package version cannot be represented as a version code".to_string()),
    }
}

fn required(name: &str) -> Result<String, String> {
    env::var(name)
        .ok()
        .filter(|value| !value.trim().is_empty())
        .ok_or_else(|| format!("{name} is required"))
}

fn bounded(name: &str, default: usize, minimum: usize, maximum: usize) -> Result<usize, String> {
    let value = match env::var(name) {
        Ok(raw) => raw
            .parse::<usize>()
            .map_err(|_| format!("{name} must be an integer"))?,
        Err(_) => default,
    };
    if !(minimum..=maximum).contains(&value) {
        return Err(format!("{name} must be between {minimum} and {maximum}"));
    }
    Ok(value)
}
