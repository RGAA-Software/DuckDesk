use std::{collections::BTreeMap, sync::Arc, time::Duration};

use anyhow::{Result, ensure};
use iroh::{Endpoint, RelayUrl, endpoint::RelayStatus};
use iroh_relay::{RelayConfig, RelayQuicConfig};
use tokio::sync::Mutex;
use tokio::time::Instant;

use crate::PrivateRelay;

type CandidateMap = BTreeMap<RelayUrl, Arc<RelayConfig>>;

pub(crate) struct RelayCandidates {
    current: Mutex<CandidateState>,
}

struct CandidateState {
    configured: CandidateMap,
    retry_after: BTreeMap<RelayUrl, Instant>,
}

impl RelayCandidates {
    pub(crate) fn new(relays: &[PrivateRelay]) -> Result<Self> {
        Ok(Self {
            current: Mutex::new(CandidateState {
                configured: Self::validate(relays)?,
                retry_after: BTreeMap::new(),
            }),
        })
    }

    pub(crate) async fn reprobe(&self, endpoint: &Endpoint) {
        // Serialize with updates so a health retry cannot reinsert a withdrawn Relay.
        let candidates = self.current.lock().await;
        if let Some((url, configuration)) = candidates
            .configured
            .iter()
            .find(|(url, _)| !candidates.retry_after.contains_key(*url))
        {
            endpoint
                .insert_relay(url.clone(), configuration.clone())
                .await;
        }
    }

    pub(crate) async fn recover_admission(&self, endpoint: &Endpoint, statuses: &[RelayStatus]) {
        // Keep healthy connections on their current home. Retrying suppressed
        // candidates must not move a working endpoint back onto a full Relay.
        if endpoint.is_closed() || statuses.iter().any(RelayStatus::is_connected) {
            return;
        }
        let mut candidates = self.current.lock().await;
        let now = Instant::now();
        let retry_urls: Vec<_> = candidates
            .retry_after
            .iter()
            .filter(|(_, retry_at)| **retry_at <= now)
            .map(|(url, _)| url.clone())
            .collect();
        for url in retry_urls {
            candidates.retry_after.remove(&url);
            if let Some(configuration) = candidates.configured.get(&url) {
                endpoint.insert_relay(url, configuration.clone()).await;
            }
        }
        for status in statuses {
            // This public typed signal also covers capacity and maintenance
            // denials. The reason string is diagnostic text, never a protocol.
            if status.auth_denied_reason().is_none()
                || !candidates.configured.contains_key(status.url())
                || candidates.retry_after.contains_key(status.url())
                || candidates.configured.len() - candidates.retry_after.len() <= 1
            {
                continue;
            }
            candidates
                .retry_after
                .insert(status.url().clone(), now + Duration::from_secs(5));
            endpoint.remove_relay(status.url()).await;
            tracing::warn!(relay = %status.url(), "Relay admission denied; trying another configured candidate");
        }
    }

    fn validate(relays: &[PrivateRelay]) -> Result<CandidateMap> {
        ensure!(relays.len() <= 32, "too many Relay candidates");
        let mut candidates = CandidateMap::new();
        for relay in relays {
            ensure!(
                matches!(relay.url.scheme(), "https" | "http"),
                "invalid Relay scheme"
            );
            ensure!(
                relay.url.username().is_empty() && relay.url.password().is_none(),
                "Relay URL contains credentials"
            );
            ensure!(
                relay.url.query().is_none() && relay.url.fragment().is_none(),
                "invalid Relay URL suffix"
            );
            ensure!(relay.qad_port != Some(0), "invalid QAD port");
            ensure!(
                relay.qad_port.is_none() || relay.url.scheme() == "https",
                "QAD requires HTTPS"
            );
            ensure!(
                !candidates.contains_key(&relay.url),
                "duplicate Relay candidate"
            );
            candidates.insert(
                relay.url.clone(),
                Arc::new(RelayConfig::new(
                    relay.url.clone(),
                    relay.qad_port.map(RelayQuicConfig::new),
                )),
            );
        }
        Ok(candidates)
    }

    pub(crate) async fn update(
        self: &Arc<Self>,
        endpoint: &Endpoint,
        relays: Vec<PrivateRelay>,
    ) -> Result<()> {
        let replacement = Self::validate(&relays)?;
        let candidates = self.clone();
        let endpoint = endpoint.clone();
        // The FFI caller has a deadline. Once accepted, finish the serialized
        // update even if that waiter times out; cancellation between map writes
        // must not leave health retries using a different candidate set.
        tokio::spawn(async move { candidates.apply(&endpoint, replacement).await }).await?
    }

