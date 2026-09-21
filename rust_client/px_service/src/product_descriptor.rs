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
    pub distribution: String,
    pub release_namespace: Option<String>,
    pub oem_id: Option<String>,
    pub oem_profile_sha256: Option<String>,
    pub edition: String,
    pub company: String,
    pub product_version: String,
    pub product_version_code: u32,
    pub signer_certificate_sha256: Option<String>,
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
        if self.schema_version != 3 {
            return Err("installed product descriptor must use schema 3".to_string());
        }
        if !matches!(
            self.distribution.as_str(),
            "development" | "official" | "customer" | "oem"
        ) {
            return Err("installed product descriptor has an invalid distribution".to_string());
        }
        let release_identity_valid = if self.distribution == "development" {
            self.release_namespace.is_none()
                && self.oem_id.is_none()
                && self.oem_profile_sha256.is_none()
                && self.company == "Pixels"
        } else {
            valid_release_identity(
                &self.distribution,
                self.release_namespace.as_deref(),
                self.oem_id.as_deref(),
                &self.company,
                self.oem_profile_sha256.as_deref(),
            )
        };
        if !release_identity_valid {
            return Err("installed product descriptor has an invalid release identity".to_string());
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
        let signer_is_valid = self
            .signer_certificate_sha256
            .as_deref()
            .is_some_and(valid_sha256);
        if (self.distribution == "development" && self.signer_certificate_sha256.is_some())
            || (self.distribution != "development" && !signer_is_valid)
        {
            return Err(
                "installed product descriptor has an invalid signer certificate pin".to_string(),
            );
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

fn valid_sha256(value: &str) -> bool {
    value.len() == 64
        && value.bytes().all(|byte| byte.is_ascii_hexdigit())
        && value.bytes().any(|byte| byte != b'0')
}

pub(crate) fn valid_release_identity(
    distribution: &str,
    release_namespace: Option<&str>,
    oem_id: Option<&str>,
    company: &str,
    oem_profile_sha256: Option<&str>,
) -> bool {
    match distribution {
        "official" => {
            release_namespace == Some("pixels.official")
                && oem_id.is_none()
                && oem_profile_sha256.is_none()
                && company == "Pixels"
        }
        "customer" => {
            release_namespace == Some("pixels.customer")
                && oem_id.is_none()
                && oem_profile_sha256.is_none()
                && company == "Pixels"
        }
        "oem" => {
            let valid_company = company == company.trim()
                && !company.is_empty()
                && company.len() <= 128
                && !company.eq_ignore_ascii_case("Pixels")
                && company.chars().all(|character| !character.is_control());
            oem_id.is_some_and(|oem_id| {
                (3..=32).contains(&oem_id.len())
                    && oem_id.bytes().all(|byte| {
                        byte.is_ascii_lowercase() || byte.is_ascii_digit() || byte == b'-'
                    })
                    && !oem_id.starts_with('-')
                    && !oem_id.ends_with('-')
                    && !oem_id.contains("--")
                    && !matches!(oem_id, "pixels" | "official" | "customer" | "oem")
                    && release_namespace.is_some_and(|release_namespace| {
                        release_namespace == format!("oem.{oem_id}")
                    })
                    && valid_company
                    && oem_profile_sha256.is_some_and(valid_sha256)
            })
        }
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn descriptor(product: &str, edition: &str, capabilities: &[&str]) -> ProductDescriptor {
        ProductDescriptor {
            schema_version: 3,
            product: product.to_string(),
            distribution: "official".to_string(),
            release_namespace: Some("pixels.official".into()),
            oem_id: None,
            oem_profile_sha256: None,
            edition: edition.to_string(),
            company: "Pixels".to_string(),
            product_version: "3.3.67".to_string(),
            product_version_code: 30367,
            signer_certificate_sha256: Some("b".repeat(64)),
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
        let mut invalid_distribution = descriptor("remote", "REMOTE", REMOTE_CAPABILITIES);
        invalid_distribution.distribution = "official-looking".to_string();
        assert!(invalid_distribution.validate().is_err());
        let mut retired_schema = descriptor("remote", "REMOTE", REMOTE_CAPABILITIES);
        retired_schema.schema_version = 2;
        assert!(retired_schema.validate().is_err());
        let mut mismatched_domain = descriptor("remote", "REMOTE", REMOTE_CAPABILITIES);
        mismatched_domain.release_namespace = Some("pixels.customer".into());
        assert!(mismatched_domain.validate().is_err());
        let mut oem = descriptor("remote", "REMOTE", REMOTE_CAPABILITIES);
        oem.distribution = "oem".into();
        oem.release_namespace = Some("oem.acme-cloud".into());
        oem.oem_id = Some("acme-cloud".into());
        oem.oem_profile_sha256 = Some("e".repeat(64));
        oem.company = "Acme Systems".into();
        assert!(oem.validate().is_ok());
        oem.company = "Pixels".into();
        assert!(oem.validate().is_err());
        oem.company = "Acme Systems".into();
        oem.oem_profile_sha256 = Some("f".repeat(63));
        assert!(oem.validate().is_err());
        let mut missing_release_signer = descriptor("remote", "REMOTE", REMOTE_CAPABILITIES);
        missing_release_signer.signer_certificate_sha256 = None;
        assert!(missing_release_signer.validate().is_err());
        let mut development = descriptor("remote", "REMOTE", REMOTE_CAPABILITIES);
        development.distribution = "development".into();
        development.release_namespace = None;
        development.signer_certificate_sha256 = None;
        assert!(development.validate().is_ok());
    }
}
