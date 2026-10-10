//! A held QUIC connection survives an externally interrupted private Relay.
//! The harness only restarts its own Relay process; it never resets a host network adapter.
use std::time::Duration;

use anyhow::{Context, Result, ensure};
use bytes::Bytes;
use px_transport::{Connection, send_media};
use tokio::time::Instant;

pub async fn run(connection: Connection, run_time: Duration) -> Result<()> {
    let started = Instant::now();
    let send_until = started + run_time;
    let receive_until = send_until + Duration::from_secs(2);
    let initial_paths = format!("{:?}", connection.paths());
    println!("RECOVERY_READY: paths={initial_paths}");

    let reliable = async {
        let (mut sender, mut receiver) = connection.open_bi().await?;
        sender.set_priority(100)?;
        let mut sequence = 0u64;
        let mut maximum_gap_ms = 0u128;
        let mut last_received = started;
        while Instant::now() < send_until {
            let payload = sequence.to_be_bytes();
            sender.write_all(&payload).await?;
            let mut echoed = [0u8; 8];
            tokio::time::timeout_at(receive_until, receiver.read_exact(&mut echoed))
                .await
                .context("reliable stream did not resume")??;
            ensure!(echoed == payload, "recovery stream changed byte ordering");
            let now = Instant::now();
            maximum_gap_ms = maximum_gap_ms.max(now.duration_since(last_received).as_millis());
            last_received = now;
            sequence += 1;
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
        sender.finish()?;
        ensure!(last_received >= send_until - Duration::from_secs(1));
        Ok::<_, anyhow::Error>((sequence, maximum_gap_ms))
    };
    let datagram_sender = async {
        let mut interval = tokio::time::interval(Duration::from_micros(16667));
        interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        let mut sequence = 0u32;
        while Instant::now() < send_until {
            interval.tick().await;
            let mut payload = vec![0u8; 256];
            payload[..4].copy_from_slice(&sequence.to_be_bytes());
            payload[4..12].copy_from_slice(&(started.elapsed().as_millis() as u64).to_be_bytes());
            send_media(&connection, Bytes::from(payload)).await?;
            sequence += 1;
        }
        Ok::<_, anyhow::Error>(sequence)
    };
    let datagram_receiver = async {
        let mut received_count = 0u32;
        let mut maximum_gap_ms = 0u128;
        let mut last_received = started;
        let mut latest_sent_ms = 0u64;
        while let Ok(received) =
            tokio::time::timeout_at(receive_until, connection.read_datagram()).await
        {
            let payload = received?;
            ensure!(payload.len() == 256, "invalid recovery datagram");
            let now = Instant::now();
            maximum_gap_ms = maximum_gap_ms.max(now.duration_since(last_received).as_millis());
            last_received = now;
            latest_sent_ms = latest_sent_ms.max(u64::from_be_bytes(payload[4..12].try_into()?));
            received_count += 1;
        }
        ensure!(
            latest_sent_ms as u128 >= run_time.as_millis() - 1000,
            "fresh datagrams did not resume"
        );
        Ok::<_, anyhow::Error>((received_count, maximum_gap_ms, latest_sent_ms))
    };
    let (reliable_result, sent_result, received_result) =
        tokio::join!(reliable, datagram_sender, datagram_receiver);
    println!(
        "RECOVERY_FINISHED: paths={:?} reliable={:?} sent={:?} received={:?}",
        connection.paths(),
        reliable_result,
        sent_result,
        received_result
    );
    connection.close(0u32.into(), b"recovery probe complete");
    let (reliable_exchanges, reliable_maximum_gap_ms) = reliable_result?;
    let datagrams_sent = sent_result?;
    let (datagrams_received, datagram_maximum_gap_ms, latest_datagram_sent_ms) = received_result?;
    println!(
        "{}",
        serde_json::json!({
            "same_quic_connection": true,
            "initial_paths": initial_paths,
            "final_paths": format!("{:?}", connection.paths()),
            "reliable_exchanges": reliable_exchanges,
            "reliable_maximum_gap_ms": reliable_maximum_gap_ms,
            "datagrams_sent": datagrams_sent,
            "datagrams_received": datagrams_received,
            "datagram_maximum_gap_ms": datagram_maximum_gap_ms,
            "latest_datagram_sent_ms": latest_datagram_sent_ms,
        })
    );
    Ok(())
}
