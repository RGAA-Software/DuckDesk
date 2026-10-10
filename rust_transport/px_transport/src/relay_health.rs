//! Recover the home Relay through iroh's public candidate-update API.
use std::{sync::Arc, time::Duration};

use crate::relay_candidates::RelayCandidates;
use iroh::{Endpoint, Watcher};
use tokio::{task::JoinHandle, time::Instant};

const DISCONNECTED_GRACE: Duration = Duration::from_secs(2);
const REPROBE_INTERVAL: Duration = Duration::from_secs(5);

pub(crate) struct RelayHealth {
    worker: JoinHandle<()>,
}

impl RelayHealth {
    pub(crate) fn start(endpoint: Endpoint, candidates: Arc<RelayCandidates>) -> Self {
        let worker = tokio::spawn(async move {
            let mut disconnected_since = None;
            let mut last_reprobe = None;
            let mut interval = tokio::time::interval(Duration::from_millis(250));
            interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
            loop {
                interval.tick().await;
                if endpoint.is_closed() {
                    break;
                }
                let statuses = endpoint.home_relay_status().get();
                candidates.recover_admission(&endpoint, &statuses).await;
                if statuses.iter().any(|status| status.is_connected()) {
                    disconnected_since = None;
                    continue;
                }
                let now = Instant::now();
                let disconnected_at = *disconnected_since.get_or_insert(now);
                if now.duration_since(disconnected_at) < DISCONNECTED_GRACE
                    || last_reprobe
                        .is_some_and(|previous| now.duration_since(previous) < REPROBE_INTERVAL)
                {
                    continue;
                }
                // iroh 1.3.0's incremental HTTPS-only probe plan may be empty.
                // network_change() only asks the OS monitor for interface changes.
                // Reapplying an existing candidate triggers a full network report without
                // changing the EndpointId, closing streams or disconnecting healthy peers.
                candidates.reprobe(&endpoint).await;
                last_reprobe = Some(now);
            }
        });
        Self { worker }
    }

    pub(crate) fn stop(&self) {
        self.worker.abort();
    }
}

impl Drop for RelayHealth {
    fn drop(&mut self) {
        self.stop();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use iroh::{RelayMode, endpoint::presets};
    use iroh_relay::RelayConfig;

    #[tokio::test]
    async fn stop_and_drop_cancel_the_endpoint_worker() {
        for explicit_stop in [true, false, true] {
            let endpoint = Endpoint::builder(presets::Minimal)
                .relay_mode(RelayMode::Disabled)
                .bind()
                .await
                .unwrap();
            let relay = Arc::new(RelayConfig::new(
                "https://127.0.0.1:18551".parse().unwrap(),
                None,
            ));
            let updates = Arc::new(
                RelayCandidates::new(&[crate::PrivateRelay {
                    url: relay.url.clone(),
                    qad_port: None,
                }])
                .unwrap(),
            );
            let health = RelayHealth::start(endpoint.clone(), updates);
            let cancellation = health.worker.abort_handle();
            if explicit_stop {
                health.stop();
                health.stop();
            }
            drop(health);
            tokio::time::timeout(Duration::from_secs(1), async {
                while !cancellation.is_finished() {
                    tokio::task::yield_now().await;
                }
            })
            .await
            .expect("dropped health worker retained the endpoint");
            endpoint.close().await;
        }
    }

    #[tokio::test]
    async fn closing_the_endpoint_exits_the_health_loop() {
        let endpoint = Endpoint::builder(presets::Minimal)
            .relay_mode(RelayMode::Disabled)
            .bind()
            .await
            .unwrap();
        let relay = Arc::new(RelayConfig::new(
            "https://127.0.0.1:18551".parse().unwrap(),
            None,
        ));
        let updates = Arc::new(
            RelayCandidates::new(&[crate::PrivateRelay {
                url: relay.url.clone(),
                qad_port: None,
            }])
            .unwrap(),
        );
        let health = RelayHealth::start(endpoint.clone(), updates);
        endpoint.close().await;
        tokio::time::timeout(Duration::from_secs(1), async {
            while !health.worker.is_finished() {
                tokio::task::yield_now().await;
            }
        })
        .await
        .expect("closed endpoint left a health worker running");
    }
}
