//! Bounded single-peer Relay throughput measurement, without concurrent media/file load.
use std::{collections::HashSet, path::PathBuf, time::Duration};

use anyhow::{Context, Result, ensure};
use bytes::Bytes;
use px_transport::{Connection, EndpointAddr, EndpointConfig, TransportEndpoint, send_media};
use serde_json::json;
use tokio::time::Instant;

const PAYLOAD_SIZE: usize = 1100;
const DATAGRAM_SECONDS: u64 = 10;

fn report_path(connection: &Connection) -> Result<()> {
    let paths = connection.paths();
    let selected = paths.iter().find(|path| path.is_selected());
    ensure!(
        selected.as_ref().is_some_and(|path| !path.is_ip()),
        "measurement requires an actual Relay path"
    );
    println!(
        "{}",
        json!({"event":"relay_speed.path", "peer_id":connection.remote_id().to_string(),
        "selected":selected.as_ref().map(|path| path.remote_addr())})
    );
    Ok(())
}

async fn serve(connection: &Connection, seconds: u64, ceiling_mbps: u64) -> Result<()> {
    let (mut control_send, mut control_receive) = connection.accept_bi().await?;
    let mut request = [0u8; 1];
    control_receive.read_exact(&mut request).await?;
    ensure!(request == [1], "invalid reliable phase request");
    let mut payload_send = connection.open_uni().await?;
    let payload = vec![0xA5; 16 * 1024];
    let started = Instant::now();
    let mut sent_bytes = 0u64;
    while started.elapsed() < Duration::from_secs(seconds) {
        payload_send.write_all(&payload).await?;
        sent_bytes += payload.len() as u64;
        let deadline = started
            + Duration::from_secs_f64(sent_bytes as f64 * 8.0 / (ceiling_mbps * 1_000_000) as f64);
        tokio::time::sleep_until(deadline).await;
    }
    payload_send.finish()?;
    println!(
        "{}",
        json!({"event":"relay_speed.reliable_sent", "bytes":sent_bytes,
        "elapsed_ms":started.elapsed().as_millis(), "offered_ceiling_mbps":ceiling_mbps})
    );
    // Receiver confirms EOF before the first datagram phase. No bulk stream competes with it.
    for rate_mbps in [1u64, 4, 10] {
        control_receive.read_exact(&mut request).await?;
        ensure!(
            request == [rate_mbps as u8],
            "invalid datagram phase request"
        );
        let started = Instant::now();
        let mut attempted = 0u64;
        let mut sent = 0u64;
        let mut queue_failures = 0u64;
        let mut pacing_timer = tokio::time::interval(Duration::from_millis(10));
        pacing_timer.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        let mut last_pacing_tick = started;
        let mut packet_credit = 0.0f64;
        let mut maximum_pacing_gap_ms = 0u128;
        while started.elapsed() < Duration::from_secs(DATAGRAM_SECONDS) {
            pacing_timer.tick().await;
            let pacing_time = Instant::now();
            let elapsed_seconds = pacing_time.duration_since(last_pacing_tick).as_secs_f64();
            maximum_pacing_gap_ms =
                maximum_pacing_gap_ms.max(pacing_time.duration_since(last_pacing_tick).as_millis());
            last_pacing_tick = pacing_time;
            // Account for actual scheduler time, with a bounded 32-packet burst.
            // A timer wake-up per sub-millisecond packet cannot measure 10Mbps on Windows.
            packet_credit = (packet_credit
                + elapsed_seconds * (rate_mbps * 1_000_000) as f64 / (PAYLOAD_SIZE as f64 * 8.0))
                .min(32.0);
            while packet_credit >= 1.0 && started.elapsed() < Duration::from_secs(DATAGRAM_SECONDS)
            {
                let mut payload = vec![0x5A; PAYLOAD_SIZE];
                payload[..8].copy_from_slice(&attempted.to_le_bytes());
                payload[8] = rate_mbps as u8;
                attempted += 1;
                packet_credit -= 1.0;
                if send_media(connection, Bytes::from(payload)).await.is_ok() {
                    sent += 1;
                } else {
                    queue_failures += 1;
                }
            }
        }
        let mut summary = Vec::with_capacity(24);
        summary.extend_from_slice(&sent.to_le_bytes());
        summary.extend_from_slice(&queue_failures.to_le_bytes());
        summary.extend_from_slice(&(started.elapsed().as_micros() as u64).to_le_bytes());
        control_send.write_all(&summary).await?;
        println!(
            "{}",
            json!({"event":"relay_speed.datagram_sent", "target_mbps":rate_mbps,
            "sent":sent,"queue_failures":queue_failures,"elapsed_ms":started.elapsed().as_millis(),
            "maximum_pacing_tick_gap_ms":maximum_pacing_gap_ms})
        );
    }
    control_receive.read_exact(&mut request).await?;
    ensure!(request == [0], "receiver did not finish");
    control_send.write_all(&[0]).await?;
    control_send.finish()?;
    let _ = tokio::time::timeout(Duration::from_secs(3), connection.closed()).await;
    Ok(())
}

