use std::{env, net::SocketAddr};

#[derive(Clone)]
pub struct RelayConfig {
    pub listen: SocketAddr,
    pub app_key: Vec<u8>,
    pub max_connections: usize,
    pub max_rooms: usize,
    pub outbound_queue: usize,
    pub max_message_bytes: usize,
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
        Ok(Self {
            listen,
            app_key,
            max_connections: bounded("PIXELS_RELAY_MAX_CONNECTIONS", 4_096, 2, 100_000)?,
            max_rooms: bounded("PIXELS_RELAY_MAX_ROOMS", 2_048, 1, 50_000)?,
            outbound_queue: bounded("PIXELS_RELAY_OUTBOUND_QUEUE", 256, 8, 8_192)?,
            max_message_bytes: bounded(
                "PIXELS_RELAY_MAX_MESSAGE_BYTES",
                8 * 1024 * 1024,
                1_024,
                64 * 1024 * 1024,
            )?,
        })
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
