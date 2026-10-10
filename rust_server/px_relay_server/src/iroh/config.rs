use std::{
    net::SocketAddr,
    path::{Path, PathBuf},
};

use anyhow::{ensure, Context, Result};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct IrohRelayConfig {
    pub https_bind: SocketAddr,
    pub qad_bind: SocketAddr,
    pub qad_public_port: Option<u16>,
    #[serde(default)]
    pub console_managed: bool,
    pub certificate_file: PathBuf,
    pub private_key_file: PathBuf,
    #[serde(default = "default_connection_limit")]
    pub max_connections: usize,
}

fn default_connection_limit() -> usize {
    4096
}

impl IrohRelayConfig {
    pub fn load(path: &Path) -> Result<Self> {
        let absolute_path = std::fs::canonicalize(path).context("read Relay configuration path")?;
        let contents = std::fs::read(&absolute_path).context("read Relay configuration")?;
        let mut config: Self =
            serde_json::from_slice(&contents).context("parse Relay configuration")?;
        config.validate()?;
        let directory = absolute_path
            .parent()
            .context("Relay configuration has no parent directory")?;
        if config.certificate_file.is_relative() {
            config.certificate_file = directory.join(&config.certificate_file);
        }
        if config.private_key_file.is_relative() {
            config.private_key_file = directory.join(&config.private_key_file);
        }
        Ok(config)
    }

    pub fn validate(&self) -> Result<()> {
        ensure!(
            self.qad_public_port != Some(0),
            "Relay public QAD port must be nonzero"
        );
        ensure!(
            (2..=100_000).contains(&self.max_connections),
            "Relay max_connections must be between 2 and 100000"
        );
        ensure!(
            self.https_bind.port() != 0 && self.qad_bind.port() != 0,
            "Relay listening ports must be explicit and nonzero"
        );
        ensure!(
            !self.certificate_file.as_os_str().is_empty()
                && !self.private_key_file.as_os_str().is_empty(),
            "Relay TLS certificate and key paths are required"
        );
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn configuration_rejects_invalid_capacity_ports_and_unknown_fields() {
        let mut config: IrohRelayConfig = serde_json::from_str(r#"{"https_bind":"127.0.0.1:18460","qad_bind":"127.0.0.1:18460","certificate_file":"relay.pem","private_key_file":"relay-key.pem"}"#).unwrap();
        assert!(config.validate().is_ok());
        config.max_connections = 1;
        assert!(config.validate().is_err());
        config.max_connections = 2;
        config.qad_bind.set_port(0);
        assert!(config.validate().is_err());
        assert!(serde_json::from_str::<IrohRelayConfig>(r#"{"https_bind":"127.0.0.1:18460","qad_bind":"127.0.0.1:18460","certificate_file":"relay.pem","private_key_file":"relay-key.pem","unexpected_field":true}"#).is_err());
    }
}
