use px_pg::{DatabaseConfig, Transport};
use std::{
    env,
    net::{IpAddr, SocketAddr},
    path::PathBuf,
};
use uuid::Uuid;
use zeroize::Zeroizing;

pub struct Settings {
    pub database: DatabaseConfig,
    pub deployment: Uuid,
    pub listen: SocketAddr,
    pub static_directory: PathBuf,
    pub tls: Option<(PathBuf, PathBuf)>,
    pub signing_key: PathBuf,
    pub trust_store: PathBuf,
    pub trusted_proxy: Option<IpAddr>,
}

#[derive(Debug, thiserror::Error)]
#[error("invalid Auth configuration (check required deployment, database, signing key, static and TLS settings)")]
pub struct ConfigurationError;

impl Settings {
    pub fn from_env() -> Result<Self, ConfigurationError> {
        let required = |key| env::var(key).map_err(|_| ConfigurationError);
        let local = match env::var("PIXELS_AUTH_LOCAL_DEVELOPMENT").as_deref() {
            Ok("1") => true,
            Err(env::VarError::NotPresent) | Ok("0") => false,
            _ => return Err(ConfigurationError),
        };
        let deployment = required("PIXELS_DEPLOYMENT_ID")?
            .parse::<Uuid>()
            .map_err(|_| ConfigurationError)?;
        if deployment.is_nil() {
            return Err(ConfigurationError);
        }
        let database = DatabaseConfig::parse(
            &Zeroizing::new(required("PIXELS_DATABASE_URL")?),
            if local {
                Transport::LocalDevelopment
            } else {
                Transport::VerifyFull
            },
        )
        .map_err(|_| ConfigurationError)?;
        let listen: SocketAddr = required("PIXELS_AUTH_LISTEN")?
            .parse()
            .map_err(|_| ConfigurationError)?;
        if local && !listen.ip().is_loopback() {
            return Err(ConfigurationError);
        }
        let signing_key = PathBuf::from(required("PIXELS_AUTH_SIGNING_KEY")?);
        let trust_store = PathBuf::from(required("PIXELS_AUTH_TRUST_STORE")?);
        let tls = match (
            env::var_os("PIXELS_AUTH_TLS_CERT"),
            env::var_os("PIXELS_AUTH_TLS_KEY"),
        ) {
            (Some(cert), Some(key)) => Some((PathBuf::from(cert), PathBuf::from(key))),
            (None, None) if local => None,
            _ => return Err(ConfigurationError),
        };
        let static_directory = PathBuf::from(required("PIXELS_AUTH_STATIC_DIRECTORY")?);
        if !static_directory.join("index.html").is_file() {
            return Err(ConfigurationError);
        }
        let trusted_proxy = match env::var("PIXELS_AUTH_TRUSTED_PROXY_IP") {
            Ok(address) => Some(address.parse::<IpAddr>().map_err(|_| ConfigurationError)?),
            Err(env::VarError::NotPresent) => None,
            Err(env::VarError::NotUnicode(_)) => return Err(ConfigurationError),
        };
        Ok(Self {
            database,
            deployment,
            listen,
            static_directory,
            tls,
            signing_key,
            trust_store,
            trusted_proxy,
        })
    }
}
