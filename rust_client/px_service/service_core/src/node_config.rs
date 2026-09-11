//! Node-local listeners and allocation policy. Console never supplies a global pool.
use serde::Deserialize;
use std::path::Path;

#[derive(Clone, Debug, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct NodeConfig {
    pub console_url: String,
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
            discovery_port: 4604,
            discovery_enabled: false,
        }
    }
}

impl Default for NodeConfig {
    fn default() -> Self {
        Self {
            console_url: String::new(),
            access_host: String::new(),
            network: NetworkConfig::default(),
            applications: PortRange {
                port_start: 4613,
                port_end: 4999,
            },
            rtc: PortRange {
                port_start: 5000,
                port_end: 5299,
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
        self.console_endpoint()?;
        if !self.access_host.is_empty() {
            let host = url::Host::parse(&self.access_host).map_err(|_| "access_host must be an IP address or hostname")?;
            if self.access_host.trim() != self.access_host || self.access_host.contains(['/', '?', '#', '@']) {
                return Err("access_host must not contain a URL, port or credentials".into());
            }
            match host {
                url::Host::Ipv4(ip) if ip.is_unspecified() || ip.is_multicast() || ip.is_broadcast() =>
                    return Err("access_host must be a usable destination".into()),
                url::Host::Ipv6(ip) if ip.is_unspecified() || ip.is_multicast() =>
                    return Err("access_host must be a usable destination".into()),
                _ => {}
            }
        }
        if self
            .network
            .listen_host
            .parse::<std::net::IpAddr>()
            .is_err()
        {
            return Err("network.listen_host must be a local IP address".into());
        }
        if self.network.listen_port == 0 || self.network.desktop_port == 0 {
            return Err("management and desktop ports must be between 1 and 65535".into());
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
            if range.contains(self.network.listen_port) || range.contains(self.network.desktop_port)
            {
                return Err(format!("{name} overlaps a node listener"));
            }
        }
        if self.network.listen_port == self.network.desktop_port
            || self.applications.port_start <= self.rtc.port_end
                && self.rtc.port_start <= self.applications.port_end
        {
            return Err("node port assignments overlap".into());
        }
        Ok(())
    }

    /// Address configuration never carries credentials; registration remains a separate workflow.
    pub fn console_endpoint(&self) -> Result<Option<(String, u16)>, String> {
        if self.console_url.is_empty() { return Ok(None); }
        let endpoint = url::Url::parse(&self.console_url).map_err(|_| "console_url must be an HTTPS URL")?;
        if endpoint.scheme() != "https" || endpoint.host_str().is_none() || !endpoint.username().is_empty()
            || endpoint.password().is_some() || endpoint.query().is_some() || endpoint.fragment().is_some()
            || endpoint.path() != "/" || self.console_url.trim() != self.console_url {
            return Err("console_url requires an HTTPS origin without credentials, path, query or fragment".into());
        }
        let port = endpoint.port_or_known_default().ok_or("console_url has no valid port")?;
        if port == 0 { return Err("console_url port must be between 1 and 65535".into()); }
        let host = match endpoint.host().ok_or("console_url has no host")? {
            url::Host::Domain(value) => value.to_owned(),
            url::Host::Ipv4(value) => value.to_string(),
            url::Host::Ipv6(value) => value.to_string(),
        };
        Ok(Some((host, port)))
    }

    /// Service owns these arguments; persisted launches cannot retain stale listener settings.
    pub fn configure_render(&self, args: &mut Vec<String>, desktop: bool) {
        let mut names = vec!["service_server_port", "rtc_port_start", "rtc_port_end", "rtc_advertised_ipv4"];
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_and_independent_node_ranges_are_valid() {
        NodeConfig::default().validate().unwrap();
        let mut config = NodeConfig::default();
        config.network.listen_port = 4603;
        config.network.desktop_port = 4601;
        config.applications = PortRange {
            port_start: 4613,
            port_end: 4999,
        };
        config.rtc = PortRange {
            port_start: 5000,
            port_end: 5299,
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
            "--game_path=中文 game.exe".into(),
        ];
        config.configure_render(&mut args, false);
        assert!(args.contains(&"--network_listen_port=12345".into()));
        assert!(args.contains(&"--game_path=中文 game.exe".into()));
        let first = args.clone();
        config.configure_render(&mut args, false);
        assert_eq!(args, first);
        config.configure_render(&mut args, true);
        assert!(!args.contains(&"--network_listen_port=12345".into()));
        assert!(args.contains(&"--network_listen_port=4601".into()));
        config.access_host = "203.0.113.8".into();
        config.configure_render(&mut args, true);
        assert!(args.contains(&"--rtc_advertised_ipv4=203.0.113.8".into()));
        config.access_host = "render.example.com".into();
        config.configure_render(&mut args, true);
        assert!(!args.iter().any(|arg| arg.starts_with("--rtc_advertised_ipv4=")));
    }

    #[test]
    fn minimal_configuration_requires_only_two_addresses() {
        let config: NodeConfig = toml::from_str("console_url='https://console.example.com:4600'\naccess_host='render.example.com'").unwrap();
        config.validate().unwrap();
        assert_eq!(config.console_endpoint().unwrap(), Some(("console.example.com".into(), 4600)));
        assert_eq!(config.network.desktop_port, 4601);
        assert_eq!(config.applications.port_start, 4613);
        assert_eq!(config.rtc.port_end, 5299);
        assert!(!config.network.discovery_enabled);
    }

    #[test]
    fn addresses_reject_credentials_and_ambiguous_destinations() {
        for value in ["http://console.example.com", "https://user:secret@example.com", "https://example.com/path", "https://example.com?key=secret", "https://example.com:0"] {
            let config = NodeConfig { console_url: value.into(), ..Default::default() };
            assert!(config.validate().is_err(), "invalid URL accepted");
        }
        for value in ["0.0.0.0", "[::]", "239.1.1.1", "https://render.example.com", "render.example.com:4601", "user@host", " host"] {
            let config = NodeConfig { access_host: value.into(), ..Default::default() };
            assert!(config.validate().is_err(), "invalid host accepted");
        }
        let config = NodeConfig { console_url: "https://[2001:db8::1]:4600".into(), access_host: "[2001:db8::2]".into(), ..Default::default() };
        config.validate().unwrap();
        assert_eq!(config.console_endpoint().unwrap(), Some(("2001:db8::1".into(), 4600)));
    }
}
