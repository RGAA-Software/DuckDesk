//! Small held-connection checks driven by an isolated Console/Service harness.
use std::time::Duration;

use anyhow::{ensure, Context, Result};
use bytes::Bytes;
use px_transport::{send_media, Connection, EndpointConfig, TransportEndpoint};
use serde_json::{json, Value};
use tokio::io::{AsyncBufReadExt, BufReader};

const WAIT: Duration = Duration::from_secs(8);

async fn check_messages(source: &Connection, peer: &Connection, sequence: u32) -> Result<()> {
    let payload = sequence.to_be_bytes();
    send_media(source, Bytes::copy_from_slice(&payload)).await?;
    ensure!(
        tokio::time::timeout(WAIT, peer.read_datagram())
            .await??
            .as_ref()
            == payload
    );
    send_media(peer, Bytes::copy_from_slice(&payload)).await?;
    ensure!(
        tokio::time::timeout(WAIT, source.read_datagram())
            .await??
            .as_ref()
            == payload
    );
    Ok(())
}

async fn check_new_pair(endpoints: &[TransportEndpoint]) -> Result<()> {
    let (accepted, connected) = tokio::join!(
        endpoints[0].accept(),
        endpoints[1].connect(endpoints[0].address())
    );
    let source = accepted?;
    let peer = connected?;
    let (mut peer_sender, mut peer_receiver) = peer.open_bi().await?;
    peer_sender.write_all(&[0; 4]).await?;
    let (mut source_sender, mut source_receiver) =
        tokio::time::timeout(WAIT, source.accept_bi()).await??;
    let mut received = [0; 4];
    source_receiver.read_exact(&mut received).await?;
    for sequence in 1_u32..=4 {
        let expected = sequence.to_be_bytes();
        peer_sender.write_all(&expected).await?;
        source_receiver.read_exact(&mut received).await?;
        ensure!(received == expected);
        source_sender.write_all(&received).await?;
        peer_receiver.read_exact(&mut received).await?;
        ensure!(received == expected);
        check_messages(&source, &peer, sequence).await?;
    }
    peer_sender.finish()?;
    source_sender.finish()?;
    source.close(0_u32.into(), b"new pair verified");
    peer.close(0_u32.into(), b"new pair verified");
    Ok(())
}

