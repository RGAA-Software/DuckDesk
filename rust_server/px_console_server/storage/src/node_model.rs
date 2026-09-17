use crate::{StoreError, TokenDigest};
use chrono::{DateTime, Utc};
use uuid::Uuid;

#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum NodeProduct {
    CloudNode,
    Remote,
}
impl NodeProduct {
    pub(crate) fn name(self) -> &'static str {
        match self {
            Self::CloudNode => "cloud_node",
            Self::Remote => "remote",
        }
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RuntimeEpoch(pub(crate) i64);
impl RuntimeEpoch {
    pub fn value(self) -> i64 {
        self.0
    }
}
/// Server-side connection context, not a request DTO. Constructed only by node authentication.
#[derive(Debug, Clone)]
pub struct NodeConnection {
    pub(crate) id: Uuid,
    pub(crate) generation: i64,
    pub(crate) epoch: RuntimeEpoch,
    pub(crate) key: TokenDigest,
}
impl NodeConnection {
    pub fn id(&self) -> Uuid {
        self.id
    }
    pub fn generation(&self) -> i64 {
        self.generation
    }
    pub fn epoch(&self) -> RuntimeEpoch {
        self.epoch
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeConfiguration {
    pub draining: bool,
    pub disabled: bool,
    pub max_instances: u32,
}
impl NodeConfiguration {
    pub(crate) fn validate(&self) -> Result<(), StoreError> {
        if !(1..=64).contains(&self.max_instances) {
            return Err(StoreError::InvalidInput);
        }
        Ok(())
    }
}
#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct NodeReport {
    pub sequence: u64,
    pub product_version_code: u32,
    pub public_host: String,
    pub desktop_port: u16,
    pub application_port_start: u16,
    pub application_port_end: u16,
    pub game_hook: bool,
    pub webview: bool,
    pub rdp: bool,
}
impl NodeReport {
    pub(crate) fn validate(&self) -> Result<(i64, String), StoreError> {
        let sequence = i64::try_from(self.sequence).map_err(|_| StoreError::InvalidInput)?;
        if sequence < 1
            || self.product_version_code == 0
            || self.desktop_port == 0
            || self.application_port_start == 0
            || self.application_port_start > self.application_port_end
            || (self.application_port_start..=self.application_port_end)
                .contains(&self.desktop_port)
            || self.public_host.is_empty()
            || self.public_host.len() > 253
            || self.public_host.chars().any(char::is_whitespace)
            || self.public_host.chars().any(char::is_control)
            || self.public_host.contains(['/', '\\', '@', '?', '#'])
        {
            return Err(StoreError::InvalidInput);
        }
        let host = if let Ok(ip) = self.public_host.parse::<std::net::IpAddr>() {
            ip.to_string()
        } else {
            url::Host::parse(&self.public_host)
                .map_err(|_| StoreError::InvalidInput)?
                .to_string()
        };
        Ok((sequence, host))
    }
}
/// Administrative view only. Fresh means recent authenticated contact, not reconciled capacity.
#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, sqlx::FromRow)]
pub struct NodeProfile {
    pub id: Uuid,
    pub device_id: Uuid,
    pub product: String,
    pub revision: i64,
    pub generation: i64,
    pub control_epoch: Option<i64>,
    pub state: String,
    pub draining: bool,
    pub disabled: bool,
    pub max_instances: i32,
    pub report_sequence: i64,
    pub last_seen: Option<DateTime<Utc>>,
    pub product_version_code: Option<i64>,
    pub public_host: Option<String>,
    pub desktop_port: Option<i32>,
    pub application_port_start: Option<i32>,
    pub application_port_end: Option<i32>,
    pub game_hook: bool,
    pub webview: bool,
    pub rdp: bool,
    pub endpoint_revision: i64,
    pub fresh: bool,
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn node_reports_reject_overflow_uri_ports_and_unknown_endpoint_values() {
        let mut report = NodeReport {
            sequence: 1,
            product_version_code: 1,
            public_host: "node.example.test".into(),
            desktop_port: 4601,
            application_port_start: 4613,
            application_port_end: 4998,
            game_hook: true,
            webview: true,
            rdp: true,
        };
        assert!(report.validate().is_ok());
        for host in [
            "",
            "https://node.example.test",
            "node.example.test:443",
            "x@y.test",
            "a b",
            "a/b",
            "a\\b",
            "a#fragment",
        ] {
            report.public_host = host.into();
            assert!(report.validate().is_err(), "{host}");
        }
        report.public_host = "::1".into();
        assert!(report.validate().is_ok());
        report.sequence = u64::MAX;
        assert!(report.validate().is_err());
        report.sequence = 1;
        report.desktop_port = 4613;
        assert!(report.validate().is_err());
        report.desktop_port = 4601;
        report.application_port_start = 0;
        assert!(report.validate().is_err());
        assert!(NodeConfiguration {
            draining: false,
            disabled: false,
            max_instances: u32::MAX
        }
        .validate()
        .is_err());
    }
}
