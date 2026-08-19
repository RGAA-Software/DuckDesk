use crate::config::cms_server_config::CmsServerConfig;
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct CmsAccessInfo {
    pub cms_srv_config: CmsServerConfig,
    //pub relay_srv_config: Vec<CmsServerConfig>,
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The access-info JSON keys are a wire contract shared with the panel
    /// (`cms_access_info_parser`) and the C++ test (`test_access_decrypt.cpp`):
    /// the outer key must be `cms_srv_config` and the port key `srv_cms_port`.
    #[test]
    fn access_info_serializes_with_cms_keys() {
        let info = CmsAccessInfo {
            cms_srv_config: CmsServerConfig {
                srv_name: "Srv.01".to_string(),
                srv_w3c_ip: "127.0.0.1".to_string(),
                srv_cms_port: 30500,
                srv_udp_broadcast_port: 30501,
                srv_relay_port: 30502,
                srv_appkey: "ff785bd3031bc6cf920a782e50f43dcb".to_string(),
                srv_ssl_enable: true,
            },
        };
        let json = serde_json::to_string(&info).expect("serialize");
        assert!(
            json.contains("\"cms_srv_config\""),
            "missing cms_srv_config: {json}"
        );
        assert!(
            json.contains("\"srv_cms_port\":30500"),
            "missing srv_cms_port: {json}"
        );
        assert!(!json.contains("spvr"), "old spvr keys leaked: {json}");
    }

    #[test]
    fn access_info_roundtrip_keeps_srv_ssl_enable() {
        let info = CmsAccessInfo {
            cms_srv_config: CmsServerConfig {
                srv_name: "Srv.01".to_string(),
                srv_w3c_ip: "127.0.0.1".to_string(),
                srv_cms_port: 30500,
                srv_udp_broadcast_port: 30501,
                srv_relay_port: 30502,
                srv_appkey: "ff785bd3031bc6cf920a782e50f43dcb".to_string(),
                srv_ssl_enable: false,
            },
        };
        let json = serde_json::to_string(&info).expect("serialize");
        assert!(
            json.contains("\"srv_ssl_enable\":false"),
            "missing srv_ssl_enable: {json}"
        );
        let parsed: CmsAccessInfo = serde_json::from_str(&json).expect("deserialize");
        assert!(!parsed.cms_srv_config.srv_ssl_enable);
    }

    /// Old access info has no `srv_ssl_enable`; it must default to true (HTTPS)
    /// for backward compatibility.
    #[test]
    fn access_info_without_srv_ssl_enable_defaults_to_true() {
        let json = r#"{
            "cms_srv_config": {
                "srv_name": "Srv.01",
                "srv_w3c_ip": "127.0.0.1",
                "srv_cms_port": 30500,
                "srv_udp_broadcast_port": 30501,
                "srv_relay_port": 30502,
                "srv_appkey": "ff785bd3031bc6cf920a782e50f43dcb"
            }
        }"#;
        let parsed: CmsAccessInfo = serde_json::from_str(json).expect("deserialize");
        assert!(parsed.cms_srv_config.srv_ssl_enable);
    }
}
