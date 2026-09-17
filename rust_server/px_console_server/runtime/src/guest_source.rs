//! Deployment-keyed guest source correlation derived only from the direct socket peer.

use crate::error::ApiError;
use px_console_store::OriginFingerprint;
use std::{net::IpAddr, path::PathBuf, time::Duration};
use uuid::Uuid;
use zeroize::Zeroizing;

pub struct GuestAdmission {
    deployment: Uuid,
    key: Zeroizing<[u8; 32]>,
    pub(crate) enabled: bool,
    pub(crate) lifetime: Duration,
    limits: px_credentials::LoginLimits,
}

impl GuestAdmission {
    /// Loads the stable private deployment key through the ACL/type-checked file handle.
    /// There is no generated restart fallback because changing the key would change the
    /// identity of existing source bans.
    pub async fn load(
        deployment: Uuid,
        path: PathBuf,
        enabled: bool,
        lifetime: Duration,
    ) -> Result<Self, ApiError> {
        let bytes =
            tokio::task::spawn_blocking(move || px_private_files::private::read_private(&path))
                .await
                .map_err(|_| ApiError::Internal)?
                .map_err(|_| ApiError::Unavailable)?;
        if bytes.len() != 32 {
            return Err(ApiError::Invalid);
        }
        let mut key = Zeroizing::new([0; 32]);
        key.copy_from_slice(&bytes);
        Self::from_key(deployment, key, enabled, lifetime)
    }

    fn from_key(
        deployment: Uuid,
        key: Zeroizing<[u8; 32]>,
        enabled: bool,
        lifetime: Duration,
    ) -> Result<Self, ApiError> {
        if deployment.is_nil()
            || *key == [0; 32]
            || lifetime.subsec_nanos() != 0
            || !(60..=86400).contains(&lifetime.as_secs())
        {
            return Err(ApiError::Invalid);
        }
        Ok(Self {
            deployment,
            key,
            enabled,
            lifetime,
            limits: Default::default(),
        })
    }

    /// Isolated database/API tests use a deterministic synthetic key, never a product runtime.
    #[cfg(feature = "pg-integration")]
    pub fn for_isolated_test(
        deployment: Uuid,
        key: Zeroizing<[u8; 32]>,
        enabled: bool,
        lifetime: Duration,
    ) -> Result<Self, ApiError> {
        if std::env::var("PIXELS_PG_ISOLATED_TEST").as_deref() != Ok("1") {
            return Err(ApiError::Rejected);
        }
        Self::from_key(deployment, key, enabled, lifetime)
    }

    pub(crate) fn matches(&self, deployment: Uuid) -> bool {
        self.deployment == deployment
    }

    pub(crate) fn admit(&self, ip: IpAddr) -> Result<OriginFingerprint, ApiError> {
        if !self.enabled {
            return Err(ApiError::Rejected);
        }
        let ip = canonical(ip);
        if !self.limits.allow(&ip.to_string(), ip) {
            return Err(ApiError::RateLimited);
        }
        Ok(OriginFingerprint::from_hmac_sha256(self.digest(ip)))
    }

    fn digest(&self, ip: IpAddr) -> [u8; 32] {
        let key = ring::hmac::Key::new(ring::hmac::HMAC_SHA256, self.key.as_slice());
        let mut context = ring::hmac::Context::with_key(&key);
        context.update(b"Pixels-Guest-Source-v1\0");
        context.update(self.deployment.as_bytes());
        match canonical(ip) {
            IpAddr::V4(value) => {
                context.update(&[4]);
                context.update(&value.octets());
            }
            IpAddr::V6(value) => {
                context.update(&[6]);
                context.update(&value.octets());
            }
        }
        context
            .sign()
            .as_ref()
            .try_into()
            .expect("SHA-256 has a fixed 32-byte output")
    }
}

fn canonical(ip: IpAddr) -> IpAddr {
    match ip {
        IpAddr::V6(value) => value.to_ipv4_mapped().map(IpAddr::V4).unwrap_or(ip),
        _ => ip,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn source_is_domain_separated_canonical_and_never_a_client_claim() {
        let deployment = Uuid::parse_str("12345678-1234-4234-8234-123456789abc").unwrap();
        let first = GuestAdmission::from_key(
            deployment,
            Zeroizing::new([42; 32]),
            true,
            Duration::from_secs(3600),
        )
        .unwrap();
        let mapped = "::ffff:192.0.2.1".parse().unwrap();
        let v4 = "192.0.2.1".parse().unwrap();
        assert_eq!(first.digest(mapped), first.digest(v4));
        assert_ne!(first.digest(v4), first.digest("192.0.2.2".parse().unwrap()));

        let other = GuestAdmission::from_key(
            Uuid::new_v4(),
            Zeroizing::new([42; 32]),
            true,
            Duration::from_secs(3600),
        )
        .unwrap();
        assert_ne!(first.digest(v4), other.digest(v4));
        let other_key = GuestAdmission::from_key(
            deployment,
            Zeroizing::new([43; 32]),
            true,
            Duration::from_secs(3600),
        )
        .unwrap();
        assert_ne!(first.digest(v4), other_key.digest(v4));

        for _ in 0..10 {
            assert!(first.admit(v4).is_ok());
        }
        assert!(matches!(first.admit(mapped), Err(ApiError::RateLimited)));

        // Independently computed with Node.js crypto/OpenSSL, not this implementation.
        assert_eq!(
            hex::encode(first.digest(v4)),
            "10956f5ab56d15827a5dc3ddec223ba8582e502c3c3c7dd329d0432f202727ba"
        );
        assert_eq!(
            hex::encode(first.digest("2001:db8::1".parse().unwrap())),
            "4576fca32eddf4e40eddc06e86015e90841219e799b83b4d6ff4a214fc657e14"
        );

        let disabled = GuestAdmission::from_key(
            deployment,
            Zeroizing::new([42; 32]),
            false,
            Duration::from_secs(60),
        )
        .unwrap();
        assert!(matches!(disabled.admit(v4), Err(ApiError::Rejected)));
        assert!(GuestAdmission::from_key(
            deployment,
            Zeroizing::new([0; 32]),
            true,
            Duration::from_secs(60)
        )
        .is_err());
        assert!(GuestAdmission::from_key(
            deployment,
            Zeroizing::new([42; 32]),
            true,
            Duration::from_secs(59)
        )
        .is_err());
    }
}