    async fn apply(&self, endpoint: &Endpoint, replacement: CandidateMap) -> Result<()> {
        let mut current = self.current.lock().await;
        ensure!(!endpoint.is_closed(), "endpoint closed");
        if current.configured == replacement {
            return Ok(());
        }
        // Add alternatives before withdrawing old candidates. These APIs update
        // home selection without closing existing peer connections or streams.
        for (url, configuration) in &replacement {
            if current.configured.get(url) != Some(configuration) {
                current.retry_after.remove(url);
                endpoint
                    .insert_relay(url.clone(), configuration.clone())
                    .await;
            }
        }
        for url in current
            .configured
            .keys()
            .filter(|url| !replacement.contains_key(*url))
        {
            endpoint.remove_relay(url).await;
        }
        current
            .retry_after
            .retain(|url, _| replacement.contains_key(url));
        // A Console update can withdraw every unsuppressed alternative. Keep
        // the remaining configured candidate eligible, just as for a single
        // Relay at startup; temporary local policy must not leave an empty map.
        if !replacement.is_empty()
            && replacement
                .keys()
                .all(|url| current.retry_after.contains_key(url))
        {
            if let Some((url, configuration)) = replacement.first_key_value() {
                current.retry_after.remove(url);
                endpoint
                    .insert_relay(url.clone(), configuration.clone())
                    .await;
            }
        }
        current.configured = replacement;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use iroh::{RelayMode, endpoint::presets};

    #[tokio::test]
    async fn console_withdrawal_of_the_alternative_reactivates_the_remaining_candidate() {
        let endpoint = Endpoint::builder(presets::Minimal)
            .relay_mode(RelayMode::Custom(iroh::RelayMap::empty()))
            .bind()
            .await
            .unwrap();
        let primary = PrivateRelay {
            url: "https://127.0.0.1:18601".parse().unwrap(),
            qad_port: None,
        };
        let alternative = PrivateRelay {
            url: "https://127.0.0.1:18602".parse().unwrap(),
            qad_port: None,
        };
        let candidates = Arc::new(RelayCandidates::new(&[]).unwrap());
        candidates
            .update(&endpoint, vec![primary.clone(), alternative.clone()])
            .await
            .unwrap();
        candidates
            .current
            .lock()
            .await
            .retry_after
            .insert(primary.url.clone(), Instant::now() + Duration::from_secs(5));
        endpoint.remove_relay(&primary.url).await;
        candidates
            .update(&endpoint, vec![primary.clone(), alternative])
            .await
            .unwrap();
        assert!(
            candidates
                .current
                .lock()
                .await
                .retry_after
                .contains_key(&primary.url)
        );
        candidates
            .update(&endpoint, vec![primary.clone()])
            .await
            .unwrap();
        let current = candidates.current.lock().await;
        assert_eq!(current.configured.len(), 1);
        assert!(current.configured.contains_key(&primary.url));
        assert!(current.retry_after.is_empty());
        drop(current);
        endpoint.close().await;
    }

    #[tokio::test]
    async fn invalid_replacement_preserves_candidates_and_closed_endpoint_rejects_update() {
        let endpoint = Endpoint::builder(presets::Minimal)
            .relay_mode(RelayMode::Disabled)
            .bind()
            .await
            .unwrap();
        let candidates = Arc::new(RelayCandidates::new(&[]).unwrap());
        let invalid = PrivateRelay {
            url: "https://127.0.0.1:18555".parse().unwrap(),
            qad_port: Some(0),
        };
        assert!(candidates.update(&endpoint, vec![invalid]).await.is_err());
        assert!(candidates.current.lock().await.configured.is_empty());
        endpoint.close().await;
        assert!(candidates.update(&endpoint, vec![]).await.is_err());
    }

    #[tokio::test]
    async fn cancelling_the_waiter_does_not_abandon_an_accepted_update() {
        let endpoint = Endpoint::builder(presets::Minimal)
            .relay_mode(RelayMode::Disabled)
            .bind()
            .await
            .unwrap();
        let candidates = Arc::new(RelayCandidates::new(&[]).unwrap());
        let initial_guard = candidates.current.lock().await;
        let replacement = PrivateRelay {
            url: "https://127.0.0.1:18555".parse().unwrap(),
            qad_port: None,
        };
        let relay_url = replacement.url.clone();
        let waiter = candidates.update(&endpoint, vec![replacement]);
        assert!(
            tokio::time::timeout(std::time::Duration::from_millis(20), waiter)
                .await
                .is_err()
        );
        drop(initial_guard);
        tokio::time::timeout(std::time::Duration::from_secs(1), async {
            loop {
                if candidates
                    .current
                    .lock()
                    .await
                    .configured
                    .contains_key(&relay_url)
                {
                    break;
                }
                tokio::task::yield_now().await;
            }
        })
        .await
        .expect("accepted update was abandoned with its caller");
        candidates.update(&endpoint, vec![]).await.unwrap();
        candidates.reprobe(&endpoint).await;
        assert!(candidates.current.lock().await.configured.is_empty());
        endpoint.close().await;
    }
}
