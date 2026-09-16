use serde::Deserialize;
use std::collections::HashSet;
use std::path::Path;

const CLOUD_NODE_CAPABILITIES: &[&str] = &[
    "browser_remote",
    "cloud_app_catalog",
    "cloud_app_host",
    "desktop_client",
    "desktop_host",
    "file_transfer",
    "game_hook",
    "joystick",
    "rdp_client",
    "rdp_host",
    "system_information",
    "virtual_display",
    "webview_host",
];
const REMOTE_CAPABILITIES: &[&str] = &[
    "browser_remote",
    "desktop_client",
    "desktop_host",
    "file_transfer",
    "joystick",
    "rdp_client",
    "rdp_host",
    "system_information",
    "virtual_display",
];

#[derive(Debug, Clone, Deserialize, PartialEq, Eq)]
pub struct ProductDescriptor {
    pub schema_version: u32,
    pub product: String,
    pub edition: String,
    pub company: String,
    pub product_version: String,
    pub product_version_code: u32,
    pub capabilities: Vec<String>,
}

impl ProductDescriptor {
    pub fn load_for_current_executable() -> Result<Self, String> {
        let executable = std::env::current_exe()
            .map_err(|error| format!("resolve px_service executable failed: {error}"))?;
        let directory = executable
            .parent()
            .ok_or_else(|| "px_service executable has no parent directory".to_string())?;
        Self::load(&directory.join("product-manifest.json"))
    }

    pub fn load(path: &Path) -> Result<Self, String> {
        let bytes = std::fs::read(path).map_err(|error| {
            format!(
                "read installed product descriptor {} failed: {error}",
                path.display()
            )
        })?;
        let descriptor: Self = serde_json::from_slice(&bytes).map_err(|error| {
            format!(
                "parse installed product descriptor {} failed: {error}",
                path.display()
            )
        })?;
        descriptor.validate()?;
        Ok(descriptor)
    }

    fn validate(&self) -> Result<(), String> {
        if self.schema_version != 2 || self.company != "Pixels" {
            return Err(
                "installed product descriptor must use schema 2 and company Pixels".to_string(),
            );
        }
        let expected = match (self.product.as_str(), self.edition.as_str()) {
            ("cloud_node", "CLOUD_NODE") => CLOUD_NODE_CAPABILITIES,
            ("remote", "REMOTE") => REMOTE_CAPABILITIES,
            _ => {
                return Err(
                    "px_service is only valid in Pixels Cloud Node or Pixels Remote".to_string(),
                );
            }
        };
        if self.product_version_code == 0
            || self.product_version.split('.').count() != 3
            || !self
                .product_version
                .split('.')
                .all(|part| !part.is_empty() && part.bytes().all(|byte| byte.is_ascii_digit()))
        {
            return Err("installed product descriptor has an invalid product version".to_string());
        }
        let actual: HashSet<&str> = self.capabilities.iter().map(String::as_str).collect();
        let required: HashSet<&str> = expected.iter().copied().collect();
        if actual.len() != self.capabilities.len() || actual != required {
            return Err(format!(
                "installed product descriptor capabilities do not match {}",
                self.product
            ));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn descriptor(product: &str, edition: &str, capabilities: &[&str]) -> ProductDescriptor {
        ProductDescriptor {
            schema_version: 2,
            product: product.to_string(),
            edition: edition.to_string(),
            company: "Pixels".to_string(),
            product_version: "3.3.67".to_string(),
            product_version_code: 30367,
            capabilities: capabilities.iter().map(|value| value.to_string()).collect(),
        }
    }

    #[test]
    fn accepts_only_exact_host_product_capability_sets() {
        assert!(
            descriptor("cloud_node", "CLOUD_NODE", CLOUD_NODE_CAPABILITIES)
                .validate()
                .is_ok()
        );
        assert!(descriptor("remote", "REMOTE", REMOTE_CAPABILITIES)
            .validate()
            .is_ok());
        assert!(descriptor("client", "CLIENT", REMOTE_CAPABILITIES)
            .validate()
            .is_err());
        let mut remote = REMOTE_CAPABILITIES.to_vec();
        remote.push("webview_host");
        assert!(descriptor("remote", "REMOTE", &remote).validate().is_err());
    }
}
