//! Optional per-deployment port profile, shared by Console and Service.
//! Missing file preserves existing defaults; malformed profiles fail closed.
use serde::Deserialize;
use std::path::Path;

#[derive(Debug, Clone, Copy, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct PortRange {
    pub start: u16,
    pub end: u16,
}

#[derive(Debug, Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NetworkPorts {
    pub console: u16,
    pub desktop_render: u16,
    pub turn: u16,
    pub service: u16,
    pub discovery: u16,
    pub legacy_relay: u16,
    pub media_http: u16,
    pub media_rtmp: u16,
    pub apps: PortRange,
    pub rtc: PortRange,
    pub turn_relay: PortRange,
    pub reserved: Vec<u16>,
}

impl NetworkPorts {
    pub fn parse(text: &str) -> Result<Self, String> {
        let value: Self = serde_json::from_str(text).map_err(|e| format!("network_ports.json: {e}"))?;
        value.validate()?;
        Ok(value)
    }

    pub fn validate(&self) -> Result<(), String> {
        let mut occupied = std::collections::HashSet::new();
        for port in [self.console, self.desktop_render, self.turn, self.service, self.discovery,
                     self.legacy_relay, self.media_http, self.media_rtmp]
            .into_iter().chain(self.reserved.iter().copied())
        {
            if port == 0 || !occupied.insert(port) {
                return Err(format!("network_ports.json: invalid or duplicate port {port}"));
            }
        }
        for range in [self.apps, self.rtc, self.turn_relay] {
            if range.start == 0 || range.start > range.end {
                return Err("network_ports.json: invalid port range".into());
            }
            for port in range.start..=range.end {
                if !occupied.insert(port) {
                    return Err(format!("network_ports.json: overlapping port {port}"));
                }
            }
        }
        Ok(())
    }

    pub fn load(path: &Path) -> Result<Option<Self>, String> {
        match std::fs::read_to_string(path) {
            Ok(text) => Self::parse(&text).map(Some),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
            Err(e) => Err(format!("cannot read network port profile: {e}")),
        }
    }

    pub fn load_beside_executable() -> Result<Option<Self>, String> {
        let exe = std::env::current_exe().map_err(|e| e.to_string())?;
        let dir = exe.parent().ok_or("executable has no parent directory")?;
        Self::load(&dir.join("network_ports.json"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const PROFILE: &str = include_str!("../../../deploy/node90/network_ports.json");

    #[test]
    fn node90_profile_has_expected_roles() {
        let ports = NetworkPorts::parse(PROFILE).unwrap();
        assert_eq!(ports.console, 4600);
        assert_eq!(ports.service, 4603);
        assert_eq!(ports.apps, PortRange { start: 4613, end: 4999 });
        assert_eq!(ports.rtc.end - ports.rtc.start + 1, 300);
        assert_eq!(ports.turn_relay.end - ports.turn_relay.start + 1, 699);
        assert!(ports.reserved.contains(&5300));
    }

    #[test]
    fn ranges_are_not_restricted_to_node90() {
        let mut ports = NetworkPorts::parse(PROFILE).unwrap();
        ports.apps = PortRange { start: 32000, end: 32999 };
        ports.validate().unwrap();
        ports.apps = PortRange { start: 65535, end: 65535 };
        ports.validate().unwrap();
    }

    #[test]
    fn rejects_zero_reversed_overlapping_and_reserved_ports() {
        for range in [PortRange { start: 0, end: 1 }, PortRange { start: 20, end: 10 },
                      PortRange { start: 4990, end: 5010 }, PortRange { start: 5300, end: 5300 }] {
            let mut ports = NetworkPorts::parse(PROFILE).unwrap();
            ports.apps = range;
            assert!(ports.validate().is_err());
        }
        assert!(NetworkPorts::parse(&PROFILE.replace("4600", "65536")).is_err());
        assert!(NetworkPorts::parse(&PROFILE.replace("4600", "4601")).is_err());
        assert!(NetworkPorts::parse(&PROFILE.replace("4600", "4600.5")).is_err());
        assert!(NetworkPorts::parse("{}").is_err());
    }
}
