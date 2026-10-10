use std::{sync::Arc, time::Duration};

use anyhow::{Context, Result};
use iroh_relay::server::{CertConfig, QuicConfig, RelayConfig, Server, ServerConfig, TlsConfig};
use rustls::pki_types::{pem::PemObject, CertificateDer, PrivateKeyDer};
use serde::Serialize;
use tokio_util::sync::CancellationToken;

use super::{admission::RelayAdmission, IrohRelayConfig};

#[derive(Debug, Serialize)]
pub struct RelaySnapshot {
    pub draining: bool,
    pub connections: usize,
    pub received_bytes: u64,
    pub forwarded_bytes: u64,
}

pub struct IrohRelay {
    server: Server,
    admission: Arc<RelayAdmission>,
    max_connections: usize,
    qad_port: u16,
}

#[derive(Clone)]
pub struct IrohRelayManagement {
    admission: Arc<RelayAdmission>,
    metrics: Arc<iroh_relay::server::Metrics>,
    max_connections: usize,
    qad_port: u16,
}

impl IrohRelayManagement {
    pub fn set_draining(&self, draining: bool) {
        self.admission.set_draining(draining);
    }

    pub fn report(
        &self,
        sequence: u64,
        product_version_code: u32,
    ) -> px_relay_control_protocol::RelayReport {
        let (draining, connections) = self.admission.snapshot();
        px_relay_control_protocol::RelayReport {
            sequence,
            product_version_code,
            draining,
            max_connections: self.max_connections as u32,
            current_connections: connections as u32,
            max_rooms: None,
            current_rooms: None,
            iroh_qad_port: Some(self.qad_port),
            uploaded_bytes: self.metrics.bytes_recv.get(),
            forwarded_bytes: self.metrics.bytes_sent.get(),
        }
    }
}

impl IrohRelay {
    pub fn management(&self) -> IrohRelayManagement {
        IrohRelayManagement {
            admission: self.admission.clone(),
            metrics: self.server.metrics().server.clone(),
            max_connections: self.max_connections,
            qad_port: self.qad_port,
        }
    }
    pub async fn start(config: &IrohRelayConfig) -> Result<Self> {
        config.validate()?;
        let certificates = CertificateDer::pem_file_iter(&config.certificate_file)
            .context("open Relay TLS certificate")?
            .collect::<Result<Vec<_>, _>>()
            .context("parse Relay TLS certificate")?;
        let key = PrivateKeyDer::from_pem_file(&config.private_key_file)
            .context("read Relay TLS private key")?;
        let tls = rustls::ServerConfig::builder_with_provider(Arc::new(
            rustls::crypto::ring::default_provider(),
        ))
        .with_safe_default_protocol_versions()?
        .with_no_client_auth()
        .with_single_cert(certificates, key)
        .context("configure Relay TLS certificate/key")?;
        let admission = Arc::new(RelayAdmission::new(config.max_connections));
        // The upstream ancillary HTTP endpoint is loopback-only. Public Relay uses TLS.
        let mut relay = RelayConfig::new(([127, 0, 0, 1], 0));
        relay.tls = Some(TlsConfig::new(
            config.https_bind,
            CertConfig::Manual { server_config: tls },
        ));
        relay.access = admission.clone();
        let mut server_config = ServerConfig::default();
        server_config.relay = Some(relay);
        server_config.quic = Some(QuicConfig::new(config.qad_bind));
        let server = Server::spawn(server_config)
            .await
            .context("start Relay HTTPS/QAD listeners")?;
        Ok(Self {
            server,
            admission,
            max_connections: config.max_connections,
            qad_port: config.qad_public_port.unwrap_or(config.qad_bind.port()),
        })
    }

    pub fn set_draining(&self, draining: bool) {
        self.admission.set_draining(draining);
    }

    pub fn snapshot(&self) -> RelaySnapshot {
        let (draining, connections) = self.admission.snapshot();
        let metrics = &self.server.metrics().server;
        RelaySnapshot {
            draining,
            connections,
            received_bytes: metrics.bytes_recv.get(),
            forwarded_bytes: metrics.bytes_sent.get(),
        }
    }

    pub async fn run(mut self, cancellation: CancellationToken) -> Result<()> {
        tracing::info!(https = ?self.server.https_addr(), qad = ?self.server.quic_addr(), "Pixels iroh Relay started");
        let mut interval = tokio::time::interval(Duration::from_secs(5));
        interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        loop {
            tokio::select! {
                biased;
                _ = cancellation.cancelled() => break,
                completed = self.server.join() => {
                    completed.context("Relay supervisor join failed")?.context("Relay listener failed")?;
                    anyhow::bail!("Relay listeners stopped unexpectedly");
                }
                _ = interval.tick() => {
                    let snapshot = self.snapshot();
                    tracing::info!(connections = snapshot.connections, received_bytes = snapshot.received_bytes,
                        forwarded_bytes = snapshot.forwarded_bytes, draining = snapshot.draining, "Relay traffic");
                }
            }
        }
        self.admission.set_draining(true);
        tokio::time::timeout(Duration::from_secs(10), self.server.shutdown())
            .await
            .context("Relay shutdown deadline exceeded")?
            .context("Relay shutdown failed")?;
        tracing::info!("Pixels iroh Relay stopped");
        Ok(())
    }
}