pub(crate) async fn run(configuration: EndpointConfig) -> Result<()> {
    ensure!(
        configuration.relay_only && configuration.relays.len() == 1,
        "retention probe requires one private Relay-only candidate"
    );
    let source = TransportEndpoint::bind(configuration.clone()).await?;
    let peer = TransportEndpoint::bind(configuration.clone()).await?;
    source.online(WAIT).await?;
    peer.online(WAIT).await?;
    let source_identity = source.address().id;
    let peer_identity = peer.address().id;
    let home = configuration.relays[0].url.clone();
    let (accepted, connected) = tokio::join!(source.accept(), peer.connect(source.address()));
    let source_connection = accepted?;
    let peer_connection = connected?;
    // Opening a QUIC stream becomes visible remotely only after its first write.
    let (mut peer_sender, mut peer_receiver) = peer_connection.open_bi().await?;
    peer_sender.write_all(&[0; 4]).await?;
    let (mut source_sender, mut source_receiver) =
        tokio::time::timeout(WAIT, source_connection.accept_bi()).await??;
    let mut payload = [0; 4];
    source_receiver.read_exact(&mut payload).await?;
    source_sender.write_all(&payload).await?;
    peer_receiver.read_exact(&mut payload).await?;
    let relay_descriptions: Vec<_> = configuration
        .relays
        .iter()
        .map(|relay| json!({"url": relay.url, "qad_port": relay.qad_port}))
        .collect();
    println!(
        "RETENTION_READY {}",
        json!({"endpoint_address": source.address(),
        "endpoint_configuration": {"relay_only": true, "relays": relay_descriptions,
            "ca_certificates_pem": configuration.ca_certificates_pem}})
    );
    let mut commands = BufReader::new(tokio::io::stdin()).lines();
    let mut sequence = 0_u32;
    let mut new_endpoints = Vec::new();
    while let Some(command) = commands.next_line().await? {
        let request: Value = serde_json::from_str(&command)?;
        let phase = request["phase"]
            .as_str()
            .context("retention phase missing")?;
        if phase == "new-endpoints" || phase == "allocate-restored" {
            ensure!(
                new_endpoints.is_empty(),
                "previous new endpoint allocation still held"
            );
            let mut available = configuration.clone();
            available.relays = serde_json::from_value(request["relays"].clone())?;
            let endpoint_count = if phase == "new-endpoints" { 2 } else { 4 };
            ensure!(
                available.relays.len() == endpoint_count / 2,
                "unexpected available Relay count"
            );
            let mut homes = Vec::new();
            for _endpoint_index in 0..endpoint_count {
                let endpoint = TransportEndpoint::bind(available.clone()).await?;
                endpoint.online(WAIT).await?;
                let selected = endpoint
                    .relay_status()
                    .into_iter()
                    .find(|status| status.is_connected())
                    .context("fresh endpoint has no connected home")?
                    .url()
                    .clone();
                homes.push(selected);
                new_endpoints.push(endpoint);
            }
            for relay in &available.relays {
                ensure!(
                    homes
                        .iter()
                        .filter(|selected| **selected == relay.url)
                        .count()
                        == 2,
                    "new endpoints did not use available capacity"
                );
            }
            if phase == "new-endpoints" {
                check_new_pair(&new_endpoints).await?;
                println!(
                    "DISTRIBUTION_READY {}",
                    json!({"homes":homes,"endpoint_count":2,
                    "reliable_round_trips":4,"datagrams_each_direction":4})
                );
            } else {
                println!(
                    "DISTRIBUTION_RESTORED {}",
                    json!({"homes":homes,"endpoint_count":4,"home_counts":[2,2]})
                );
            }
            continue;
        }
        if phase == "finish" {
            for endpoint in new_endpoints.drain(..) {
                endpoint.close().await;
            }
            println!("DISTRIBUTION_FINISHED");
            return Ok(());
        }
        if phase == "release-new" {
            for endpoint in new_endpoints.drain(..) {
                endpoint.close().await;
            }
            println!("DISTRIBUTION_RELEASED");
            continue;
        }
        if phase == "release" {
            peer_sender.finish()?;
            source_sender.finish()?;
            source_connection.close(0_u32.into(), b"retention phase finished");
            peer_connection.close(0_u32.into(), b"retention phase finished");
            peer.close().await;
            source.close().await;
            println!("RETENTION_RELEASED");
            continue;
        }
        if phase == "restore" {
            let restored = TransportEndpoint::bind(configuration.clone()).await?;
            restored.online(WAIT).await?;
            ensure!(
                restored.address().id != source_identity,
                "restored endpoint reused old identity"
            );
            restored.close().await;
            println!("RETENTION_RESTORED");
            return Ok(());
        }
        ensure!(
            matches!(phase, "full" | "draining"),
            "unknown retention phase"
        );
        let relays = serde_json::from_value(request["relays"].clone())?;
        source.update_relays(relays).await?;
        ensure!(source.address().id == source_identity && peer.address().id == peer_identity);
        ensure!(
            source
                .relay_status()
                .iter()
                .any(|status| status.is_connected() && status.url() == &home),
            "existing source lost its connected home"
        );
        for _exchange_index in 0..4 {
            sequence += 1;
            let expected = sequence.to_be_bytes();
            peer_sender.write_all(&expected).await?;
            source_receiver.read_exact(&mut payload).await?;
            ensure!(payload == expected);
            source_sender.write_all(&payload).await?;
            peer_receiver.read_exact(&mut payload).await?;
            ensure!(payload == expected);
            check_messages(&source_connection, &peer_connection, sequence).await?;
        }
        let rejected = TransportEndpoint::bind(configuration.clone()).await?;
        tokio::time::timeout(WAIT, async {
            loop {
                if rejected
                    .relay_status()
                    .iter()
                    .any(|status| status.auth_denied_reason().is_some())
                {
                    break;
                }
                tokio::time::sleep(Duration::from_millis(20)).await;
            }
        })
        .await
        .context("new endpoint was not denied")?;
        ensure!(!rejected
            .relay_status()
            .iter()
            .any(|status| status.is_connected()));
        rejected.close().await;
        println!(
            "RETENTION_PHASE {}",
            json!({"phase": phase, "home": home,
            "same_endpoint_identities": true, "same_quic_connections": true, "same_reliable_stream": true,
            "reliable_round_trips": 4, "datagrams_each_direction": 4, "new_endpoint_denied": true})
        );
    }
    anyhow::bail!("retention harness closed before release/restore")
}
