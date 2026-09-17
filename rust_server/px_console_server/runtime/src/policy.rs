use crate::error::ApiError;
use px_console_store::ClientType;
use std::time::Duration;

/// Validated direct-TLS ingress policy. Forwarded identities are not supported implicitly.
pub struct IngressPolicy {
    origin: String,
    pub(crate) registration: bool,
    pub(crate) session_lifetime: Duration,
}
impl IngressPolicy {
    pub fn new(
        origin: &str,
        registration: bool,
        session_lifetime: Duration,
        local: bool,
    ) -> Result<Self, ApiError> {
        let url = url::Url::parse(origin).map_err(|_| ApiError::Invalid)?;
        if url.origin().ascii_serialization() != origin
            || !url.username().is_empty()
            || url.password().is_some()
            || url.query().is_some()
            || url.fragment().is_some()
            || !(url.scheme() == "https"
                || (local
                    && url.scheme() == "http"
                    && matches!(url.host_str(), Some("127.0.0.1" | "localhost" | "[::1]"))))
            || session_lifetime.subsec_nanos() != 0
            || !(60..=86400).contains(&session_lifetime.as_secs())
        {
            return Err(ApiError::Invalid);
        }
        Ok(Self {
            origin: origin.into(),
            registration,
            session_lifetime,
        })
    }
    pub(crate) fn check(
        &self,
        headers: &axum::http::HeaderMap,
        client: ClientType,
    ) -> Result<(), ApiError> {
        if headers
            .keys()
            .any(|key| key.as_str() == "forwarded" || key.as_str().starts_with("x-forwarded-"))
        {
            return Err(ApiError::Rejected);
        }
        let origins = headers
            .get_all(axum::http::header::ORIGIN)
            .iter()
            .collect::<Vec<_>>();
        match origins.as_slice() {
            [value] if value.to_str().ok() == Some(self.origin.as_str()) => Ok(()),
            [] if matches!(client, ClientType::Panel | ClientType::Android) => Ok(()),
            _ => Err(ApiError::Rejected),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn origins_and_lifetimes_are_explicit_and_canonical() {
        for origin in [
            "https://site.test/",
            "https://user@site.test",
            "https://site.test/path",
            "https://site.test?x=y",
            "http://site.test",
            "null",
        ] {
            assert!(IngressPolicy::new(origin, true, Duration::from_secs(60), true).is_err());
        }
        assert!(IngressPolicy::new(
            "http://127.0.0.1:8123",
            false,
            Duration::from_secs(60),
            true
        )
        .is_ok());
        assert!(
            IngressPolicy::new("http://127.0.0.1", false, Duration::from_secs(60), false).is_err()
        );
        assert!(IngressPolicy::new(
            "https://site.test",
            false,
            Duration::from_secs(86401),
            false
        )
        .is_err());
    }
}
