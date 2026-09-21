//! Shared release *metadata*, not a signature verifier or permission to install.
//! Desk publishes; each Console independently approves. No cross-database access.
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

#[derive(Debug, thiserror::Error)]
#[error("invalid release metadata")]
pub struct InvalidRelease;

macro_rules! dimension {
    ($name:ident { $($variant:ident => $wire:literal),+ $(,)? }) => {
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
        pub enum $name { $(#[serde(rename = $wire)] $variant),+ }
        impl $name {
            pub fn name(self) -> &'static str { match self { $(Self::$variant => $wire),+ } }
        }
        impl std::str::FromStr for $name {
            type Err = InvalidRelease;
            fn from_str(value: &str) -> Result<Self, Self::Err> {
                match value { $($wire => Ok(Self::$variant),)+ _ => Err(InvalidRelease) }
            }
        }
    };
}
dimension!(Product { CloudNode => "cloud_node", Client => "client", Remote => "remote", Android => "android", Server => "server" });
dimension!(Distribution { Official => "official", Customer => "customer" });
dimension!(Channel { Stable => "stable", Preview => "preview" });
dimension!(OperatingSystem { Windows => "windows", Linux => "linux", Android => "android" });
dimension!(Architecture { X86_64 => "x86_64", Aarch64 => "aarch64" });

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReleaseQuery {
    pub product: Product,
    pub distribution: Distribution,
    pub channel: Channel,
    pub os: OperatingSystem,
    pub architecture: Architecture,
}
impl ReleaseQuery {
    pub fn validate(&self) -> Result<(), InvalidRelease> {
        let supported = match self.product {
            Product::Android => {
                self.os == OperatingSystem::Android && self.architecture == Architecture::Aarch64
            }
            Product::Server => {
                matches!(self.os, OperatingSystem::Windows | OperatingSystem::Linux)
                    && self.architecture == Architecture::X86_64
            }
            Product::CloudNode | Product::Client | Product::Remote => {
                self.os == OperatingSystem::Windows && self.architecture == Architecture::X86_64
            }
        };
        supported.then_some(()).ok_or(InvalidRelease)
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct ReleaseSpec {
    pub target: ReleaseQuery,
    pub build_number: i64,
    pub version: String,
    /// HTTPS base URL of the TUF metadata repository. Must end with `/`.
    pub metadata_base_url: String,
    /// HTTPS base URL of the TUF targets repository. Must end with `/`.
    pub targets_base_url: String,
    /// Exact TUF target path. It is resolved only by the verified TUF client.
    pub target_name: String,
    pub sha256: String,
    /// SHA-256 of the platform signing certificate DER. Required for Windows and Android.
    pub platform_signer_sha256: Option<String>,
    pub size_bytes: i64,
}
fn digest_text(value: &str) -> bool {
    value.len() == 64
        && value
            .bytes()
            .all(|byte_value| byte_value.is_ascii_digit() || (b'a'..=b'f').contains(&byte_value))
}
fn public_base_url(value: &str) -> bool {
    if value.len() > 2048
        || value
            .bytes()
            .any(|byte_value| byte_value.is_ascii_whitespace() || byte_value.is_ascii_control())
    {
        return false;
    }
    url::Url::parse(value).is_ok_and(|url| {
        url.scheme() == "https"
            && url.host_str().is_some()
            && url.username().is_empty()
            && url.password().is_none()
            && url.fragment().is_none()
            && url.query().is_none()
            && url.path().ends_with('/')
    })
}
fn target_name(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 512
        && !value.starts_with(['/', '\\'])
        && !value.ends_with(['/', '\\'])
        && !value.contains('\\')
        && !value
            .bytes()
            .any(|byte_value| byte_value.is_ascii_whitespace() || byte_value.is_ascii_control())
        && value
            .split('/')
            .all(|component| !component.is_empty() && component != "." && component != "..")
}
impl ReleaseSpec {
    pub fn validate(&self) -> Result<(), InvalidRelease> {
        self.target.validate()?;
        let platform_signer_valid = match self.target.os {
            OperatingSystem::Windows | OperatingSystem::Android => self
                .platform_signer_sha256
                .as_deref()
                .is_some_and(digest_text),
            OperatingSystem::Linux => self.platform_signer_sha256.is_none(),
        };
        let valid = self.build_number > 0
            && self.size_bytes > 0
            && self.size_bytes <= (1_i64 << 40)
            && !self.version.trim().is_empty()
            && self.version.chars().count() <= 64
            && !self.version.chars().any(char::is_control)
            && public_base_url(&self.metadata_base_url)
            && public_base_url(&self.targets_base_url)
            && target_name(&self.target_name)
            && digest_text(&self.sha256)
            && platform_signer_valid;
        valid.then_some(()).ok_or(InvalidRelease)
    }
    pub fn digest(&self) -> Result<[u8; 32], InvalidRelease> {
        self.validate()?;
        let bytes = serde_json::to_vec(self).map_err(|_| InvalidRelease)?;
        Ok(Sha256::digest(bytes).into())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn spec() -> ReleaseSpec {
        ReleaseSpec {
            target: ReleaseQuery {
                product: Product::Server,
                distribution: Distribution::Customer,
                channel: Channel::Stable,
                os: OperatingSystem::Linux,
                architecture: Architecture::X86_64,
            },
            build_number: 32,
            version: "1.2.3".into(),
            metadata_base_url: "https://example.invalid/metadata/".into(),
            targets_base_url: "https://example.invalid/targets/".into(),
            target_name: "server/server.tar.gz".into(),
            sha256: "a".repeat(64),
            platform_signer_sha256: None,
            size_bytes: 500,
        }
    }
    #[test]
    fn every_platform_is_explicit_and_unsupported_products_have_no_aliases() {
        let mut release_spec = spec();
        release_spec.validate().unwrap();
        release_spec.target.os = OperatingSystem::Windows;
        release_spec.platform_signer_sha256 = Some("b".repeat(64));
        release_spec.validate().unwrap();
        release_spec.target.product = Product::Android;
        assert!(release_spec.validate().is_err());
        release_spec.target.os = OperatingSystem::Android;
        release_spec.target.architecture = Architecture::Aarch64;
        release_spec.platform_signer_sha256 = Some("b".repeat(64));
        release_spec.validate().unwrap();
        for value in ["panel", "gammaray", "Client", ""] {
            assert!(value.parse::<Product>().is_err());
        }
        let mut value = serde_json::to_value(&release_spec).unwrap();
        value["target"]
            .as_object_mut()
            .unwrap()
            .remove("architecture");
        assert!(serde_json::from_value::<ReleaseSpec>(value).is_err());
    }
    #[test]
    fn content_identity_includes_every_dimension_and_signed_metadata_reference() {
        let base = spec();
        let digest = base.digest().unwrap();
        for field in [
            "build_number",
            "version",
            "metadata_base_url",
            "targets_base_url",
            "target_name",
            "sha256",
            "size_bytes",
        ] {
            let mut value = serde_json::to_value(&base).unwrap();
            value[field] = match field {
                "build_number" | "size_bytes" => serde_json::json!(999),
                "sha256" => serde_json::json!("c".repeat(64)),
                "metadata_base_url" | "targets_base_url" => {
                    serde_json::json!("https://example.invalid/other/")
                }
                _ => serde_json::json!("other"),
            };
            assert_ne!(
                serde_json::from_value::<ReleaseSpec>(value)
                    .unwrap()
                    .digest()
                    .unwrap(),
                digest
            );
        }
        let mut signed_windows_release = spec();
        signed_windows_release.target.os = OperatingSystem::Windows;
        signed_windows_release.platform_signer_sha256 = Some("b".repeat(64));
        let signed_windows_digest = signed_windows_release.digest().unwrap();
        signed_windows_release.platform_signer_sha256 = Some("c".repeat(64));
        assert_ne!(
            signed_windows_release.digest().unwrap(),
            signed_windows_digest
        );
        for (field, other) in [
            ("product", "client"),
            ("distribution", "official"),
            ("channel", "preview"),
            ("os", "windows"),
        ] {
            let mut value = serde_json::to_value(&base).unwrap();
            value["target"][field] = serde_json::json!(other);
            if field == "product" {
                value["target"]["os"] = serde_json::json!("windows");
            }
            if field == "product" || field == "os" {
                value["platform_signer_sha256"] = serde_json::json!("c".repeat(64));
            }
            assert_ne!(
                serde_json::from_value::<ReleaseSpec>(value)
                    .unwrap()
                    .digest()
                    .unwrap(),
                digest
            );
        }
    }
    #[test]
    fn urls_never_carry_passwords_query_tokens_or_implicit_transport() {
        for url in [
            "http://example.invalid/a",
            "https://u:p@example.invalid/a",
            "https://example.invalid/a?token=secret",
            "https://example.invalid/a#fragment",
            " https://example.invalid/a",
            "https://example.invalid/a\n",
            "file:///tmp/a",
        ] {
            let mut release_spec = spec();
            release_spec.metadata_base_url = url.into();
            assert!(release_spec.validate().is_err());
            release_spec = spec();
            release_spec.targets_base_url = url.into();
            assert!(release_spec.validate().is_err());
        }
        for name in [
            "",
            "/absolute.exe",
            "../escape.exe",
            "a/../b.exe",
            "a\\b.exe",
            "a b.exe",
        ] {
            let mut release_spec = spec();
            release_spec.target_name = name.into();
            assert!(release_spec.validate().is_err());
        }
    }
    #[test]
    fn sizes_hashes_builds_and_unknown_fields_are_bounded_without_coercion() {
        for size in [-1, 0, (1_i64 << 40) + 1] {
            let mut release_spec = spec();
            release_spec.size_bytes = size;
            assert!(release_spec.validate().is_err());
        }
        for hash in ["A".repeat(64), "a".repeat(63), "g".repeat(64)] {
            let mut release_spec = spec();
            release_spec.sha256 = hash;
            assert!(release_spec.validate().is_err());
        }
        let mut release_spec = spec();
        release_spec.build_number = 0;
        assert!(release_spec.validate().is_err());
        release_spec = spec();
        release_spec.target.os = OperatingSystem::Windows;
        assert!(release_spec.validate().is_err());
        release_spec.platform_signer_sha256 = Some("b".repeat(64));
        assert!(release_spec.validate().is_ok());
        let mut value = serde_json::to_value(spec()).unwrap();
        value["signed"] = serde_json::json!(true);
        assert!(serde_json::from_value::<ReleaseSpec>(value).is_err());
    }
}
