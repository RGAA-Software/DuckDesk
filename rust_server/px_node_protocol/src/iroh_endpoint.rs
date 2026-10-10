use serde::{Deserialize, Serialize};
use uuid::Uuid;

/// Console-owned public network settings. Local bind addresses are chosen by Render.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct IrohNetworkConfig {
    pub relays: Vec<IrohRelayConfig>,
    pub relay_only: bool,
    pub ca_certificates_pem: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct IrohRelayConfig {
    pub url: String,
    pub qad_port: Option<u16>,
}

impl IrohNetworkConfig {
    pub fn is_valid(&self) -> bool {
        if self.relays.len() > 32
            || (self.relay_only && self.relays.is_empty())
            || self.ca_certificates_pem.len() > 8
            || !serde_json::to_vec(self).is_ok_and(|encoded| encoded.len() <= 12_288)
        {
            return false;
        }
        let mut relay_urls = std::collections::HashSet::new();
        self.relays.iter().all(|relay| {
            let Ok(relay_url) = url::Url::parse(&relay.url) else {
                return false;
            };
            matches!(relay_url.scheme(), "https" | "http")
                && relay_url.host_str().is_some()
                && relay_url.username().is_empty()
                && relay_url.password().is_none()
                && relay_url.query().is_none()
                && relay_url.fragment().is_none()
                && relay.qad_port != Some(0)
                && (relay.qad_port.is_none() || relay_url.scheme() == "https")
                && relay_urls.insert(relay_url)
        }) && self.ca_certificates_pem.iter().all(|certificate| {
            certificate.contains("-----BEGIN CERTIFICATE-----")
                && certificate.contains("-----END CERTIFICATE-----")
                && !certificate.contains("PRIVATE KEY")
        })
    }
}

/// Public endpoint metadata; frontend authorization is carried separately by the existing session.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct IrohConnectionDescription {
    pub endpoint_address: serde_json::Value,
    pub endpoint_configuration: serde_json::Value,
}

impl IrohConnectionDescription {
    pub fn is_valid(&self) -> bool {
        let Some(identity) = self
            .endpoint_address
            .get("id")
            .and_then(serde_json::Value::as_str)
        else {
            return false;
        };
        identity.len() == 64
            && identity
                .bytes()
                .all(|character| character.is_ascii_hexdigit())
            && self
                .endpoint_address
                .get("addrs")
                .and_then(serde_json::Value::as_array)
                .is_some_and(|routes| !routes.is_empty() && routes.len() <= 32)
            && self.endpoint_configuration.is_object()
            && !self
                .endpoint_configuration
                .as_object()
                .is_some_and(|configuration| configuration.contains_key("bind_address"))
            && serde_json::to_vec(self).is_ok_and(|encoded| encoded.len() <= 16_384)
    }
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct RenderIrohEndpoint {
    pub port: u16,
    pub instance_id: Option<Uuid>,
    pub launch_id: Option<Uuid>,
    pub description: IrohConnectionDescription,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn network_configuration_supports_private_relay_sets_without_local_bind_or_credentials() {
        let mut configuration = IrohNetworkConfig::default();
        assert!(configuration.is_valid());
        configuration.relay_only = true;
        assert!(!configuration.is_valid());
        configuration.relays = (0..10)
            .map(|relay_index| IrohRelayConfig {
                url: format!("https://relay-{relay_index}.example.test:4605"),
                qad_port: Some(4605),
            })
            .collect();
        assert!(configuration.is_valid());
        configuration.relays[0].qad_port = Some(0);
        assert!(!configuration.is_valid());
        configuration.relays[0].qad_port = Some(4605);
        configuration.relays[0].url = "https://user:password@relay.example.test".into();
        assert!(!configuration.is_valid());
        assert!(
            serde_json::from_str::<IrohNetworkConfig>(r#"{"bind_address":"0.0.0.0:4601"}"#)
                .is_err()
        );
    }

    #[test]
    fn endpoint_description_is_public_and_has_a_reachable_identity() {
        let mut description = IrohConnectionDescription {
            endpoint_address: serde_json::json!({"id": "a".repeat(64), "addrs": [{"Ip": "192.168.31.90:4601"}]}),
            endpoint_configuration: serde_json::json!({}),
        };
        assert!(description.is_valid());
        description.endpoint_configuration["bind_address"] = "0.0.0.0:4601".into();
        assert!(!description.is_valid());
        description.endpoint_configuration = serde_json::json!({});
        description.endpoint_address["addrs"] = serde_json::json!([]);
        assert!(!description.is_valid());
    }
}
