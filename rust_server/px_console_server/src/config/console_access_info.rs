use crate::config::console_server_config::{ConsoleServerConfig, LegacyCmsServerConfig};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct ConsoleAccessInfo {
    pub console_srv_config: ConsoleServerConfig,
    /// Serialized alongside the canonical key for one-release compatibility
    /// with already deployed Panels.
    #[serde(rename = "cms_srv_config", default, skip_serializing_if = "Option::is_none")]
    pub legacy_cms_srv_config: Option<LegacyCmsServerConfig>,
    //pub relay_srv_config: Vec<ConsoleServerConfig>,
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The access-info JSON keys are a wire contract shared with the panel
    /// (`console_access_info_parser`) and the C++ test (`test_access_decrypt.cpp`):
    /// the outer key must be `console_srv_config` and the port key `srv_console_port`.
    #[test]
    fn access_info_serializes_with_console_keys() {
        let info = ConsoleAccessInfo {
            console_srv_config: ConsoleServerConfig {
                srv_name: "Srv.01".to_string(),
                srv_w3c_ip: "127.0.0.1".to_string(),
                srv_console_port: 30500,
                srv_udp_broadcast_port: 30501,
                srv_relay_port: 30502,
                srv_appkey: "ff785bd3031bc6cf920a782e50f43dcb".to_string(),
                srv_ssl_enable: true,
            },
            legacy_cms_srv_config: None,
        };
        let json = serde_json::to_string(&info).expect("serialize");
        assert!(
            json.contains("\"console_srv_config\""),
            "missing console_srv_config: {json}"
        );
        assert!(
            json.contains("\"srv_console_port\":30500"),
            "missing srv_console_port: {json}"
        );
        assert!(!json.contains("spvr"), "old spvr keys leaked: {json}");
    }

    #[test]
    fn access_info_roundtrip_keeps_srv_ssl_enable() {
        let info = ConsoleAccessInfo {
            console_srv_config: ConsoleServerConfig {
                srv_name: "Srv.01".to_string(),
                srv_w3c_ip: "127.0.0.1".to_string(),
                srv_console_port: 30500,
                srv_udp_broadcast_port: 30501,
                srv_relay_port: 30502,
                srv_appkey: "ff785bd3031bc6cf920a782e50f43dcb".to_string(),
                srv_ssl_enable: false,
            },
            legacy_cms_srv_config: None,
        };
        let json = serde_json::to_string(&info).expect("serialize");
        assert!(
            json.contains("\"srv_ssl_enable\":false"),
            "missing srv_ssl_enable: {json}"
        );
        let parsed: ConsoleAccessInfo = serde_json::from_str(&json).expect("deserialize");
        assert!(!parsed.console_srv_config.srv_ssl_enable);
    }

    /// Old access info has no `srv_ssl_enable`; it must default to true (HTTPS)
    /// for backward compatibility.
    #[test]
    fn access_info_without_srv_ssl_enable_defaults_to_true() {
        let json = r#"{
            "console_srv_config": {
                "srv_name": "Srv.01",
                "srv_w3c_ip": "127.0.0.1",
                "srv_console_port": 30500,
                "srv_udp_broadcast_port": 30501,
                "srv_relay_port": 30502,
                "srv_appkey": "ff785bd3031bc6cf920a782e50f43dcb"
            }
        }"#;
        let parsed: ConsoleAccessInfo = serde_json::from_str(json).expect("deserialize");
        assert!(parsed.console_srv_config.srv_ssl_enable);
    }

    #[test]
    fn upgrade_payload_serializes_canonical_and_legacy_keys() {
        let canonical = ConsoleServerConfig {
            srv_name: "Srv.01".to_string(),
            srv_w3c_ip: "127.0.0.1".to_string(),
            srv_console_port: 30500,
            srv_udp_broadcast_port: 30501,
            srv_relay_port: 30502,
            srv_appkey: "appkey".to_string(),
            srv_ssl_enable: true,
        };
        let info = ConsoleAccessInfo {
            legacy_cms_srv_config: Some(LegacyCmsServerConfig::from(&canonical)),
            console_srv_config: canonical,
        };
        let value = serde_json::to_value(info).expect("serialize");
        assert_eq!(value["console_srv_config"]["srv_console_port"], 30500);
        assert_eq!(value["cms_srv_config"]["srv_cms_port"], 30500);
    }
}
