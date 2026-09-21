use axum::http::{HeaderMap, HeaderName};
use std::net::IpAddr;

const X_FORWARDED_FOR: HeaderName = HeaderName::from_static("x-forwarded-for");

#[derive(Clone, Copy, Debug, Default)]
pub(crate) struct ClientAddressResolver {
    trusted_proxy: Option<IpAddr>,
}

impl ClientAddressResolver {
    pub(crate) fn new(trusted_proxy: Option<IpAddr>) -> Self {
        Self { trusted_proxy }
    }

    pub(crate) fn resolve(&self, peer_address: IpAddr, headers: &HeaderMap) -> Result<IpAddr, ()> {
        if self.trusted_proxy != Some(peer_address) {
            return Ok(peer_address);
        }
        if headers.get_all(&X_FORWARDED_FOR).iter().count() != 1 {
            return Err(());
        }
        let forwarded_address = headers
            .get(&X_FORWARDED_FOR)
            .and_then(|header_value| header_value.to_str().ok())
            .filter(|header_value| !header_value.contains(','))
            .ok_or(())?;
        forwarded_address.parse::<IpAddr>().map_err(|_| ())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use axum::http::HeaderValue;

    #[test]
    fn untrusted_peer_cannot_spoof_forwarded_address() {
        let resolver = ClientAddressResolver::new(Some("127.0.0.1".parse().unwrap()));
        let mut headers = HeaderMap::new();
        headers.insert(&X_FORWARDED_FOR, HeaderValue::from_static("203.0.113.7"));

        assert_eq!(
            resolver.resolve("198.51.100.8".parse().unwrap(), &headers),
            Ok("198.51.100.8".parse().unwrap())
        );
    }

    #[test]
    fn trusted_proxy_supplies_exactly_one_client_address() {
        let resolver = ClientAddressResolver::new(Some("127.0.0.1".parse().unwrap()));
        let mut headers = HeaderMap::new();
        headers.insert(&X_FORWARDED_FOR, HeaderValue::from_static("203.0.113.7"));

        assert_eq!(
            resolver.resolve("127.0.0.1".parse().unwrap(), &headers),
            Ok("203.0.113.7".parse().unwrap())
        );
    }

    #[test]
    fn trusted_proxy_rejects_missing_or_chained_forwarding_values() {
        let resolver = ClientAddressResolver::new(Some("127.0.0.1".parse().unwrap()));
        assert_eq!(
            resolver.resolve("127.0.0.1".parse().unwrap(), &HeaderMap::new()),
            Err(())
        );

        let mut chained_headers = HeaderMap::new();
        chained_headers.insert(
            &X_FORWARDED_FOR,
            HeaderValue::from_static("203.0.113.7, 198.51.100.8"),
        );
        assert_eq!(
            resolver.resolve("127.0.0.1".parse().unwrap(), &chained_headers),
            Err(())
        );

        let mut repeated_headers = HeaderMap::new();
        repeated_headers.append(&X_FORWARDED_FOR, HeaderValue::from_static("203.0.113.7"));
        repeated_headers.append(&X_FORWARDED_FOR, HeaderValue::from_static("198.51.100.8"));
        assert_eq!(
            resolver.resolve("127.0.0.1".parse().unwrap(), &repeated_headers),
            Err(())
        );
    }
}
