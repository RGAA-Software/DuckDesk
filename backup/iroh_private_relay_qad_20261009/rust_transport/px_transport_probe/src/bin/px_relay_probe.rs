//! Isolated private-relay probe, not the Console-managed product service.
use std::net::SocketAddr;

use anyhow::{Context, Result};
use iroh_relay::server::{RelayConfig, Server, ServerConfig};

#[tokio::main]
async fn main() -> Result<()> {
    let bind_address: SocketAddr = std::env::args()
        .nth(1)
        .context("usage: px_relay_probe <bind-address:port>")?
        .parse()?;
    let mut config = ServerConfig::default();
    config.relay = Some(RelayConfig::new(bind_address));
    let server = Server::spawn(config).await?;
    println!("Private test relay listening on {bind_address}");
    tokio::signal::ctrl_c().await?;
    server.shutdown().await?;
    Ok(())
}
