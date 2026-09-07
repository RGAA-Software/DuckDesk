use chrono::{DateTime, Utc};
use redis::aio::ConnectionManager;
use redis::RedisResult;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::OnceLock;
use std::time::Duration;
use tokio::sync::mpsc;

const TRAFFIC_QUEUE_CAPACITY: usize = 4096;
const TRAFFIC_FLUSH_INTERVAL: Duration = Duration::from_millis(250);
const TRAFFIC_FLUSH_BATCH_SIZE: usize = 512;

struct TrafficSample {
    keys: [String; 2],
    size: i64,
}

pub struct RelayTrafficRecorder {
    sender: OnceLock<mpsc::Sender<TrafficSample>>,
    dropped_samples: AtomicU64,
}

impl RelayTrafficRecorder {
    pub const fn new() -> Self {
        Self {
            sender: OnceLock::new(),
            dropped_samples: AtomicU64::new(0),
        }
    }

    pub fn start(&self, redis_conn: ConnectionManager) -> Result<(), &'static str> {
        let (sender, receiver) = mpsc::channel(TRAFFIC_QUEUE_CAPACITY);
        self.sender
            .set(sender)
            .map_err(|_| "Relay traffic recorder has already started")?;
        tokio::spawn(run_recorder(redis_conn, receiver));
        Ok(())
    }

    pub fn record_upload(&self, device_id: &str, size: i64) {
        self.record("upload", device_id, size);
    }

    pub fn record_download(&self, device_id: &str, size: i64) {
        self.record("down", device_id, size);
    }

    fn record(&self, direction: &str, device_id: &str, size: i64) {
        if size <= 0 {
            return;
        }
        let Some(sender) = self.sender.get() else {
            self.note_dropped_sample();
            return;
        };
        let sample = TrafficSample {
            keys: traffic_keys(direction, device_id, Utc::now()),
            size,
        };
        if sender.try_send(sample).is_err() {
            self.note_dropped_sample();
        }
    }

    fn note_dropped_sample(&self) {
        let dropped = self.dropped_samples.fetch_add(1, Ordering::Relaxed) + 1;
        if dropped == 1 || dropped.is_multiple_of(1024) {
            tracing::warn!(
                dropped,
                "dropping Relay traffic statistics without blocking the data path"
            );
        }
    }
}

fn traffic_keys(direction: &str, device_id: &str, now: DateTime<Utc>) -> [String; 2] {
    [
        format!("{direction}_{device_id}:{}", now.format("%Y_%m")),
        format!("{direction}_{device_id}:{}", now.format("%Y_%m_%d")),
    ]
}

async fn run_recorder(
    mut redis_conn: ConnectionManager,
    mut receiver: mpsc::Receiver<TrafficSample>,
) {
    let mut totals = HashMap::<String, i64>::new();
    let mut flush_timer = tokio::time::interval(TRAFFIC_FLUSH_INTERVAL);
    flush_timer.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);

    loop {
        tokio::select! {
            sample = receiver.recv() => {
                let Some(sample) = sample else {
                    flush_totals(&mut redis_conn, &mut totals).await;
                    return;
                };
                for key in sample.keys {
                    *totals.entry(key).or_default() += sample.size;
                }
                if totals.len() >= TRAFFIC_FLUSH_BATCH_SIZE {
                    flush_totals(&mut redis_conn, &mut totals).await;
                }
            }
            _ = flush_timer.tick() => flush_totals(&mut redis_conn, &mut totals).await,
        }
    }
}

async fn flush_totals(redis_conn: &mut ConnectionManager, totals: &mut HashMap<String, i64>) {
    if totals.is_empty() {
        return;
    }
    let pending = std::mem::take(totals);
    let mut pipeline = redis::pipe();
    for (key, size) in &pending {
        pipeline.cmd("INCRBY").arg(key).arg(size).ignore();
    }
    let result: RedisResult<()> = pipeline.query_async(redis_conn).await;
    if let Err(error) = result {
        tracing::warn!(sample_count = pending.len(), %error, "failed to flush Relay traffic statistics");
    }
}

#[cfg(test)]
mod tests {
    use super::traffic_keys;
    use chrono::{TimeZone, Utc};

    #[test]
    fn traffic_keys_include_direction_device_and_utc_period() {
        let now = Utc.with_ymd_and_hms(2026, 9, 7, 23, 59, 58).unwrap();
        assert_eq!(
            traffic_keys("upload", "device-1", now),
            [
                "upload_device-1:2026_09".to_string(),
                "upload_device-1:2026_09_07".to_string(),
            ]
        );
    }
}
