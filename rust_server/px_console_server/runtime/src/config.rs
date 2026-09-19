use crate::{
    IngressPolicy, LicenseEntitlement, LicenseLaunchConfig, RuntimeSecrets, WorkspaceKeyFile,
};
use px_console_store::{CacheOptions, WorkspaceVault};
use px_pg::{DatabaseConfig, Transport};
use px_private_files::CacheRoot;
use serde::Deserialize;
use std::{env, net::SocketAddr, path::PathBuf, sync::Arc, time::Duration};
use uuid::Uuid;
use zeroize::Zeroizing;

#[derive(Debug, thiserror::Error)]
#[error("invalid Console configuration (check database, deployment, listener, TLS, origin, license and private-key settings)")]
pub struct ConfigurationError;

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct KeySource {
    id: Uuid,
    path: PathBuf,
}

#[derive(Clone)]
pub struct RelayEndpoint {
    pub host: String,
    pub port: u16,
    pub app_key: String,
}

pub struct ConsoleLaunchConfig {
    database: DatabaseConfig,
    deployment: Uuid,
    listen: SocketAddr,
    static_directory: PathBuf,
    tls: Option<(PathBuf, PathBuf)>,
    policy: IngressPolicy,
    guests_enabled: bool,
    guest_lifetime: Duration,
    guest_source_key: PathBuf,
    active_workspace_key: Uuid,
    workspace_keys: Vec<WorkspaceKeyFile>,
    recording_cache_directory: PathBuf,
    recording_cache_options: CacheOptions,
    relay: Option<RelayEndpoint>,
    license: LicenseLaunchConfig,
}

pub struct ConsoleLaunch {
    pub database: DatabaseConfig,
    pub deployment: Uuid,
    pub listen: SocketAddr,
    pub static_directory: PathBuf,
    pub tls: Option<(PathBuf, PathBuf)>,
    pub policy: IngressPolicy,
    pub vault: Arc<WorkspaceVault>,
    pub guests: crate::GuestAdmission,
    pub recording_cache_root: Arc<CacheRoot>,
    pub recording_cache_options: CacheOptions,
    pub relay: Option<RelayEndpoint>,
    pub license: LicenseEntitlement,
}

impl ConsoleLaunchConfig {
    pub fn from_env() -> Result<Self, ConfigurationError> {
        Self::parse(|key| env::var(key).ok())
    }

