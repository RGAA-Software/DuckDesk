//! Private infrastructure transport. Console owns business authorization above this layer.

use std::{net::SocketAddr, time::Duration};

use anyhow::{Context, Result, ensure};
use bytes::Bytes;
use iroh::{
    Endpoint, RelayMode, Watcher,
    endpoint::{QuicTransportConfig, presets},
};
pub use iroh::{EndpointAddr, RelayUrl, endpoint::Connection};
use iroh_relay::{RelayConfig, RelayQuicConfig, tls::CaTlsConfig};
use rustls::pki_types::{CertificateDer, pem::PemObject};

mod relay_candidates;
mod relay_health;

#[cfg(test)]
mod connection_liveness_tests;

pub const ALPN: &[u8] = b"pixels/transport/1";
pub const MAX_CONTROL_MESSAGE: usize = 1024 * 1024;

const CONNECTION_IDLE_TIMEOUT: Duration = Duration::from_secs(8);
const CONNECTION_KEEP_ALIVE_INTERVAL: Duration = Duration::from_secs(1);

#[derive(Debug, Clone, serde::Deserialize)]
#[serde(deny_unknown_fields)]
pub struct PrivateRelay {
    pub url: RelayUrl,
    pub qad_port: Option<u16>,
}

#[derive(Debug, Clone, Default, serde::Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct EndpointConfig {
    pub bind_address: Option<SocketAddr>,
    pub relays: Vec<PrivateRelay>,
    pub relay_only: bool,
    pub ca_certificates_pem: Vec<String>,
}

pub struct TransportEndpoint {
    endpoint: Endpoint,
    relay_health: Option<relay_health::RelayHealth>,
    relay_candidates: std::sync::Arc<relay_candidates::RelayCandidates>,
}

impl TransportEndpoint {
    pub async fn bind(config: EndpointConfig) -> Result<Self> {
        ensure!(
            !config.relay_only || !config.relays.is_empty(),
            "relay-only transport requires a configured private relay"
        );
        for relay in &config.relays {
            ensure!(relay.qad_port != Some(0), "QAD port cannot be zero");
            ensure!(
                relay.qad_port.is_none() || relay.url.scheme() == "https",
                "QAD requires HTTPS relay configuration"
            );
        }
        let relay_candidates =
            std::sync::Arc::new(relay_candidates::RelayCandidates::new(&config.relays)?);
        // Retain a private Relay transport even with no initial candidates, so
        // Console can add one later. An empty Custom map never uses public Relays.
        let relay_mode = RelayMode::Custom(
            config
                .relays
                .into_iter()
                .map(|relay| RelayConfig::new(relay.url, relay.qad_port.map(RelayQuicConfig::new)))
                .collect(),
        );
        // Minimal configures crypto only: no public relay or public address lookup service.
        // Keep QUIC's default congestion controller. Bound unsent media independently;
        // an application frame deadline must not rely on a particular controller's probe cycle.
        let transport_config = QuicTransportConfig::builder()
            // Detect an unresponsive peer in time for existing-session readmission.
            // QUIC heartbeats keep a healthy static desktop alive without video frames.
            .max_idle_timeout(Some(CONNECTION_IDLE_TIMEOUT.try_into()?))
            .keep_alive_interval(CONNECTION_KEEP_ALIVE_INTERVAL)
            .datagram_send_buffer_size(64 * 1024)
            .build();
        let mut builder = Endpoint::builder(presets::Minimal)
            .alpns(vec![ALPN.to_vec()])
            .transport_config(transport_config)
            .relay_mode(relay_mode);
        if !config.ca_certificates_pem.is_empty() {
            let mut roots = Vec::new();
            for certificate_pem in config.ca_certificates_pem {
                let certificates = CertificateDer::pem_slice_iter(certificate_pem.as_bytes())
                    .collect::<std::result::Result<Vec<_>, _>>()?;
                ensure!(!certificates.is_empty(), "CA PEM has no certificates");
                roots.extend(certificates);
            }
            builder = builder.ca_tls_config(CaTlsConfig::default().with_extra_roots(roots));
        }
        if config.relay_only {
            builder = builder.clear_ip_transports();
        } else if let Some(bind_address) = config.bind_address {
            builder = builder.clear_ip_transports().bind_addr(bind_address)?;
        }
        let endpoint = builder.bind().await.context("bind Pixels transport")?;
        let relay_health = Some(relay_health::RelayHealth::start(
            endpoint.clone(),
            relay_candidates.clone(),
        ));
        Ok(Self {
            endpoint,
            relay_health,
            relay_candidates,
        })
    }

    pub fn address(&self) -> EndpointAddr {
        self.endpoint.addr()
    }

    pub async fn update_relays(&self, relays: Vec<PrivateRelay>) -> Result<()> {
        self.relay_candidates.update(&self.endpoint, relays).await
    }

    pub fn relay_status(&self) -> Vec<iroh::endpoint::RelayStatus> {
        self.endpoint.home_relay_status().get()
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
        if let Some(relay_health) = &self.relay_health {
            relay_health.stop();
        }
        self.endpoint.close().await;
    }
}

pub async fn send_media(connection: &Connection, payload: Bytes) -> Result<()> {
    let maximum_size = connection
        .max_datagram_size()
        .context("peer does not accept datagrams")?;
    ensure!(
        payload.len() <= maximum_size,
        "media payload exceeds path MTU"
    );
    // Unlike send_datagram, this does not silently evict earlier fragments of the
    // same frame. Expiry fails the current batch so its existing recovery path can
    // request a fresh reference frame instead of extending the stale queue.
    tokio::time::timeout(
        Duration::from_millis(20),
        connection.send_datagram_wait(payload),
    )
    .await
    .context("media send queue remained full")??;
    Ok(())
}