async fn receive(connection: &Connection, ceiling_mbps: u64) -> Result<()> {
    let (mut control_send, mut control_receive) = connection.open_bi().await?;
    control_send.write_all(&[1]).await?;
    let mut payload_receive = connection.accept_uni().await?;
    let started = Instant::now();
    let mut received_bytes = 0u64;
    let mut window_bytes = 0u64;
    let mut window_started = started;
    let mut payload = vec![0u8; 64 * 1024];
    let mut maximum_read_gap_ms = 0u128;
    let mut last_received = started;
    while let Some(length) = payload_receive.read(&mut payload).await? {
        ensure!(
            payload[..length].iter().all(|value| *value == 0xA5),
            "reliable payload changed"
        );
        maximum_read_gap_ms = maximum_read_gap_ms.max(last_received.elapsed().as_millis());
        last_received = Instant::now();
        received_bytes += length as u64;
        window_bytes += length as u64;
        if window_started.elapsed() >= Duration::from_secs(1) {
            println!(
                "{}",
                json!({"event":"relay_speed.reliable_window",
                "mbps":window_bytes as f64 * 8.0 / window_started.elapsed().as_secs_f64() / 1_000_000.0})
            );
            window_bytes = 0;
            window_started = Instant::now();
            report_path(connection)?;
        }
    }
    let elapsed_seconds = started.elapsed().as_secs_f64();
    println!(
        "{}",
        json!({"event":"relay_speed.reliable_received", "bytes_verified":received_bytes,
        "elapsed_seconds":elapsed_seconds,"effective_mbps":received_bytes as f64 * 8.0 / elapsed_seconds / 1_000_000.0,
        "offered_ceiling_mbps":ceiling_mbps,"maximum_read_gap_ms":maximum_read_gap_ms})
    );
    ensure!(received_bytes > 0, "empty reliable transfer");
    for rate_mbps in [1u64, 4, 10] {
        let mut sequences = HashSet::new();
        let mut duplicates = 0u64;
        let mut old_phase_packets = 0u64;
        let mut maximum_gap_ms = 0u128;
        let mut last_received = Instant::now();
        control_send.write_all(&[rate_mbps as u8]).await?;
        let mut summary = [0u8; 24];
        {
            let finish_phase = control_receive.read_exact(&mut summary);
            tokio::pin!(finish_phase);
            loop {
                tokio::select! {
                    completed = &mut finish_phase => { completed?; break; }
                    received = connection.read_datagram() => {
                        let received = received?;
                        ensure!(received.len() == PAYLOAD_SIZE && received[9..].iter().all(|value| *value == 0x5A), "datagram payload changed");
                        if received[8] != rate_mbps as u8 { old_phase_packets += 1; continue; }
                        maximum_gap_ms = maximum_gap_ms.max(last_received.elapsed().as_millis());
                        last_received = Instant::now();
                        let sequence = u64::from_le_bytes(received[..8].try_into()?);
                        if !sequences.insert(sequence) { duplicates += 1; }
                    }
                }
            }
        }
        // Reliable completion can overtake the tail of unreliable data; allow a bounded drain.
        let drain_deadline = Instant::now() + Duration::from_millis(500);
        while let Ok(received) =
            tokio::time::timeout_at(drain_deadline, connection.read_datagram()).await
        {
            let received = received?;
            ensure!(
                received.len() == PAYLOAD_SIZE && received[9..].iter().all(|value| *value == 0x5A),
                "tail datagram payload changed"
            );
            if received[8] == rate_mbps as u8 {
                if !sequences.insert(u64::from_le_bytes(received[..8].try_into()?)) {
                    duplicates += 1;
                }
            } else {
                old_phase_packets += 1;
            }
        }
        let sent = u64::from_le_bytes(summary[..8].try_into()?);
        let queue_failures = u64::from_le_bytes(summary[8..16].try_into()?);
        let send_seconds = u64::from_le_bytes(summary[16..].try_into()?) as f64 / 1_000_000.0;
        ensure!(
            sent >= sequences.len() as u64,
            "received more unique packets than sent"
        );
        println!(
            "{}",
            json!({"event":"relay_speed.datagram_received", "target_mbps":rate_mbps,
            "sent":sent,"unique_received":sequences.len(),"lost_after_enqueue":sent-sequences.len() as u64,
            "queue_failures":queue_failures,"duplicates":duplicates,"old_phase_packets":old_phase_packets,
            "send_seconds":send_seconds,"effective_mbps":sequences.len() as f64 * PAYLOAD_SIZE as f64 * 8.0 / send_seconds / 1_000_000.0,
            "maximum_arrival_gap_ms":maximum_gap_ms})
        );
        report_path(connection)?;
    }
    control_send.write_all(&[0]).await?;
    let mut acknowledged = [1u8; 1];
    control_receive.read_exact(&mut acknowledged).await?;
    ensure!(acknowledged == [0], "completion acknowledgment changed");
    Ok(())
}