    fn parse(get: impl Fn(&str) -> Option<String>) -> Result<Self, ConfigurationError> {
        let required = |key| {
            get(key)
                .filter(|value| !value.is_empty())
                .ok_or(ConfigurationError)
        };
        let local = flag(get("PIXELS_CONSOLE_LOCAL_DEVELOPMENT"), false)?;
        let deployment = required("PIXELS_DEPLOYMENT_ID")?
            .parse::<Uuid>()
            .map_err(|_| ConfigurationError)?;
        if deployment.is_nil() {
            return Err(ConfigurationError);
        }
        let database = DatabaseConfig::parse(
            &Zeroizing::new(required("PIXELS_CONSOLE_DATABASE_URL")?),
            if local {
                Transport::LocalDevelopment
            } else {
                Transport::VerifyFull
            },
        )
        .map_err(|_| ConfigurationError)?;
        let listen = required("PIXELS_CONSOLE_LISTEN")?
            .parse::<SocketAddr>()
            .map_err(|_| ConfigurationError)?;
        if listen.port() == 0 || (local && !listen.ip().is_loopback()) {
            return Err(ConfigurationError);
        }
        let static_directory = PathBuf::from(required("PIXELS_CONSOLE_STATIC_DIRECTORY")?);
        let tls = match (
            get("PIXELS_CONSOLE_TLS_CERT"),
            get("PIXELS_CONSOLE_TLS_KEY"),
        ) {
            (Some(cert), Some(key)) if !cert.is_empty() && !key.is_empty() => {
                Some((PathBuf::from(cert), PathBuf::from(key)))
            }
            (None, None) if local => None,
            _ => return Err(ConfigurationError),
        };
        let registration = flag(get("PIXELS_CONSOLE_REGISTRATION"), false)?;
        let guests_enabled = flag(get("PIXELS_CONSOLE_GUESTS"), false)?;
        let session_lifetime = seconds(required("PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS")?)?;
        let guest_lifetime = seconds(required("PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS")?)?;
        let policy = IngressPolicy::new(
            &required("PIXELS_CONSOLE_PUBLIC_ORIGIN")?,
            registration,
            session_lifetime,
            local,
        )
        .map_err(|_| ConfigurationError)?;
        if !(60..=86400).contains(&guest_lifetime.as_secs()) {
            return Err(ConfigurationError);
        }
        let guest_source_key = PathBuf::from(required("PIXELS_CONSOLE_GUEST_SOURCE_KEY")?);
        let active_workspace_key = required("PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY")?
            .parse::<Uuid>()
            .map_err(|_| ConfigurationError)?;
        let sources =
            serde_json::from_str::<Vec<KeySource>>(&required("PIXELS_CONSOLE_WORKSPACE_KEYS")?)
                .map_err(|_| ConfigurationError)?;
        if active_workspace_key.is_nil() || sources.is_empty() || sources.len() > 32 {
            return Err(ConfigurationError);
        }
        let workspace_keys = sources
            .into_iter()
            .map(|source| WorkspaceKeyFile {
                id: source.id,
                path: source.path,
            })
            .collect();
        let recording_cache_directory =
            PathBuf::from(required("PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY")?);
        let recording_cache_options = CacheOptions {
            byte_limit: number(&required("PIXELS_CONSOLE_RECORDING_CACHE_BYTES")?)?,
            maximum_downloads: number(&required("PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS")?)?,
            ttl_seconds: number(&required("PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS")?)?,
        };
        if !(1_048_576..=1_099_511_627_776).contains(&recording_cache_options.byte_limit)
            || !(1..=32).contains(&recording_cache_options.maximum_downloads)
            || !(60..=604800).contains(&recording_cache_options.ttl_seconds)
        {
            return Err(ConfigurationError);
        }
        let relay = match (
            get("PIXELS_RELAY_PUBLIC_HOST").filter(|value| !value.is_empty()),
            get("PIXELS_RELAY_PUBLIC_PORT").filter(|value| !value.is_empty()),
            get("PIXELS_RELAY_APP_KEY").filter(|value| !value.is_empty()),
        ) {
            (None, None, None) => None,
            (Some(host), Some(port), Some(app_key))
                if valid_host(&host) && (16..=512).contains(&app_key.len()) =>
            {
                Some(RelayEndpoint {
                    host,
                    port: port.parse::<u16>().map_err(|_| ConfigurationError)?,
                    app_key,
                })
            }
            _ => return Err(ConfigurationError),
        };
        let authority_deployment_id = required("PIXELS_CONSOLE_LICENSE_AUTHORITY_DEPLOYMENT_ID")?
            .parse::<Uuid>()
            .map_err(|_| ConfigurationError)?;
        let license = LicenseLaunchConfig::new(
            &required("PIXELS_CONSOLE_DISTRIBUTION")?,
            required("PIXELS_CONSOLE_MACHINE_SHA256")?,
            authority_deployment_id,
            PathBuf::from(required("PIXELS_CONSOLE_LICENSE_TRUST_STORE")?),
            PathBuf::from(required("PIXELS_CONSOLE_LICENSE_FILE")?),
            PathBuf::from(required("PIXELS_CONSOLE_LICENSE_STATE_DIRECTORY")?),
            get("PIXELS_CONSOLE_AUTH_VERIFY_URL").filter(|value| !value.is_empty()),
            get("PIXELS_CONSOLE_AUTH_VERIFY_CA")
                .filter(|value| !value.is_empty())
                .map(PathBuf::from),
            local,
        )
        .map_err(|_| ConfigurationError)?;
        Ok(Self {
            database,
            deployment,
            listen,
            static_directory,
            tls,
            policy,
            guests_enabled,
            guest_lifetime,
            guest_source_key,
            active_workspace_key,
            workspace_keys,
            recording_cache_directory,
            recording_cache_options,
            relay,
            license,
        })
    }

