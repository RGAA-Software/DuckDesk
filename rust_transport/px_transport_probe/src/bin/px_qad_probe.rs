use std::{net::SocketAddr, sync::Arc, time::Duration};

use anyhow::{Context, Result, ensure};
use futures_lite::StreamExt;
use iroh_relay::quic::{QUIC_ADDR_DISC_CLOSE_CODE, QUIC_ADDR_DISC_CLOSE_REASON, QuicClient};
use rustls::pki_types::{CertificateDer, pem::PemObject};

#[tokio::main]
async fn main() -> Result<()> {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    ensure!(
        arguments.len() == 3,
        "usage: px_qad_probe <server-ip:udp-port> <TLS-server-name> <CA-file>"
    );
    let server_address: SocketAddr = arguments[0].parse().context("parse QAD endpoint")?;
    let mut trust_roots = rustls::RootCertStore::empty();
    for certificate in CertificateDer::pem_file_iter(&arguments[2]).context("open QAD CA")? {
        trust_roots.add(certificate.context("parse QAD CA")?)?;
    }
    ensure!(
        !trust_roots.is_empty(),
        "QAD CA file contains no certificates"
    );
    let tls_config = rustls::ClientConfig::builder_with_provider(Arc::new(
        rustls::crypto::ring::default_provider(),
    ))
    .with_safe_default_protocol_versions()?
    .with_root_certificates(trust_roots)
    .with_no_client_auth();
    let bind_address = if server_address.is_ipv4() {
        "0.0.0.0:0"
    } else {
        "[::]:0"
    };
    let endpoint = noq::Endpoint::client(bind_address.parse()?)?;
    let qad_client = QuicClient::new(endpoint.clone(), tls_config);
    let started = std::time::Instant::now();
    let probe_result = tokio::time::timeout(Duration::from_secs(10), async {
        let connection = qad_client
            .create_conn(server_address, &arguments[1])
            .await
            .context("connect to QAD with certificate verification")?;
        let observed_address = connection
            .observed_external_addr()
            .next()
            .await
            .context("QAD closed without an observed address")?;
        connection.close(QUIC_ADDR_DISC_CLOSE_CODE, QUIC_ADDR_DISC_CLOSE_REASON);
        Ok::<_, anyhow::Error>(observed_address)
    })
    .await;
    endpoint.close(QUIC_ADDR_DISC_CLOSE_CODE, QUIC_ADDR_DISC_CLOSE_REASON);
    let _ = tokio::time::timeout(Duration::from_secs(2), endpoint.wait_idle()).await;
    let observed_address = probe_result.context("QAD probe timed out")??;
    println!(
        "{}",
        serde_json::json!({
            "server": server_address.to_string(),
            "observed_address": observed_address.to_string(),
            "elapsed_ms": started.elapsed().as_millis(),
            "tls_verified": true,
        })
    );
    Ok(())
}
