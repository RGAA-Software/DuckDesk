//! Low-rate cross-host connectivity evidence; never changes host NIC settings.
use std::{path::PathBuf, time::Duration};

use anyhow::{Context, Result, ensure};
use bytes::Bytes;
use px_transport::{Connection, EndpointAddr, EndpointConfig, TransportEndpoint, send_media};
use serde_json::json;
use tokio::{task::JoinSet, time::Instant};

struct Options {
    role: String,
    config_path: PathBuf,
    address_path: PathBuf,
    seconds: u64,
}

impl Options {
    fn parse() -> Result<Self> {
        let arguments: Vec<String> = std::env::args().skip(1).collect();
        ensure!(
            arguments.len() == 4,
            "usage: px_nat_probe serve|connect <config.json> <address.json> <seconds>"
        );
        ensure!(
            matches!(arguments[0].as_str(), "serve" | "connect"),
            "invalid probe role"
        );
        let seconds = arguments[3]
            .parse::<u64>()
            .context("parse probe duration")?;
        ensure!(
            (5..=120).contains(&seconds),
            "duration must be 5..120 seconds"
        );
        Ok(Self {
            role: arguments[0].clone(),
            config_path: PathBuf::from(&arguments[1]),
            address_path: PathBuf::from(&arguments[2]),
            seconds,
        })
    }
}

fn report_paths(connection: &Connection, phase: &str, elapsed_ms: u128) {
    let paths = connection.paths();
    let selected = paths.iter().find(|path| path.is_selected());
    println!(
        "{}",
        json!({
            "event": "nat_probe.path", "phase": phase, "elapsed_ms": elapsed_ms,
            "peer_id": connection.remote_id().to_string(),
            "selected": selected.as_ref().map(|path| path.remote_addr()),
            "selected_is_ip": selected.as_ref().map(|path| path.is_ip()),
            "paths": format!("{paths:?}")
        })
    );
}