    pub async fn load(self) -> Result<ConsoleLaunch, ConfigurationError> {
        let static_directory = tokio::fs::canonicalize(&self.static_directory)
            .await
            .map_err(|_| ConfigurationError)?;
        let index_path = tokio::fs::canonicalize(static_directory.join("index.html"))
            .await
            .map_err(|_| ConfigurationError)?;
        if !index_path.starts_with(&static_directory) {
            return Err(ConfigurationError);
        }
        let index_metadata = tokio::fs::metadata(index_path)
            .await
            .map_err(|_| ConfigurationError)?;
        if !index_metadata.is_file() || index_metadata.len() > crate::static_files::MAX_FILE_BYTES {
            return Err(ConfigurationError);
        }
        let secrets = RuntimeSecrets::load(
            self.deployment,
            self.active_workspace_key,
            self.workspace_keys,
            self.guest_source_key,
            self.guests_enabled,
            self.guest_lifetime,
        )
        .await
        .map_err(|_| ConfigurationError)?;
        let (vault, guests) = secrets.into_parts();
        let recording_cache_root = tokio::task::spawn_blocking(move || {
            CacheRoot::open(&self.recording_cache_directory, self.deployment)
        })
        .await
        .map_err(|_| ConfigurationError)?
        .map_err(|_| ConfigurationError)?;
        let license = self
            .license
            .admit(self.deployment)
            .await
            .map_err(|_| ConfigurationError)?;
        Ok(ConsoleLaunch {
            database: self.database,
            deployment: self.deployment,
            listen: self.listen,
            static_directory,
            tls: self.tls,
            policy: self.policy,
            vault,
            guests,
            recording_cache_root,
            recording_cache_options: self.recording_cache_options,
            relay: self.relay,
            license,
        })
    }
}

fn flag(value: Option<String>, default: bool) -> Result<bool, ConfigurationError> {
    match value.as_deref() {
        None => Ok(default),
        Some("0") => Ok(false),
        Some("1") => Ok(true),
        _ => Err(ConfigurationError),
    }
}

fn seconds(value: String) -> Result<Duration, ConfigurationError> {
    let value = value.parse::<u64>().map_err(|_| ConfigurationError)?;
    Ok(Duration::from_secs(value))
}

fn number<T: std::str::FromStr>(value: &str) -> Result<T, ConfigurationError> {
    value.parse().map_err(|_| ConfigurationError)
}

