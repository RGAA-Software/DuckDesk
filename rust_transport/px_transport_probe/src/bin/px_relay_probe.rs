//! Isolated private-relay/QAD probe, not the Console-managed product service.
use std::net::SocketAddr;

use anyhow::{Context, Result, ensure};
use iroh_relay::server::{CertConfig, QuicConfig, RelayConfig, Server, ServerConfig, TlsConfig};
use rustls::pki_types::{CertificateDer, PrivateKeyDer, pem::PemObject};

fn option<'arguments>(
    arguments: &'arguments [String],
    name: &str,
) -> Result<Option<&'arguments str>> {
    arguments
        .iter()
        .position(|argument| argument == name)
        .map(|position| {
            arguments
                .get(position + 1)
                .map(String::as_str)
                .context("option value missing")
        })
        .transpose()
}

#[tokio::main]
async fn main() -> Result<()> {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let bind_address: SocketAddr = arguments.first()
        .context("usage: px_relay_probe <bind-address:port> [--tls-cert PEM --tls-key PEM --qad-bind address:port]")?.parse()?;
    let certificate = option(&arguments, "--tls-cert")?;
    let private_key = option(&arguments, "--tls-key")?;
    let qad_bind = option(&arguments, "--qad-bind")?;
    ensure!(
        certificate.is_some() == private_key.is_some(),
        "TLS requires both certificate and private key"
    );
    ensure!(
        qad_bind.is_none() || certificate.is_some(),
        "QAD requires TLS configuration"
    );
    let mut config = ServerConfig::default();
    let mut relay = RelayConfig::new(bind_address);
    if let (Some(certificate), Some(private_key)) = (certificate, private_key) {
        let certificates = CertificateDer::pem_file_iter(certificate)?
            .collect::<std::result::Result<Vec<_>, _>>()?;
        let server_config = rustls::ServerConfig::builder_with_provider(std::sync::Arc::new(
            rustls::crypto::ring::default_provider(),
        ))
        .with_safe_default_protocol_versions()?
        .with_no_client_auth()
        .with_single_cert(certificates, PrivateKeyDer::from_pem_file(private_key)?)?;
        // The ancillary HTTP listener stays on loopback; the selected port serves TLS.
        relay.http_bind_addr = "127.0.0.1:0".parse()?;
        relay.tls = Some(TlsConfig::new(
            bind_address,
            CertConfig::Manual { server_config },
        ));
    }
    config.relay = Some(relay);
    if let Some(qad_bind) = qad_bind {
        config.quic = Some(QuicConfig::new(qad_bind.parse::<SocketAddr>()?));
    }
    let server = Server::spawn(config).await?;
    println!(
        "Private test relay listening on {bind_address}; QAD={:?}",
        server.quic_addr()
    );
    tokio::signal::ctrl_c().await?;
    server.shutdown().await?;
    Ok(())
}
