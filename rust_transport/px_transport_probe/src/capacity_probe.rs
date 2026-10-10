//! Exercise the product Relay's connection cap with real TLS endpoint sockets.
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use px_transport::{EndpointConfig, TransportEndpoint};
use tokio::time::Instant;

/// Allocate fresh endpoints from the same candidate pool, without pinning homes.
pub(crate) async fn allocate(config: EndpointConfig) -> Result<()> {
    ensure!(
        config.relay_only && config.relays.len() == 2,
        "allocation probe requires two Relay-only candidates"
    );
    let mut endpoints = Vec::new();
    let mut homes = Vec::new();
    for endpoint_index in 0..4 {
        let started = Instant::now();
        let endpoint = TransportEndpoint::bind(config.clone()).await?;
        endpoint.online(Duration::from_secs(5)).await?;
        let home = endpoint
            .relay_status()
            .into_iter()
            .find(|status| status.is_connected())
            .context("new endpoint has no connected home")?
            .url()
            .clone();
        println!(
            "ENDPOINT_ALLOCATED {}",
            serde_json::json!({"endpoint_index": endpoint_index,
            "home": home, "online_ms": started.elapsed().as_millis()})
        );
        homes.push(home);
        endpoints.push(endpoint);
    }
    for candidate in &config.relays {
        ensure!(
            homes.iter().filter(|home| **home == candidate.url).count() == 2,
            "fresh endpoints did not fill both Relay capacities"
        );
    }
    for (endpoint, home) in endpoints.iter().zip(&homes) {
        ensure!(
            endpoint
                .relay_status()
                .iter()
                .any(|status| status.is_connected() && status.url() == home),
            "an existing healthy endpoint moved while new endpoints were allocated"
        );
    }
    // Exercise the two admitted sockets on one home. A peer on another home
    // may need an additional Relay socket; a fully occupied cap cannot promise it.
    let peer_index = homes
        .iter()
        .enumerate()
        .skip(1)
        .find(|(_, home)| **home == homes[0])
        .map(|(index, _)| index)
        .context("missing admitted peer on the first home")?;
    let (accepted, connected) = tokio::join!(
        endpoints[0].accept(),
        endpoints[peer_index].connect(endpoints[0].address())
    );
    let (echoed, transferred) = tokio::join!(
        super::echo_connection(accepted?),
        super::probe_connection(connected?)
    );
    echoed?;
    transferred?;
    for endpoint in endpoints {
        endpoint.close().await;
    }
    println!(
        "ALLOCATION_VERIFIED {}",
        serde_json::json!({"endpoints": 4, "capacity_per_relay": 2,
        "home_counts": [2, 2], "healthy_homes_retained": true, "traffic_scope": "same-home admitted pair"})
    );
    Ok(())
}

pub(crate) async fn run(config: EndpointConfig, release_denied: bool) -> Result<()> {
    ensure!(
        config.relay_only && config.relays.len() == 2,
        "capacity probe requires two private Relay-only candidates"
    );
    let mut holders = Vec::new();
    for candidate in &config.relays {
        let mut exclusive = config.clone();
        exclusive.relays = vec![candidate.clone()];
        for _connection_index in 0..2 {
            let endpoint = TransportEndpoint::bind(exclusive.clone()).await?;
            endpoint.online(Duration::from_secs(5)).await?;
            holders.push(endpoint);
        }
    }
    let waiting = TransportEndpoint::bind(config.clone()).await?;
    let denied_url = tokio::time::timeout(Duration::from_secs(5), async {
        loop {
            if let Some(status) = waiting
                .relay_status()
                .into_iter()
                .find(|status| status.auth_denied_reason().is_some())
            {
                break status.url().clone();
            }
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
    })
    .await
    .context("full Relays did not reject a new endpoint")?;
    println!("CAPACITY_DENIED {denied_url}");
    let free_index = config
        .relays
        .iter()
        .position(|candidate| (candidate.url == denied_url) == release_denied)
        .context("missing alternate Relay")?;
    if release_denied {
        // Let both candidates reject the peer before releasing the initially
        // rejected one; recovery must revisit a temporarily suppressed Relay.
        tokio::time::sleep(Duration::from_secs(1)).await;
    }
    for holder in &holders[free_index * 2..free_index * 2 + 2] {
        holder.close().await;
    }
    let released_at = Instant::now();
    waiting
        .online(Duration::from_secs(5))
        .await
        .context("did not select the alternate Relay after capacity was released")?;
    let recovery_ms = released_at.elapsed().as_millis();
    ensure!(
        waiting
            .relay_status()
            .iter()
            .any(|status| status.is_connected() && status.url() == &config.relays[free_index].url),
        "connected to a Relay whose capacity is still full"
    );
    let mut available = config.clone();
    available.relays = vec![config.relays[free_index].clone()];
    let peer = TransportEndpoint::bind(available).await?;
    peer.online(Duration::from_secs(5)).await?;
    let (accepted, connected) = tokio::join!(waiting.accept(), peer.connect(waiting.address()));
    let (echoed, transferred) = tokio::join!(
        super::echo_connection(accepted?),
        super::probe_connection(connected?)
    );
    echoed?;
    transferred?;
    peer.close().await;
    waiting.close().await;
    for holder in holders {
        holder.close().await;
    }
    for candidate in &config.relays {
        let mut exclusive = config.clone();
        exclusive.relays = vec![candidate.clone()];
        let restored = TransportEndpoint::bind(exclusive).await?;
        restored.online(Duration::from_secs(5)).await?;
        restored.close().await;
    }
    println!(
        "CAPACITY_RECOVERED {}",
        serde_json::json!({"capacity_per_relay": 2, "recovery_ms": recovery_ms,
            "released_relay": config.relays[free_index].url, "release_denied": release_denied, "both_relays_reusable": true})
    );
    Ok(())
}
