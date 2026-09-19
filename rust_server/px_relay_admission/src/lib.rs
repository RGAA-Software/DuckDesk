//! Short-lived Relay websocket admission tickets shared by Console and Relay.
use hmac::{Hmac, Mac};
use sha2::Sha256;
use uuid::Uuid;
use zeroize::Zeroizing;

const VERSION: &str = "pxr1";
const MAX_LIFETIME_SECONDS: u64 = 300;
type HmacSha256 = Hmac<Sha256>;

/// Mints a ticket that admits only the frontend socket naming this resource session and
/// remote resource. Render still performs the authoritative Console frontend-grant check.
pub fn issue(
    secret: &[u8],
    session_id: Uuid,
    remote_resource_id: Uuid,
    now_unix_seconds: u64,
    expires_at_unix_seconds: u64,
) -> Option<Zeroizing<String>> {
    if !(16..=512).contains(&secret.len())
        || session_id.is_nil()
        || remote_resource_id.is_nil()
        || expires_at_unix_seconds <= now_unix_seconds
        || expires_at_unix_seconds - now_unix_seconds > MAX_LIFETIME_SECONDS
    {
        return None;
    }
    let payload = format!("{VERSION}.{expires_at_unix_seconds}.{session_id}.{remote_resource_id}");
    let mut signer = HmacSha256::new_from_slice(secret).ok()?;
    signer.update(payload.as_bytes());
    Some(Zeroizing::new(format!(
        "{payload}.{}",
        hex::encode(signer.finalize().into_bytes())
    )))
}

/// Verifies a ticket against the websocket's resource identities and the Relay host's
/// own clock. Parsing is deliberately strict and bounded.
pub fn verify(
    secret: &[u8],
    ticket: &str,
    expected_session_id: Uuid,
    expected_remote_resource_id: Uuid,
    now_unix_seconds: u64,
) -> bool {
    if !(16..=512).contains(&secret.len()) || ticket.len() > 256 {
        return false;
    }
    let mut fields = ticket.split('.');
    let (Some(version), Some(expiration), Some(session), Some(remote), Some(signature)) = (
        fields.next(),
        fields.next(),
        fields.next(),
        fields.next(),
        fields.next(),
    ) else {
        return false;
    };
    if fields.next().is_some() || version != VERSION || signature.len() != 64 {
        return false;
    }
    let Ok(expires_at_unix_seconds) = expiration.parse::<u64>() else {
        return false;
    };
    let Ok(session_id) = Uuid::parse_str(session) else {
        return false;
    };
    let Ok(remote_resource_id) = Uuid::parse_str(remote) else {
        return false;
    };
    if session_id != expected_session_id
        || remote_resource_id != expected_remote_resource_id
        || expires_at_unix_seconds <= now_unix_seconds
        || expires_at_unix_seconds - now_unix_seconds > MAX_LIFETIME_SECONDS
    {
        return false;
    }
    let Ok(signature_bytes) = hex::decode(signature) else {
        return false;
    };
    let payload = format!("{VERSION}.{expires_at_unix_seconds}.{session_id}.{remote_resource_id}");
    HmacSha256::new_from_slice(secret)
        .map(|mut verifier| {
            verifier.update(payload.as_bytes());
            verifier.verify_slice(&signature_bytes).is_ok()
        })
        .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn admission_is_short_lived_bound_and_tamper_evident() {
        let secret = b"isolated-relay-app-key";
        let session_id = Uuid::parse_str("10000000-0000-0000-0000-000000000001").unwrap();
        let remote_resource_id = Uuid::parse_str("20000000-0000-0000-0000-000000000002").unwrap();
        let ticket = issue(secret, session_id, remote_resource_id, 1_000, 1_060).unwrap();
        assert!(verify(
            secret,
            &ticket,
            session_id,
            remote_resource_id,
            1_001
        ));
        assert!(!verify(
            secret,
            &ticket,
            session_id,
            remote_resource_id,
            1_060
        ));
        assert!(!verify(
            secret,
            &ticket,
            Uuid::new_v4(),
            remote_resource_id,
            1_001
        ));
        assert!(!verify(
            b"different-relay-key",
            &ticket,
            session_id,
            remote_resource_id,
            1_001
        ));
        let mut tampered = ticket.to_string();
        let replacement = if tampered.ends_with('0') { "1" } else { "0" };
        tampered.replace_range(tampered.len() - 1.., replacement);
        assert!(!verify(
            secret,
            &tampered,
            session_id,
            remote_resource_id,
            1_001
        ));
    }
}
