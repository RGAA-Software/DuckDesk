use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::net::IpAddr;

pub const DEFAULT_TURN_PORT: u16 = 20128;
pub const DEFAULT_TURN_MIN_RELAY_PORT: u16 = 20200;
pub const DEFAULT_TURN_MAX_RELAY_PORT: u16 = 20500;
pub const DEFAULT_TURN_CREDENTIAL_TTL_SECONDS: u64 = 300;

fn default_turn_port() -> u16 {
    DEFAULT_TURN_PORT
}

fn default_turn_min_relay_port() -> u16 {
    DEFAULT_TURN_MIN_RELAY_PORT
}

fn default_turn_max_relay_port() -> u16 {
    DEFAULT_TURN_MAX_RELAY_PORT
}

fn default_turn_realm() -> String {
    "pixels-console".to_string()
}

fn default_turn_credential_ttl_seconds() -> u64 {
    DEFAULT_TURN_CREDENTIAL_TTL_SECONDS
}

fn default_listen_ip() -> String {
    "0.0.0.0".to_string()
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum RtcCredentialMode {
    None,
    Static,
    ConsoleEphemeral,
}

impl Default for RtcCredentialMode {
    fn default() -> Self {
        Self::None
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct ManagedTurnServerConfig {
    pub enabled: bool,
    pub listen_ip: String,
    pub public_host: String,
    pub port: u16,
    pub relay_min_port: u16,
    pub relay_max_port: u16,
    pub realm: String,
    pub enable_udp: bool,
    pub enable_tcp: bool,
    pub credential_ttl_seconds: u64,
}

impl Default for ManagedTurnServerConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            listen_ip: default_listen_ip(),
            public_host: String::new(),
            port: default_turn_port(),
            relay_min_port: default_turn_min_relay_port(),
            relay_max_port: default_turn_max_relay_port(),
            realm: default_turn_realm(),
            enable_udp: true,
            enable_tcp: true,
            credential_ttl_seconds: default_turn_credential_ttl_seconds(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(default, deny_unknown_fields)]
pub struct AdditionalIceServerConfig {
    pub id: String,
    pub name: String,
    pub enabled: bool,
    pub urls: Vec<String>,
    pub credential_mode: RtcCredentialMode,
    pub username: String,
    pub credential: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct ConsoleRtcSettings {
    /// During the initial standard-RTC test phase this remains false, so Panel
    /// deliberately bypasses the specialized net_rtc_local path.
    pub direct_probe_enabled: bool,
    pub managed_console_server: ManagedTurnServerConfig,
    pub additional_servers: Vec<AdditionalIceServerConfig>,
}

impl Default for ConsoleRtcSettings {
    fn default() -> Self {
        Self {
            direct_probe_enabled: false,
            managed_console_server: ManagedTurnServerConfig::default(),
            additional_servers: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct RtcIceConfig {
    pub revision: u64,
    pub direct_probe_enabled: bool,
    pub managed_console_server: ManagedTurnServerConfig,
    pub additional_servers: Vec<AdditionalIceServerConfig>,
}

impl RtcIceConfig {
    pub fn from_settings(settings: ConsoleRtcSettings, console_host: &str) -> Self {
        let mut managed = settings.managed_console_server;
        if managed.public_host.trim().is_empty() {
            managed.public_host = console_host.trim().to_string();
        }
        Self {
            revision: 1,
            direct_probe_enabled: settings.direct_probe_enabled,
            managed_console_server: managed,
            additional_servers: settings.additional_servers,
        }
    }

    pub fn validate(&self) -> Result<(), String> {
        let managed = &self.managed_console_server;
        if managed.enabled {
            if managed.listen_ip.parse::<IpAddr>().is_err() {
                return Err("managed Console TURN listen_ip must be an IP address".to_string());
            }
            validate_ice_host(&managed.public_host)?;
            if managed.port == 0 {
                return Err("managed Console TURN port must be non-zero".to_string());
            }
            if managed.relay_min_port == 0
                || managed.relay_max_port == 0
                || managed.relay_min_port > managed.relay_max_port
            {
                return Err("managed Console TURN relay port range is invalid".to_string());
            }
            if managed.realm.trim().is_empty() || managed.realm.len() > 255 {
                return Err("managed Console TURN realm is invalid".to_string());
            }
            if !(60..=3600).contains(&managed.credential_ttl_seconds) {
                return Err(
                    "managed Console TURN credential TTL must be between 60 and 3600 seconds"
                        .to_string(),
                );
            }
            if !managed.enable_udp && !managed.enable_tcp {
                return Err("managed Console TURN must enable UDP or TCP".to_string());
            }
        }

        if self.additional_servers.len() > 8 {
            return Err("at most 8 additional ICE servers are allowed".to_string());
        }
        let mut ids = HashSet::new();
        let mut urls = HashSet::new();
        for server in &self.additional_servers {
            if server.id.trim().is_empty()
                || server.id.len() > 64
                || server
                    .id
                    .chars()
                    .any(|ch| !(ch.is_ascii_alphanumeric() || matches!(ch, '-' | '_' | '.')))
            {
                return Err("additional ICE server id is invalid".to_string());
            }
            if !ids.insert(server.id.to_ascii_lowercase()) {
                return Err("additional ICE server ids must be unique".to_string());
            }
            if server.urls.is_empty() || server.urls.len() > 4 {
                return Err("each additional ICE server requires 1 to 4 URLs".to_string());
            }
            let mut has_turn = false;
            for url in &server.urls {
                let normalized = normalize_ice_url(url)?;
                has_turn |= normalized.starts_with("turn:") || normalized.starts_with("turns:");
                if !urls.insert(normalized) {
                    return Err("ICE server URLs must be unique".to_string());
                }
            }
            if has_turn {
                match server.credential_mode {
                    RtcCredentialMode::None => {
                        return Err("TURN URLs require a credential mode".to_string())
                    }
                    RtcCredentialMode::Static => {
                        if server.username.is_empty() || server.credential.is_empty() {
                            return Err(
                                "static TURN credentials require username and credential"
                                    .to_string(),
                            );
                        }
                    }
                    RtcCredentialMode::ConsoleEphemeral => {
                        return Err(
                            "Console ephemeral credentials are only valid for the managed Console TURN server"
                                .to_string(),
                        )
                    }
                }
            } else if server.credential_mode == RtcCredentialMode::ConsoleEphemeral {
                return Err(
                    "Console ephemeral credentials are only valid for the managed Console TURN server"
                        .to_string(),
                );
            }
        }
        Ok(())
    }

    pub fn normalize(&mut self) -> Result<(), String> {
        self.managed_console_server.listen_ip =
            self.managed_console_server.listen_ip.trim().to_string();
        self.managed_console_server.public_host =
            normalize_host(self.managed_console_server.public_host.trim());
        self.managed_console_server.realm = self.managed_console_server.realm.trim().to_string();
        for server in &mut self.additional_servers {
            server.id = server.id.trim().to_string();
            server.name = server.name.trim().to_string();
            server.username = server.username.trim().to_string();
            server.urls = server
                .urls
                .iter()
                .map(|url| normalize_ice_url(url))
                .collect::<Result<Vec<_>, _>>()?;
        }
        self.validate()
    }

    pub fn public_view(&self) -> RtcIceConfigView {
        let mut servers = Vec::new();
        if self.managed_console_server.enabled {
            let host = host_for_ice_url(&self.managed_console_server.public_host);
            let port = self.managed_console_server.port;
            let mut urls = vec![format!("stun:{host}:{port}")];
            if self.managed_console_server.enable_udp {
                urls.push(format!("turn:{host}:{port}?transport=udp"));
            }
            if self.managed_console_server.enable_tcp {
                urls.push(format!("turn:{host}:{port}?transport=tcp"));
            }
            servers.push(RtcIceServerView {
                id: "console-managed".to_string(),
                name: "Pixels Console Coturn".to_string(),
                managed: true,
                enabled: true,
                urls,
                credential_mode: RtcCredentialMode::ConsoleEphemeral,
            });
        }
        servers.extend(
            self.additional_servers
                .iter()
                .filter(|server| server.enabled)
                .map(|server| RtcIceServerView {
                    id: server.id.clone(),
                    name: server.name.clone(),
                    managed: false,
                    enabled: true,
                    urls: server.urls.clone(),
                    credential_mode: server.credential_mode.clone(),
                }),
        );
        RtcIceConfigView {
            revision: self.revision,
            direct_probe_enabled: self.direct_probe_enabled,
            servers,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RtcIceServerView {
    pub id: String,
    pub name: String,
    pub managed: bool,
    pub enabled: bool,
    pub urls: Vec<String>,
    pub credential_mode: RtcCredentialMode,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct RtcIceConfigView {
    pub revision: u64,
    pub direct_probe_enabled: bool,
    pub servers: Vec<RtcIceServerView>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RtcSessionIceServer {
    pub id: String,
    pub urls: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub username: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub credential: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct RtcSessionIceConfig {
    pub revision: u64,
    pub direct_probe_enabled: bool,
    pub expires_at: i64,
    pub ice_servers: Vec<RtcSessionIceServer>,
}

fn validate_ice_host(host: &str) -> Result<(), String> {
    let host = host.trim();
    if host.is_empty()
        || host.len() > 253
        || host.contains("://")
        || host.contains('/')
        || host.contains('?')
        || host.chars().any(char::is_whitespace)
    {
        return Err("managed Console TURN public_host is invalid".to_string());
    }
    Ok(())
}

fn normalize_host(host: &str) -> String {
    host.trim()
        .strip_prefix('[')
        .and_then(|value| value.strip_suffix(']'))
        .unwrap_or(host.trim())
        .to_string()
}

fn host_for_ice_url(host: &str) -> String {
    if host.parse::<std::net::Ipv6Addr>().is_ok() {
        format!("[{host}]")
    } else {
        host.to_string()
    }
}

fn normalize_ice_url(url: &str) -> Result<String, String> {
    let normalized = url.trim().to_ascii_lowercase();
    let valid_scheme = ["stun:", "stuns:", "turn:", "turns:"]
        .iter()
        .any(|prefix| normalized.starts_with(prefix));
    if !valid_scheme
        || normalized.len() < 7
        || normalized.len() > 512
        || normalized.chars().any(char::is_whitespace)
    {
        return Err(format!("invalid ICE server URL: {url}"));
    }
    Ok(normalized)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_config_uses_console_host_and_managed_turn() {
        let config = RtcIceConfig::from_settings(ConsoleRtcSettings::default(), "10.0.0.9");
        config.validate().unwrap();
        let view = config.public_view();
        assert!(!view.direct_probe_enabled);
        assert_eq!(view.servers.len(), 1);
        assert!(view.servers[0]
            .urls
            .contains(&"turn:10.0.0.9:20128?transport=udp".to_string()));
    }

    #[test]
    fn accepts_multiple_unique_servers() {
        let mut config = RtcIceConfig::from_settings(ConsoleRtcSettings::default(), "127.0.0.1");
        config.additional_servers = vec![AdditionalIceServerConfig {
            id: "turn-bj".to_string(),
            name: "Beijing".to_string(),
            enabled: true,
            urls: vec!["TURN:bj.example.com:3478?transport=UDP".to_string()],
            credential_mode: RtcCredentialMode::Static,
            username: "user".to_string(),
            credential: "secret".to_string(),
        }];
        config.normalize().unwrap();
        assert_eq!(
            config.additional_servers[0].urls[0],
            "turn:bj.example.com:3478?transport=udp"
        );
    }

    #[test]
    fn rejects_duplicate_urls_and_missing_turn_credentials() {
        let mut config = RtcIceConfig::from_settings(ConsoleRtcSettings::default(), "127.0.0.1");
        config.additional_servers = vec![
            AdditionalIceServerConfig {
                id: "a".to_string(),
                enabled: true,
                urls: vec!["stun:example.com:3478".to_string()],
                ..Default::default()
            },
            AdditionalIceServerConfig {
                id: "b".to_string(),
                enabled: true,
                urls: vec!["STUN:EXAMPLE.COM:3478".to_string()],
                ..Default::default()
            },
        ];
        assert!(config.normalize().is_err());

        config.additional_servers = vec![AdditionalIceServerConfig {
            id: "turn".to_string(),
            enabled: true,
            urls: vec!["turn:example.com:3478".to_string()],
            ..Default::default()
        }];
        assert!(config.normalize().is_err());

        config.additional_servers = vec![AdditionalIceServerConfig {
            id: "external-ephemeral".to_string(),
            enabled: true,
            urls: vec!["turn:external.example.com:3478".to_string()],
            credential_mode: RtcCredentialMode::ConsoleEphemeral,
            ..Default::default()
        }];
        assert!(config.normalize().is_err());
    }
}
