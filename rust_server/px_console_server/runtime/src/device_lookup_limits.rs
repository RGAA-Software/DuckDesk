use std::{
    collections::HashMap,
    net::IpAddr,
    sync::Mutex,
    time::{Duration, Instant},
};

const WINDOW: Duration = Duration::from_secs(60);
const DEVICE_REQUESTS_PER_SOURCE: u32 = 60;
const TOTAL_REQUESTS_PER_SOURCE: u32 = 600;
const MAX_BUCKETS: usize = 4096;

#[derive(Clone, Eq, Hash, PartialEq)]
enum LookupIdentity {
    Source(IpAddr),
    Device { source: IpAddr, code: String },
}

struct LookupBucket {
    started: Instant,
    requests: u32,
}

// Public endpoint reads are not password attempts. Scope device polling to its source,
// so one viewer cannot exhaust the device's budget for every other viewer.
#[derive(Default)]
pub(crate) struct DeviceLookupLimits {
    buckets: Mutex<HashMap<LookupIdentity, LookupBucket>>,
}

impl DeviceLookupLimits {
    pub(crate) fn allow(&self, code: &str, source: IpAddr) -> bool {
        self.allow_at(code, source, Instant::now())
    }

    fn allow_at(&self, code: &str, source: IpAddr, observed_at: Instant) -> bool {
        let Ok(mut buckets) = self.buckets.lock() else {
            return false;
        };
        buckets.retain(|_, bucket| observed_at.duration_since(bucket.started) < WINDOW);
        let device_key = LookupIdentity::Device {
            source,
            code: code.to_owned(),
        };
        let source_key = LookupIdentity::Source(source);
        let missing_buckets = usize::from(!buckets.contains_key(&device_key))
            + usize::from(!buckets.contains_key(&source_key));
        if buckets.len() + missing_buckets > MAX_BUCKETS {
            return false;
        }
        for (identity, limit) in [
            (&device_key, DEVICE_REQUESTS_PER_SOURCE),
            (&source_key, TOTAL_REQUESTS_PER_SOURCE),
        ] {
            if buckets
                .get(identity)
                .is_some_and(|bucket| bucket.requests >= limit)
            {
                return false;
            }
        }
        for identity in [device_key, source_key] {
            buckets
                .entry(identity)
                .or_insert(LookupBucket {
                    started: observed_at,
                    requests: 0,
                })
                .requests += 1;
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn two_second_presence_polling_remains_allowed_across_windows() {
        let limits = DeviceLookupLimits::default();
        let source = IpAddr::from([192, 168, 31, 6]);
        let started = Instant::now();
        for poll_index in 0..180 {
            assert!(limits.allow_at(
                "934886467",
                source,
                started + Duration::from_secs(poll_index * 2),
            ));
        }
    }

    #[test]
    fn device_throttle_is_bounded_and_does_not_affect_other_viewers() {
        let limits = DeviceLookupLimits::default();
        let source = IpAddr::from([192, 168, 31, 6]);
        let another_source = IpAddr::from([192, 168, 31, 7]);
        let started = Instant::now();
        for _ in 0..DEVICE_REQUESTS_PER_SOURCE {
            assert!(limits.allow_at("934886467", source, started));
        }
        assert!(!limits.allow_at("934886467", source, started));
        assert!(limits.allow_at("934886467", another_source, started));
        assert!(limits.allow_at("934886468", source, started));
        assert!(limits.allow_at("934886467", source, started + WINDOW));
    }

    #[test]
    fn rotating_codes_cannot_bypass_the_source_budget() {
        let limits = DeviceLookupLimits::default();
        let source = IpAddr::from([192, 168, 31, 6]);
        let started = Instant::now();
        for code_index in 0..TOTAL_REQUESTS_PER_SOURCE {
            assert!(limits.allow_at(&format!("{code_index:09}"), source, started));
        }
        assert!(!limits.allow_at("934886467", source, started));
        assert!(limits.allow_at("934886467", source, started + WINDOW));
    }

    #[test]
    fn bucket_capacity_fails_closed_and_expired_entries_are_reclaimed() {
        let limits = DeviceLookupLimits::default();
        let started = Instant::now();
        for source_index in 0..MAX_BUCKETS / 2 {
            let source = IpAddr::from([10, 0, (source_index / 256) as u8, source_index as u8]);
            assert!(limits.allow_at("934886467", source, started));
        }
        assert_eq!(limits.buckets.lock().unwrap().len(), MAX_BUCKETS);
        let additional_source = IpAddr::from([192, 168, 31, 6]);
        assert!(!limits.allow_at("934886467", additional_source, started));
        assert!(limits.allow_at("934886467", additional_source, started + WINDOW));
        assert_eq!(limits.buckets.lock().unwrap().len(), 2);
    }

    #[test]
    fn public_reads_do_not_change_login_attempt_limits() {
        let lookups = DeviceLookupLimits::default();
        let logins = px_credentials::LoginLimits::default();
        let source = IpAddr::from([192, 168, 31, 6]);
        for _ in 0..30 {
            assert!(lookups.allow("934886467", source));
        }
        for _ in 0..10 {
            assert!(logins.allow("Pixels", source));
        }
        assert!(!logins.allow("Pixels", source));
        assert!(lookups.allow("934886467", source));
    }
}