async fn echo(connection: Connection) -> Result<()> {
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

async fn sample(connection: &Connection, duration: Duration) -> Result<()> {
    let started = Instant::now();
    let sample_deadline = started + duration;
    let reliable = async {
        let (mut sender, mut receiver) = connection.open_bi().await?;
        let mut verified_messages = 0u64;
        let mut maximum_round_trip_ms = 0u128;
        while Instant::now() < sample_deadline {
            let message = verified_messages.to_be_bytes();
            let sent_at = Instant::now();
            sender.write_all(&message).await?;
            let mut echoed = [0u8; 8];
            tokio::time::timeout(Duration::from_secs(20), receiver.read_exact(&mut echoed))
                .await
                .context("reliable stream did not recover")??;
            ensure!(echoed == message, "reliable stream changed byte ordering");
            verified_messages += 1;
            maximum_round_trip_ms = maximum_round_trip_ms.max(sent_at.elapsed().as_millis());
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
        sender.finish()?;
        Ok::<_, anyhow::Error>((verified_messages, maximum_round_trip_ms))
    };
    let datagrams = async {
        let mut sequence = 0u64;
        let mut verified_datagrams = 0u64;
        let mut late_datagrams = 0u64;
        let mut maximum_round_trip_us = 0u128;
        let mut last_received = started;
        let mut maximum_success_gap_ms = 0u128;
        let mut interval = tokio::time::interval(Duration::from_millis(50));
        interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        while Instant::now() < sample_deadline {
            interval.tick().await;
            let sent_at = Instant::now();
            let mut payload = vec![0x5a; 128];
            payload[..8].copy_from_slice(&sequence.to_be_bytes());
            send_media(connection, Bytes::from(payload.clone())).await?;
            let receive_deadline = Instant::now() + Duration::from_millis(500);
            loop {
                let Ok(received) =
                    tokio::time::timeout_at(receive_deadline, connection.read_datagram()).await
                else {
                    break;
                };
                let received = received?;
                ensure!(received.len() == payload.len(), "unexpected datagram size");
                let echoed_sequence = u64::from_be_bytes(received[..8].try_into()?);
                if echoed_sequence < sequence {
                    late_datagrams += 1;
                    continue;
                }
                ensure!(received.as_ref() == payload, "datagram payload changed");
                verified_datagrams += 1;
                maximum_round_trip_us = maximum_round_trip_us.max(sent_at.elapsed().as_micros());
                maximum_success_gap_ms =
                    maximum_success_gap_ms.max(last_received.elapsed().as_millis());
                last_received = Instant::now();
                break;
            }
            sequence += 1;
        }
        ensure!(verified_datagrams > 0, "no datagrams echoed");
        Ok::<_, anyhow::Error>((
            sequence,
            verified_datagrams,
            late_datagrams,
            maximum_round_trip_us,
            maximum_success_gap_ms,
        ))
    };
    let (reliable_result, datagram_result) = tokio::join!(reliable, datagrams);
    let (verified_messages, reliable_rtt_ms) = reliable_result?;
    let (sent, received, late, datagram_rtt_us, success_gap_ms) = datagram_result?;
    println!(
        "{}",
        json!({
            "event": "nat_probe.result", "reliable_messages_verified": verified_messages,
            "datagrams_sent": sent, "datagrams_verified": received, "late_datagrams": late,
            "maximum_reliable_round_trip_ms": reliable_rtt_ms,
            "maximum_datagram_round_trip_us": datagram_rtt_us,
            "maximum_datagram_success_gap_ms": success_gap_ms,
            "elapsed_ms": started.elapsed().as_millis(), "peer_id": connection.remote_id().to_string()
        })
    );
    Ok(())
}

#[tokio::main(worker_threads = 2)]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::EnvFilter::from_default_env())
        .with_ansi(false)
        .with_writer(std::io::stderr)
        .init();
    let options = Options::parse()?;
    let configuration: EndpointConfig =
        serde_json::from_slice(&std::fs::read(&options.config_path)?)?;
    let endpoint = TransportEndpoint::bind(configuration).await?;
    let outcome = async {
        endpoint.online(Duration::from_secs(20)).await?;
        // Give private QAD a bounded opportunity to publish the ephemeral mapping.
        tokio::time::sleep(Duration::from_secs(1)).await;
        println!(
            "{}",
            json!({"event":"nat_probe.endpoint", "address":endpoint.address()})
        );
        let connection = if options.role == "serve" {
            std::fs::write(
                &options.address_path,
                serde_json::to_vec_pretty(&endpoint.address())?,
            )?;
            let accepted = tokio::time::timeout(Duration::from_secs(90), endpoint.accept())
                .await
                .context("no peer connected before deadline")??;
            accepted
        } else {
            let address: EndpointAddr =
                serde_json::from_slice(&std::fs::read(&options.address_path)?)?;
            tokio::time::timeout(Duration::from_secs(30), endpoint.connect(address))
                .await
                .context("peer connection deadline")??
        };
        let started = Instant::now();
        report_paths(&connection, "connected", 0);
        let monitor = async {
            loop {
                tokio::time::sleep(Duration::from_secs(1)).await;
                report_paths(&connection, "sample", started.elapsed().as_millis());
            }
        };
        let transfer = async {
            if options.role == "serve" {
                tokio::time::timeout(
                    Duration::from_secs(options.seconds + 30),
                    echo(connection.clone()),
                )
                .await
                .context("server connection did not stop")??;
            } else {
                sample(&connection, Duration::from_secs(options.seconds)).await?;
                connection.close(0u32.into(), b"NAT probe complete");
            }
            Ok::<(), anyhow::Error>(())
        };
        let transfer_result = tokio::select! {
            result = transfer => result,
            () = monitor => unreachable!(),
        };
        report_paths(&connection, "finished", started.elapsed().as_millis());
        transfer_result
    }
    .await;
    endpoint.close().await;
    outcome
}
