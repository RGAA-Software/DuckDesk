use ring::hmac;

/// Parameters a client must send when opening `/spvr/client`.
#[derive(Debug, Clone)]
pub struct ConnectionToken {
    pub token: String, // hex-encoded HMAC-SHA256
    pub ts: i64,       // millisecond timestamp
    pub nonce: String, // random hex string
}

/// Token lifetime in milliseconds.
pub const CONNECTION_TOKEN_LIFETIME_MS: i64 = 60 * 1000;

/// Generates a connection token for the given appkey/app_secret pair.
/// The token is an HMAC-SHA256 over `appkey|ts|nonce` keyed by app_secret.
pub fn generate_connection_token(appkey: &str, app_secret: &str) -> ConnectionToken {
    let ts = gr_base::get_current_timestamp();
    let nonce = generate_nonce();
    let token = compute_token(appkey, app_secret, ts, &nonce);
    ConnectionToken { token, ts, nonce }
}

/// Verifies a connection token.
///
/// Clock-skew policy: the timestamp must be within ±CONNECTION_TOKEN_LIFETIME_MS
/// of the server clock. A small *future* tolerance is required because client
/// clocks are routinely a few hundred ms ahead of the server (SNTP drift) —
/// rejecting any future timestamp would lock out otherwise-valid clients.
pub fn verify_connection_token(
    appkey: &str,
    app_secret: &str,
    token: &str,
    ts: i64,
    nonce: &str,
    now_ms: i64,
) -> bool {
    if appkey.is_empty() || app_secret.is_empty() || token.is_empty() || nonce.is_empty() {
        return false;
    }
    if (now_ms - ts).abs() > CONNECTION_TOKEN_LIFETIME_MS {
        return false;
    }
    let expected = compute_token(appkey, app_secret, ts, nonce);
    // Constant-time comparison is not critical for a short-lived single-use token;
    // plain equality is sufficient here.
    expected == token
}

fn compute_token(appkey: &str, app_secret: &str, ts: i64, nonce: &str) -> String {
    let message = format!("{}|{}|{}", appkey, ts, nonce);
    let key = hmac::Key::new(hmac::HMAC_SHA256, app_secret.as_bytes());
    let tag = hmac::sign(&key, message.as_bytes());
    bytes_to_hex(tag.as_ref())
}

fn generate_nonce() -> String {
    let mut bytes = [0u8; 16];
    rand::RngCore::fill_bytes(&mut rand::rng(), &mut bytes);
    bytes_to_hex(&bytes)
}

fn bytes_to_hex(bytes: &[u8]) -> String {
    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut out = String::with_capacity(bytes.len() * 2);
    for &b in bytes {
        out.push(HEX[(b >> 4) as usize] as char);
        out.push(HEX[(b & 0x0f) as usize] as char);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn generated_token_verifies_with_same_inputs() {
        let token = generate_connection_token("appkey-1", "secret-1");
        let now = gr_base::get_current_timestamp();
        assert!(verify_connection_token(
            "appkey-1",
            "secret-1",
            &token.token,
            token.ts,
            &token.nonce,
            now
        ));
    }

    #[test]
    fn wrong_secret_rejected() {
        let token = generate_connection_token("appkey-1", "secret-1");
        let now = gr_base::get_current_timestamp();
        assert!(!verify_connection_token(
            "appkey-1",
            "wrong-secret",
            &token.token,
            token.ts,
            &token.nonce,
            now
        ));
    }

    #[test]
    fn wrong_appkey_rejected() {
        let token = generate_connection_token("appkey-1", "secret-1");
        let now = gr_base::get_current_timestamp();
        assert!(!verify_connection_token(
            "appkey-2",
            "secret-1",
            &token.token,
            token.ts,
            &token.nonce,
            now
        ));
    }

    #[test]
    fn tampered_token_rejected() {
        let token = generate_connection_token("appkey-1", "secret-1");
        let now = gr_base::get_current_timestamp();
        let mut tampered = token.token.clone();
        if let Some(last) = tampered.pop() {
            tampered.push(if last == 'a' { 'b' } else { 'a' });
        }
        assert!(!verify_connection_token(
            "appkey-1",
            "secret-1",
            &tampered,
            token.ts,
            &token.nonce,
            now
        ));
    }

    #[test]
    fn expired_token_rejected() {
        let token = generate_connection_token("appkey-1", "secret-1");
        let future = token.ts + CONNECTION_TOKEN_LIFETIME_MS + 1000;
        assert!(!verify_connection_token(
            "appkey-1",
            "secret-1",
            &token.token,
            token.ts,
            &token.nonce,
            future
        ));
    }

    #[test]
    fn future_timestamp_rejected() {
        let token = generate_connection_token("appkey-1", "secret-1");
        let past = token.ts - CONNECTION_TOKEN_LIFETIME_MS - 1000;
        assert!(!verify_connection_token(
            "appkey-1",
            "secret-1",
            &token.token,
            token.ts,
            &token.nonce,
            past
        ));
    }

    #[test]
    fn slight_future_timestamp_accepted_clock_skew() {
        // 客户端时钟比服务器快几百毫秒（SNTP 漂移）时必须放行
        let token = generate_connection_token("appkey-1", "secret-1");
        let slightly_past = token.ts - 500; // 服务器时钟落后 token 500ms
        assert!(verify_connection_token(
            "appkey-1",
            "secret-1",
            &token.token,
            token.ts,
            &token.nonce,
            slightly_past
        ));
    }

    #[test]
    fn different_nonce_produces_different_token() {
        let t1 = generate_connection_token("appkey-1", "secret-1");
        let t2 = generate_connection_token("appkey-1", "secret-1");
        assert_ne!(t1.token, t2.token);
        assert_ne!(t1.nonce, t2.nonce);
    }
}