#[tokio::main(worker_threads = 2)]
async fn main() -> Result<()> {
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    ensure!(
        arguments.len() == 5,
        "usage: px_relay_speed serve|connect config.json address.json seconds ceiling_mbps"
    );
    let role = &arguments[0];
    ensure!(matches!(role.as_str(), "serve" | "connect"), "invalid role");
    let seconds = arguments[3].parse::<u64>()?;
    let ceiling_mbps = arguments[4].parse::<u64>()?;
    ensure!(
        (5..=30).contains(&seconds) && (1..=50).contains(&ceiling_mbps),
        "duration/ceiling outside bounded test limits"
    );
    let configuration: EndpointConfig = serde_json::from_slice(&std::fs::read(&arguments[1])?)?;
    ensure!(
        configuration.relay_only && configuration.relays.len() == 1,
        "speed probe requires one explicit Relay-only candidate"
    );
    let address_path = PathBuf::from(&arguments[2]);
    let endpoint = TransportEndpoint::bind(configuration).await?;
    let outcome = async {
        endpoint.online(Duration::from_secs(20)).await?;
        let connection = if role == "serve" {
            std::fs::write(
                address_path,
                serde_json::to_vec_pretty(&endpoint.address())?,
            )?;
            println!(
                "{}",
                json!({"event":"relay_speed.endpoint", "address":endpoint.address()})
            );
            tokio::time::timeout(Duration::from_secs(90), endpoint.accept())
                .await
                .context("peer did not connect")??
        } else {
            let address: EndpointAddr = serde_json::from_slice(&std::fs::read(address_path)?)?;
            tokio::time::timeout(Duration::from_secs(30), endpoint.connect(address))
                .await
                .context("connection deadline")??
        };
        report_path(&connection)?;
        let transfer_result = tokio::time::timeout(Duration::from_secs(seconds + 70), async {
            if role == "serve" {
                serve(&connection, seconds, ceiling_mbps).await
            } else {
                receive(&connection, ceiling_mbps).await
            }
        })
        .await
        .context("bounded throughput deadline")?;
        report_path(&connection)?;
        connection.close(0u32.into(), b"Relay throughput measurement complete");
        transfer_result
    }
    .await;
    endpoint.close().await;
    outcome
}
