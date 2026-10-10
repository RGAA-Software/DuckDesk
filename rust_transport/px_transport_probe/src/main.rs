use std::{path::PathBuf, time::Duration};

use anyhow::{Context, Result, ensure};
use bytes::Bytes;
use px_transport::{
    Connection, EndpointAddr, EndpointConfig, PrivateRelay, TransportEndpoint, send_media,
};
use tokio::{task::JoinSet, time::Instant};

mod capacity_probe;
mod recovery_probe;

const PROBE_DEADLINE: Duration = Duration::from_secs(30);

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .with_ansi(false)
        .with_writer(std::io::stderr)
        .init();
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    let operation = arguments.first().map(String::as_str).unwrap_or("self-test");
    let recovery_seconds = arguments
        .iter()
        .position(|argument| argument == "--recovery-seconds")
        .map(|position| {
            arguments
                .get(position + 1)
                .context("--recovery-seconds requires a duration")?
                .parse::<u64>()
                .context("invalid recovery duration")
        })
        .transpose()?
        .unwrap_or(20);
    ensure!(
        (5..=120).contains(&recovery_seconds),
        "recovery duration must be 5..120 seconds"
    );
    let recovery_time = Duration::from_secs(recovery_seconds);
    let mut config = if let Some(position) = arguments
        .iter()
        .position(|argument| argument == "--endpoint-config")
    {
        let configuration_path = arguments
            .get(position + 1)
            .context("--endpoint-config requires a JSON path")?;
        serde_json::from_slice::<EndpointConfig>(
            &std::fs::read(configuration_path).context("read endpoint configuration")?,
        )
        .context("parse endpoint configuration")?
    } else {
        EndpointConfig::default()
    };
    if let Some(relay_option) = arguments.iter().position(|argument| argument == "--relay") {
        let qad_port = arguments
            .iter()
            .position(|argument| argument == "--qad-port")
            .map(|position| {
                arguments
                    .get(position + 1)
                    .context("--qad-port requires a port")?
                    .parse::<u16>()
                    .context("invalid QAD port")
            })
            .transpose()?;
        config.relays.push(PrivateRelay {
            url: arguments
                .get(relay_option + 1)
                .context("--relay requires a URL")?
                .parse()?,
            qad_port,
        });
    }
    config.relay_only |= arguments.iter().any(|argument| argument == "--relay-only");
    if let Some(position) = arguments
        .iter()
        .position(|argument| argument == "--ca-root")
    {
        config.ca_certificates_pem.push(std::fs::read_to_string(
            arguments
                .get(position + 1)
                .context("--ca-root requires a PEM path")?,
        )?);
    }
    if operation == "allocation-self-test" {
        return tokio::time::timeout(Duration::from_secs(30), capacity_probe::allocate(config))
            .await
            .context("allocation probe deadline exceeded")?;
    }
    if operation == "capacity-self-test" {
        let release_denied = arguments
            .iter()
            .any(|argument| argument == "--release-denied-candidate");
        return tokio::time::timeout(
            Duration::from_secs(30),
            capacity_probe::run(config, release_denied),
        )
        .await
        .context("capacity probe deadline exceeded")?;
    }
    let endpoint = TransportEndpoint::bind(config.clone()).await?;
    if !config.relays.is_empty() {
        endpoint.online(PROBE_DEADLINE).await?;
    }
    let result = tokio::time::timeout(PROBE_DEADLINE + recovery_time, async {
        match operation {
            "relay-failover-self-test" => relay_failover_probe(&endpoint, config.clone()).await?,
            "self-test" | "recovery-self-test" => {
                let peer_endpoint = TransportEndpoint::bind(config.clone()).await?;
                let target = endpoint.address();
                let (accepted, connected) =
                    tokio::join!(endpoint.accept(), peer_endpoint.connect(target));
                let server_connection = accepted?;
                let client_connection = connected?;
                let client_probe = async {
                    if operation == "recovery-self-test" {
                        recovery_probe::run(client_connection, recovery_time).await
                    } else {
                        probe_connection(client_connection).await
                    }
                };
                let diagnostics = async {
                    let started = Instant::now();
                    let mut previous_status = String::new();
                    loop {
                        let status = format!("server={:?} client={:?} server_addr={:?} client_addr={:?}",
                            endpoint.relay_status(), peer_endpoint.relay_status(), endpoint.address(), peer_endpoint.address());
                        if status != previous_status {
                            println!("RELAY_STATUS elapsed_ms={} {status}", started.elapsed().as_millis());
                            previous_status = status;
                        }
                        tokio::time::sleep(Duration::from_millis(200)).await;
                    }
                };
                let (server_result, client_result) = tokio::select! {
                    results = async { tokio::join!(echo_connection(server_connection), client_probe) } => results,
                    () = diagnostics, if operation == "recovery-self-test" => unreachable!(),
                };
                peer_endpoint.close().await;
                client_result?;
                server_result?;
            }
            "serve" => {
                let address_path =
                    PathBuf::from(arguments.get(1).context("serve requires an address file")?);
                std::fs::write(
                    &address_path,
                    serde_json::to_vec_pretty(&endpoint.address())?,
                )?;
                println!(
                    "Listening; endpoint address saved to {}",
                    address_path.display()
                );
                echo_connection(endpoint.accept().await?).await?;
            }
            "connect" => {
                let address_path = arguments
                    .get(1)
                    .context("connect requires an address file")?;
                let address: EndpointAddr = serde_json::from_slice(&std::fs::read(address_path)?)?;
                probe_connection(endpoint.connect(address).await?).await?;
            }
            _ => {
                anyhow::bail!("expected self-test, recovery-self-test, relay-failover-self-test, serve <address-file>, or connect <address-file>")
            }
        }
        Ok::<(), anyhow::Error>(())
    })
    .await
    .context("transport probe deadline exceeded")?;
    endpoint.close().await;
    result
}

