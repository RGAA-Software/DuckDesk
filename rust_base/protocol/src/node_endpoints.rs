use crate::console_service::NodeEndpoints;

impl NodeEndpoints {
    /// Unconfigured addresses are explicit and must not fall back to a stale Panel link.
    pub fn validate(&self) -> Result<(), &'static str> {
        if self.schema_version != 1 {
            return Err("unsupported node endpoint schema");
        }
        if !self.access_host.is_empty() {
            let host = url::Host::parse(&self.access_host).map_err(|_| "invalid node host")?;
            if self.access_host.len() > 253 || self.access_host.trim() != self.access_host
                || self.access_host.contains(['/', '?', '#', '@', '\\']) {
                return Err("node host must not contain URL components");
            }
            match host {
                url::Host::Ipv4(ip) if ip.is_unspecified() || ip.is_multicast() || ip.is_broadcast() =>
                    return Err("invalid node destination"),
                url::Host::Ipv6(ip) if ip.is_unspecified() || ip.is_multicast() =>
                    return Err("invalid node destination"),
                _ => {}
            }
        }
        let valid_port = |port| (1..=65535).contains(&port);
        if !valid_port(self.desktop_port) || !valid_port(self.application_port_start)
            || !valid_port(self.application_port_end) || !valid_port(self.rtc_port_start)
            || !valid_port(self.rtc_port_end) || self.application_port_start > self.application_port_end
            || self.rtc_port_start > self.rtc_port_end {
            return Err("invalid node port range");
        }
        let applications = self.application_port_start..=self.application_port_end;
        let rtc = self.rtc_port_start..=self.rtc_port_end;
        if applications.contains(&self.desktop_port) || rtc.contains(&self.desktop_port)
            || (self.application_port_start <= self.rtc_port_end && self.rtc_port_start <= self.application_port_end) {
            return Err("overlapping node ports");
        }
        Ok(())
    }

    pub fn desktop_endpoint(&self) -> Option<(String, i32)> {
        self.validate().ok()?;
        if self.access_host.is_empty() { return None; }
        let host = url::Host::parse(&self.access_host).ok()?;
        let hostname = match host {
            url::Host::Ipv6(ip) => ip.to_string(),
            host => host.to_string(),
        };
        Some((hostname, self.desktop_port as i32))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use prost::Message;

    fn sample() -> NodeEndpoints {
        NodeEndpoints { schema_version: 1, access_host: "render.example.com".into(), desktop_port: 4601,
            application_port_start: 4613, application_port_end: 4999, rtc_port_start: 5000, rtc_port_end: 5299 }
    }

    #[test]
    fn report_round_trip_and_destinations() {
        for host in ["render.example.com", "10.0.0.90", "[2001:db8::90]", ""] {
            let report = NodeEndpoints { access_host: host.into(), ..sample() };
            assert!(report.validate().is_ok());
            assert_eq!(NodeEndpoints::decode(report.encode_to_vec().as_slice()).unwrap(), report);
            assert_eq!(report.desktop_endpoint().is_some(), !host.is_empty());
        }
    }

    #[test]
    fn reject_ambiguous_hosts_versions_and_ports() {
        for host in ["https://example.com", "user@example.com", "example.com:4601", "0.0.0.0", "[::]", "224.0.0.1"] {
            assert!(NodeEndpoints { access_host: host.into(), ..sample() }.validate().is_err());
        }
        for report in [NodeEndpoints { schema_version: 2, ..sample() }, NodeEndpoints { desktop_port: 0, ..sample() },
            NodeEndpoints { rtc_port_start: 4900, ..sample() }, NodeEndpoints { application_port_end: 70000, ..sample() }] {
            assert!(report.validate().is_err());
        }
    }
}