fn valid_host(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 253
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b':'))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    fn valid() -> HashMap<String, String> {
        let active = "12345678-1234-4234-8234-123456789abc";
        HashMap::from([
            ("PIXELS_CONSOLE_LOCAL_DEVELOPMENT".into(), "1".into()),
            ("PIXELS_DEPLOYMENT_ID".into(), Uuid::new_v4().to_string()),
            (
                "PIXELS_CONSOLE_DATABASE_URL".into(),
                "postgres://pixels_console_runtime:secret@127.0.0.1:5432/pixels_console".into(),
            ),
            ("PIXELS_CONSOLE_LISTEN".into(), "127.0.0.1:8443".into()),
            (
                "PIXELS_CONSOLE_STATIC_DIRECTORY".into(),
                "web/px_console/dist".into(),
            ),
            (
                "PIXELS_CONSOLE_PUBLIC_ORIGIN".into(),
                "http://127.0.0.1:8443".into(),
            ),
            ("PIXELS_CONSOLE_REGISTRATION".into(), "1".into()),
            ("PIXELS_CONSOLE_GUESTS".into(), "1".into()),
            (
                "PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS".into(),
                "3600".into(),
            ),
            (
                "PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS".into(),
                "3600".into(),
            ),
            (
                "PIXELS_CONSOLE_GUEST_SOURCE_KEY".into(),
                "private/guest.key".into(),
            ),
            ("PIXELS_CONSOLE_WORKSPACE_ACTIVE_KEY".into(), active.into()),
            (
                "PIXELS_CONSOLE_WORKSPACE_KEYS".into(),
                format!(r#"[{{"id":"{active}","path":"private/workspace.key"}}]"#),
            ),
            (
                "PIXELS_CONSOLE_RECORDING_CACHE_DIRECTORY".into(),
                "C:\\Pixels\\recordings".into(),
            ),
            (
                "PIXELS_CONSOLE_RECORDING_CACHE_BYTES".into(),
                "1073741824".into(),
            ),
            (
                "PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS".into(),
                "4".into(),
            ),
            (
                "PIXELS_CONSOLE_RECORDING_CACHE_TTL_SECONDS".into(),
                "86400".into(),
            ),
            (
                "PIXELS_CONSOLE_LICENSE_AUTHORITY_DEPLOYMENT_ID".into(),
                Uuid::new_v4().to_string(),
            ),
            ("PIXELS_CONSOLE_DISTRIBUTION".into(), "customer".into()),
            ("PIXELS_CONSOLE_MACHINE_SHA256".into(), "a".repeat(64)),
            (
                "PIXELS_CONSOLE_LICENSE_TRUST_STORE".into(),
                "private/license-trust.json".into(),
            ),
            (
                "PIXELS_CONSOLE_LICENSE_FILE".into(),
                "private/console.license".into(),
            ),
            (
                "PIXELS_CONSOLE_LICENSE_STATE_DIRECTORY".into(),
                "private/license-state".into(),
            ),
        ])
    }

    fn parse(values: &HashMap<String, String>) -> Result<ConsoleLaunchConfig, ConfigurationError> {
        ConsoleLaunchConfig::parse(|key| values.get(key).cloned())
    }

    #[test]
    fn new_configuration_is_explicit_and_has_no_retired_endpoint_fallback() {
        assert!(parse(&valid()).is_ok());
        for (key, value) in [
            ("PIXELS_CONSOLE_LISTEN", "0.0.0.0:8443"),
            ("PIXELS_CONSOLE_REGISTRATION", "true"),
            ("PIXELS_CONSOLE_SESSION_LIFETIME_SECONDS", "0"),
            ("PIXELS_CONSOLE_GUEST_LIFETIME_SECONDS", "86401"),
            ("PIXELS_CONSOLE_PUBLIC_ORIGIN", "http://public.example.test"),
            ("PIXELS_CONSOLE_RECORDING_CACHE_BYTES", "not-a-number"),
            ("PIXELS_CONSOLE_RECORDING_CACHE_DOWNLOADS", "0"),
        ] {
            let mut values = valid();
            values.insert(key.into(), value.into());
            assert!(parse(&values).is_err(), "accepted {key}={value}");
        }
        let mut missing = valid();
        missing.remove("PIXELS_CONSOLE_GUEST_SOURCE_KEY");
        assert!(parse(&missing).is_err());

        let mut relay = valid();
        relay.insert(
            "PIXELS_RELAY_PUBLIC_HOST".into(),
            "relay.example.test".into(),
        );
        relay.insert("PIXELS_RELAY_PUBLIC_PORT".into(), "4605".into());
        relay.insert("PIXELS_RELAY_APP_KEY".into(), "deployment-relay-key".into());
        assert!(parse(&relay).is_ok());
        relay.remove("PIXELS_RELAY_APP_KEY");
        assert!(parse(&relay).is_err());

        let mut official = valid();
        official.insert("PIXELS_CONSOLE_DISTRIBUTION".into(), "official".into());
        assert!(parse(&official).is_err());
        official.insert(
            "PIXELS_CONSOLE_AUTH_VERIFY_URL".into(),
            "http://127.0.0.1:8444/api/auth/licenses/verify".into(),
        );
        assert!(parse(&official).is_ok());
        official.insert(
            "PIXELS_CONSOLE_AUTH_VERIFY_CA".into(),
            "private/auth-ca.pem".into(),
        );
        assert!(parse(&official).is_ok());
        let mut customer = valid();
        customer.insert(
            "PIXELS_CONSOLE_AUTH_VERIFY_URL".into(),
            "https://auth.example.test/api/auth/licenses/verify".into(),
        );
        assert!(parse(&customer).is_err());
        let mut customer_with_ca = valid();
        customer_with_ca.insert(
            "PIXELS_CONSOLE_AUTH_VERIFY_CA".into(),
            "private/auth-ca.pem".into(),
        );
        assert!(parse(&customer_with_ca).is_err());
    }
}