async fn relay_failover_probe(endpoint: &TransportEndpoint, config: EndpointConfig) -> Result<()> {
    let peer_endpoint = TransportEndpoint::bind(config).await?;
    peer_endpoint.online(PROBE_DEADLINE).await?;
    let original_address = endpoint.address();
    let original_client_id = peer_endpoint.address().id;
    let (accepted, connected) = tokio::join!(
        endpoint.accept(),
        peer_endpoint.connect(original_address.clone())
    );
    let server_connection = accepted?;
    let client_connection = connected?;
    println!("RECOVERY_READY: paths={:?}", client_connection.paths());
    let started = Instant::now();
    let mut saw_disconnect = false;
    let mut disconnected_at = None;
    let recovery = async {
        loop {
            let statuses = endpoint.relay_status();
            saw_disconnect |= statuses.iter().any(|status| !status.is_connected());
            if saw_disconnect {
                disconnected_at.get_or_insert_with(Instant::now);
            }
            let address_changed = endpoint.address() != original_address;
            if saw_disconnect
                && address_changed
                && statuses.iter().any(|status| status.is_connected())
            {
                break;
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
        let reselection_ms = started.elapsed().as_millis();
        let disconnected_reselection_ms = disconnected_at
            .map(|observed: Instant| observed.elapsed().as_millis())
            .unwrap_or_default();
        ensure!(
            endpoint.address().id == original_address.id,
            "server identity changed"
        );
        ensure!(
            peer_endpoint.address().id == original_client_id,
            "client identity changed"
        );
        client_connection.close(0u32.into(), b"fresh address reconnect test");
        server_connection.close(0u32.into(), b"fresh address reconnect test");
        let (accepted, connected) =
            tokio::join!(endpoint.accept(), peer_endpoint.connect(endpoint.address()));
        let server_connection = accepted?;
        let client_connection = connected?;
        println!(
            "RELAY_FAILOVER: reselection_ms={reselection_ms} disconnected_reselection_ms={disconnected_reselection_ms} same_endpoint_ids=true paths={:?}",
            client_connection.paths()
        );
        let (server_result, client_result) = tokio::join!(
            echo_connection(server_connection),
            probe_connection(client_connection)
        );
        client_result?;
        server_result?;
        Ok::<(), anyhow::Error>(())
    };
    let result = tokio::time::timeout(Duration::from_secs(40), recovery)
        .await
        .context("relay reselection/reconnect deadline");
    peer_endpoint.close().await;
    result?
}

async fn echo_connection(connection: Connection) -> Result<()> {
    let mut workers = JoinSet::new();
    let datagram_connection = connection.clone();
    workers.spawn(async move {
        loop {
            let payload = datagram_connection.read_datagram().await?;
            send_media(&datagram_connection, payload).await?;
        }
        #[allow(unreachable_code)]
        Ok::<(), anyhow::Error>(())
    });
    loop {
        tokio::select! {
            incoming = connection.accept_bi() => {
                let Ok((mut sender, mut receiver)) = incoming else { break };
                workers.spawn(async move {
                    tokio::io::copy(&mut receiver, &mut sender).await?;
                    sender.finish()?;
                    Ok::<(), anyhow::Error>(())
                });
            }
            completed = workers.join_next(), if !workers.is_empty() => {
                if let Some(completed) = completed {
                    if let Err(failure) = completed? {
                        if connection.close_reason().is_none() { return Err(failure); }
                        break;
                    }
                }
            }
        }
    }
    workers.abort_all();
    while workers.join_next().await.is_some() {}
    Ok(())
}

async fn probe_connection(connection: Connection) -> Result<()> {
    let started = Instant::now();
    let reliable_connection = connection.clone();
    let reliable = async move {
        let mut workers = JoinSet::new();
        for stream_index in 0..4u8 {
            let stream_connection = reliable_connection.clone();
            workers.spawn(async move {
                let (mut sender, mut receiver) = stream_connection.open_bi().await?;
                sender.set_priority(if stream_index == 0 { 100 } else { -10 })?;
                let payload = vec![
                    stream_index;
                    if stream_index == 0 {
                        64
                    } else {
                        2 * 1024 * 1024
                    }
                ];
                let receive_limit = payload.len();
                let send = async {
                    sender.write_all(&payload).await?;
                    sender.finish()?;
                    Ok::<(), anyhow::Error>(())
                };
                let receive = receiver.read_to_end(receive_limit);
                let (sent, echoed) = tokio::join!(send, receive);
                sent?;
                ensure!(echoed? == payload, "reliable stream checksum mismatch");
                Ok::<usize, anyhow::Error>(payload.len())
            });
        }
        let mut reliable_bytes = 0;
        while let Some(completed) = workers.join_next().await {
            reliable_bytes += completed??;
        }
        Ok::<usize, anyhow::Error>(reliable_bytes)
    };
    let datagrams = async {
        let maximum_size = connection
            .max_datagram_size()
            .context("missing datagram support")?;
        let send = async {
            let mut interval = tokio::time::interval(Duration::from_micros(16667));
            interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
            for sequence in 0..120u32 {
                interval.tick().await;
                let mut payload = vec![0; maximum_size.min(1000)];
                payload[..4].copy_from_slice(&sequence.to_be_bytes());
                payload[4..12]
                    .copy_from_slice(&(started.elapsed().as_micros() as u64).to_be_bytes());
                send_media(&connection, Bytes::from(payload)).await?;
            }
            Ok::<(), anyhow::Error>(())
        };
        let receive = async {
            let deadline = Instant::now() + Duration::from_secs(5);
            let mut received_sequences = [false; 120];
            let mut echoed_datagrams = 0;
            let mut maximum_round_trip_us = 0;
            while echoed_datagrams < 120 {
                let Ok(received) =
                    tokio::time::timeout_at(deadline, connection.read_datagram()).await
                else {
                    break;
                };
                let payload = received?;
                ensure!(payload.len() >= 12, "truncated probe datagram");
                let sequence = u32::from_be_bytes(payload[..4].try_into()?) as usize;
                ensure!(
                    sequence < received_sequences.len(),
                    "unexpected probe sequence"
                );
                let sent_at_us = u64::from_be_bytes(payload[4..12].try_into()?);
                if !received_sequences[sequence] {
                    received_sequences[sequence] = true;
                    echoed_datagrams += 1;
                    maximum_round_trip_us = maximum_round_trip_us.max(
                        started
                            .elapsed()
                            .as_micros()
                            .saturating_sub(sent_at_us.into()),
                    );
                }
            }
            Ok::<_, anyhow::Error>((echoed_datagrams, maximum_round_trip_us))
        };
        let (sent, received) = tokio::join!(send, receive);
        sent?;
        let (echoed_datagrams, maximum_round_trip_us) = received?;
        ensure!(echoed_datagrams > 0, "no echoed datagrams");
        Ok::<_, anyhow::Error>((echoed_datagrams, maximum_round_trip_us, maximum_size))
    };
    let (reliable_result, datagram_result) = tokio::join!(reliable, datagrams);
    let reliable_bytes = reliable_result?;
    let (echoed_datagrams, maximum_round_trip_us, maximum_size) = datagram_result?;
    println!(
        "{}",
        serde_json::json!({
            "reliable_bytes_verified": reliable_bytes,
            "datagrams_sent": 120,
            "datagrams_echoed": echoed_datagrams,
            "maximum_datagram_round_trip_us": maximum_round_trip_us,
            "maximum_datagram_size": maximum_size,
            "elapsed_ms": started.elapsed().as_millis(),
            "paths": format!("{:?}", connection.paths()),
        })
    );
    connection.close(0u32.into(), b"probe complete");
    Ok(())
}
