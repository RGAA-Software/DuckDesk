//! Private infrastructure transport. Console owns business authorization above this layer.

use std::{net::SocketAddr, time::Duration};

use anyhow::{Context, Result, ensure};
use bytes::Bytes;
use iroh::{Endpoint, RelayMode, endpoint::presets};
pub use iroh::{EndpointAddr, RelayUrl, endpoint::Connection};

pub const ALPN: &[u8] = b"pixels/transport/1";
pub const MAX_CONTROL_MESSAGE: usize = 1024 * 1024;

#[derive(Debug, Clone, Default, serde::Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct EndpointConfig {
    pub bind_address: Option<SocketAddr>,
    pub relay_urls: Vec<RelayUrl>,
    pub relay_only: bool,
}

pub struct TransportEndpoint {
    endpoint: Endpoint,
}

impl TransportEndpoint {
    pub async fn bind(config: EndpointConfig) -> Result<Self> {
        ensure!(
            !config.relay_only || !config.relay_urls.is_empty(),
            "relay-only transport requires a configured private relay"
        );
        let relay_mode = if config.relay_urls.is_empty() {
            RelayMode::Disabled
        } else {
            RelayMode::custom(config.relay_urls)
        };
        // Minimal configures crypto only: no public relay or public address lookup service.
        let mut builder = Endpoint::builder(presets::Minimal)
            .alpns(vec![ALPN.to_vec()])
            .relay_mode(relay_mode);
        if config.relay_only {
            builder = builder.clear_ip_transports();
        } else if let Some(bind_address) = config.bind_address {
            builder = builder.clear_ip_transports().bind_addr(bind_address)?;
        }
        Ok(Self {
            endpoint: builder.bind().await.context("bind Pixels transport")?,
        })
    }

    pub fn address(&self) -> EndpointAddr {
        self.endpoint.addr()
    }

    pub async fn online(&self, timeout: Duration) -> Result<()> {
        tokio::time::timeout(timeout, self.endpoint.online())
            .await
            .context("private relay did not become reachable")?;
        Ok(())
    }

    pub async fn connect(&self, target: EndpointAddr) -> Result<Connection> {
        self.endpoint
            .connect(target, ALPN)
            .await
            .context("connect Pixels transport")
    }

    pub async fn accept(&self) -> Result<Connection> {
        let incoming = self.endpoint.accept().await.context("endpoint closed")?;
        incoming.await.context("accept Pixels transport")
    }

    pub async fn close(&self) {
        self.endpoint.close().await;
    }
}

pub fn send_media(connection: &Connection, payload: Bytes) -> Result<()> {
    let maximum_size = connection
        .max_datagram_size()
        .context("peer does not accept datagrams")?;
    ensure!(
        payload.len() <= maximum_size,
        "media payload exceeds path MTU"
    );
    connection.send_datagram(payload)?;
    Ok(())
}
