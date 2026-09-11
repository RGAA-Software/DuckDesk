//! Node-local listeners and allocation policy. Console never supplies a global pool.
use serde::Deserialize;
use std::path::Path;

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct NodeConfig {
    #[serde(skip)]
    pub access_host: String,
    pub network: NetworkConfig,
    pub applications: PortRange,
    pub rtc: PortRange,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct NetworkConfig {
    pub listen_host: String,
    pub listen_port: u16,
    pub desktop_port: u16,
    pub panel_port: u16,
    pub discovery_port: u16,
    pub discovery_enabled: bool,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct PortRange {
    pub port_start: u16,
    pub port_end: u16,
}

impl Default for NetworkConfig {
    fn default() -> Self {
        Self {
            listen_host: "127.0.0.1".into(),
            listen_port: 4603,
            desktop_port: 4601,
            panel_port: 4999,
            discovery_port: 4604,
            discovery_enabled: false,
        }
    }
}

impl Default for NodeConfig {
    fn default() -> Self {
        Self {
            access_host: String::new(),
            network: NetworkConfig::default(),
            applications: PortRange {
                port_start: 4613,
                port_end: 4998,
            },
            rtc: PortRange {
                port_start: 5000,
                port_end: 5031,
            },
        }
    }
}

impl PortRange {
    fn contains(&self, port: u16) -> bool {
        (self.port_start..=self.port_end).contains(&port)
    }
}

impl NodeConfig {
    pub fn load(path: &Path) -> Result<Self, String> {
        match std::fs::read_to_string(path) {
            Ok(text) => {
                // Do not include the source text: future configuration may contain credentials.
                let config: Self = toml::from_str(&text)
                    .map_err(|_| format!("invalid node configuration: {}", path.display()))?;
                config.validate()?;
                Ok(config)
            }
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => Ok(Self::default()),
            Err(error) => Err(format!("cannot read node configuration: {error}")),
        }
    }

    pub fn validate(&self) -> Result<(), String> {
        validate_access_host(&self.access_host)?;
        if self
            .network
            .listen_host
            .parse::<std::net::IpAddr>()
            .is_err()
        {
            return Err("network.listen_host must be a local IP address".into());
        }
        if self.network.listen_port == 0
            || self.network.desktop_port == 0
            || self.network.panel_port == 0
        {
            return Err("management, desktop and panel ports must be between 1 and 65535".into());
        }
        if self.network.discovery_enabled && self.network.discovery_port == 0 {
            return Err("enabled discovery requires a port between 1 and 65535".into());
        }
        for (name, range) in [("applications", &self.applications), ("rtc", &self.rtc)] {
            if range.port_start == 0 || range.port_start > range.port_end {
                return Err(format!(
                    "{name} requires 1 <= port_start <= port_end <= 65535"
                ));
            }
            if range.contains(self.network.listen_port)
                || range.contains(self.network.desktop_port)
                || range.contains(self.network.panel_port)
            {
                return Err(format!("{name} overlaps a node listener"));
            }
        }
        if self.network.listen_port == self.network.desktop_port
            || self.network.listen_port == self.network.panel_port
            || self.network.desktop_port == self.network.panel_port
            || self.applications.port_start <= self.rtc.port_end
                && self.rtc.port_start <= self.applications.port_end
        {
            return Err("node port assignments overlap".into());
        }
        Ok(())
    }

    pub fn set_access_host(&mut self, access_host: String) -> Result<(), String> {
        validate_access_host(&access_host)?;
        self.access_host = access_host;
        Ok(())
    }

    /// Service owns these arguments; persisted launches cannot retain stale listener settings.
    pub fn configure_render(&self, args: &mut Vec<String>, desktop: bool) {
        let mut names = vec![
            "service_server_port",
            "panel_server_port",
            "rtc_port_start",
            "rtc_port_end",
            "rtc_advertised_ipv4",
        ];
        if desktop {
            names.push("network_listen_port");
        }
        let mut skip_value = false;
        args.retain(|arg| {
            if skip_value {
                skip_value = false;
                return false;
            }
            for name in &names {
                if arg == &format!("--{name}") {
                    skip_value = true;
                    return false;
                }
                if arg.starts_with(&format!("--{name}=")) {
                    return false;
                }
            }
            true
        });
        args.push(format!(
            "--service_server_port={}",
            self.network.listen_port
        ));
        args.push(format!("--panel_server_port={}", self.network.panel_port));
        args.push(format!("--rtc_port_start={}", self.rtc.port_start));
        args.push(format!("--rtc_port_end={}", self.rtc.port_end));
        if let Ok(advertised_ipv4) = self.access_host.parse::<std::net::Ipv4Addr>() {
            args.push(format!("--rtc_advertised_ipv4={advertised_ipv4}"));
        }
        if desktop {
            args.push(format!(
                "--network_listen_port={}",
                self.network.desktop_port
            ));
        }
    }
}

fn validate_access_host(access_host: &str) -> Result<(), String> {
    if access_host.is_empty() {
        return Ok(());
    }
    let host = url::Host::parse(access_host)
        .map_err(|_| "node access host must be an IP address or hostname")?;
    if access_host.trim() != access_host || access_host.contains(['/', '?', '#', '@']) {
        return Err("node access host must not contain a URL, port or credentials".into());
    }
    match host {
        url::Host::Ipv4(ip) if ip.is_unspecified() || ip.is_multicast() || ip.is_broadcast() => {
            Err("node access host must be a usable destination".into())
        }
        url::Host::Ipv6(ip) if ip.is_unspecified() || ip.is_multicast() => {
            Err("node access host must be a usable destination".into())
        }
        _ => Ok(()),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_and_independent_node_ranges_are_valid() {
        NodeConfig::default().validate().unwrap();
        let mut config = NodeConfig::default();
        config.network.listen_port = 4603;
        config.network.desktop_port = 4601;
        config.network.panel_port = 4999;
        config.applications = PortRange {
            port_start: 4613,
            port_end: 4998,
        };
        config.rtc = PortRange {
            port_start: 5000,
            port_end: 5031,
        };
        config.validate().unwrap();
        config.applications = PortRange {
            port_start: 40000,
            port_end: 41000,
        };
        config.validate().unwrap();
    }

    #[test]
    fn rejects_zero_reversed_overlapping_and_unknown_settings() {
        let mut config = NodeConfig::default();
        config.rtc.port_start = 0;
        assert!(config.validate().is_err());
        config.rtc.port_start = 65535;
        assert!(config.validate().is_err());
        config.rtc = config.applications.clone();
        assert!(config.validate().is_err());
        assert!(toml::from_str::<NodeConfig>("[netwrok]\nlisten_port=4603").is_err());
        assert!(toml::from_str::<NodeConfig>("[network]\nlisten_port=65536").is_err());
    }

    #[test]
    fn service_replaces_stale_launch_arguments_idempotently() {
        let mut config = NodeConfig::default();
        let mut args = vec![
            "--service_server_port".into(),
            "1".into(),
            "--rtc_port_start=2".into(),
            "--network_listen_port=12345".into(),
            "--panel_server_port=20369".into(),
            "--game_path=中文 game.exe".into(),
        ];
        config.configure_render(&mut args, false);
        assert!(args.contains(&"--network_listen_port=12345".into()));
        assert!(args.contains(&"--panel_server_port=4999".into()));
        assert!(!args.contains(&"--panel_server_port=20369".into()));
        assert!(args.contains(&"--game_path=中文 game.exe".into()));
        let first = args.clone();
        config.configure_render(&mut args, false);
        assert_eq!(args, first);
        config.configure_render(&mut args, true);
        assert!(!args.contains(&"--network_listen_port=12345".into()));
        assert!(args.contains(&"--network_listen_port=4601".into()));
        assert!(args.contains(&"--panel_server_port=4999".into()));
        assert!(!args.contains(&"--panel_server_port=20369".into()));
        config.access_host = "203.0.113.8".into();
        config.configure_render(&mut args, true);
        assert!(args.contains(&"--rtc_advertised_ipv4=203.0.113.8".into()));
        config.access_host = "render.example.com".into();
        config.configure_render(&mut args, true);
        assert!(!args
            .iter()
            .any(|arg| arg.starts_with("--rtc_advertised_ipv4=")));
    }

    #[test]
    fn package_configuration_uses_built_in_port_defaults() {
        let config: NodeConfig = toml::from_str(include_str!("../../px_service.toml")).unwrap();
        config.validate().unwrap();
        assert!(config.access_host.is_empty());
        assert_eq!(config.network.desktop_port, 4601);
        assert_eq!(config.network.panel_port, 4999);
        assert_eq!(config.applications.port_start, 4613);
        assert_eq!(config.applications.port_end, 4998);
        assert_eq!(config.rtc.port_end, 5031);
        assert!(!config.network.discovery_enabled);
    }

    #[test]
    fn access_host_rejects_credentials_and_ambiguous_destinations() {
        for value in [
            "0.0.0.0",
            "[::]",
            "239.1.1.1",
            "https://render.example.com",
            "render.example.com:4601",
            "user@host",
            " host",
        ] {
            let config = NodeConfig {
                access_host: value.into(),
                ..Default::default()
            };
            assert!(config.validate().is_err(), "invalid host accepted");
        }
        let config = NodeConfig {
            access_host: "[2001:db8::2]".into(),
            ..Default::default()
        };
        config.validate().unwrap();
    }
}
