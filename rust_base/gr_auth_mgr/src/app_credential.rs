//! 接入方凭据（appkey/app_secret）HMAC-SHA256 签名校验的纯逻辑。
//!
//! 服务端与三端客户端（PC/box/agent）共用同一算法：
//!   sign = hex(HMAC-SHA256(key=unhex(app_secret), "{appkey}\n{timestamp_ms}\n{body}"))
//! 请求头：x-app-key / x-app-timestamp / x-app-sign。
//! 时间戳窗口 ±5 分钟，防重放。

use ring::hmac;

/// 时间戳允许的最大偏差（±5 分钟）。
pub const TIMESTAMP_TOLERANCE_MS: i64 = 5 * 60 * 1000;

pub const HEADER_APP_KEY: &str = "x-app-key";
pub const HEADER_APP_TIMESTAMP: &str = "x-app-timestamp";
pub const HEADER_APP_SIGN: &str = "x-app-sign";

pub fn hex_decode(s: &str) -> Option<Vec<u8>> {
    let s = s.trim();
    if s.len() % 2 != 0 || !s.chars().all(|c| c.is_ascii_hexdigit()) {
        return None;
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok())
        .collect()
}

pub fn hex_encode(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn build_message(appkey: &str, timestamp_ms: i64, body: &[u8]) -> Vec<u8> {
    let mut msg = Vec::with_capacity(appkey.len() + 24 + body.len());
    msg.extend_from_slice(appkey.as_bytes());
    msg.push(b'\n');
    msg.extend_from_slice(timestamp_ms.to_string().as_bytes());
    msg.push(b'\n');
    msg.extend_from_slice(body);
    msg
}

/// 计算签名（客户端与服务端共用）。app_secret 为 hex 字符串。
pub fn sign(appkey: &str, app_secret: &str, timestamp_ms: i64, body: &[u8]) -> Option<String> {
    let key_bytes = hex_decode(app_secret)?;
    let key = hmac::Key::new(hmac::HMAC_SHA256, &key_bytes);
    let msg = build_message(appkey, timestamp_ms, body);
    Some(hex_encode(hmac::sign(&key, &msg).as_ref()))
}

/// 校验签名（服务端）。时间戳偏差超过窗口视为无效，比对为常数时间。
pub fn verify(
    appkey: &str,
    app_secret: &str,
    timestamp_ms: i64,
    body: &[u8],
    sign_hex: &str,
    now_ms: i64,
) -> bool {
    if (now_ms - timestamp_ms).abs() > TIMESTAMP_TOLERANCE_MS {
        return false;
    }
    let Some(key_bytes) = hex_decode(app_secret) else {
        return false;
    };
    let Some(given) = hex_decode(sign_hex) else {
        return false;
    };
    let key = hmac::Key::new(hmac::HMAC_SHA256, &key_bytes);
    let msg = build_message(appkey, timestamp_ms, body);
    hmac::verify(&key, &msg, &given).is_ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    const APPKEY: &str = "0123456789abcdef0123456789abcdef";
    const SECRET: &str = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";
    const TS: i64 = 1_700_000_000_000;
    const BODY: &[u8] = br#"{"a":1}"#;
    /// 跨语言固定测试向量（Rust 与 Java 必须一致）。
    const EXPECTED_SIGN: &str = "4c672c5835cf972edfa9d0946fe18bc517b24bfc635dbb2e387a3a219301203a";

    #[test]
    fn sign_matches_fixed_vector() {
        assert_eq!(sign(APPKEY, SECRET, TS, BODY).unwrap(), EXPECTED_SIGN);
    }

    #[test]
    fn verify_accepts_valid_signature() {
        assert!(verify(APPKEY, SECRET, TS, BODY, EXPECTED_SIGN, TS));
    }

    #[test]
    fn verify_rejects_wrong_signature() {
        let wrong = "00".repeat(32);
        assert!(!verify(APPKEY, SECRET, TS, BODY, &wrong, TS));
    }

    #[test]
    fn verify_rejects_expired_timestamp() {
        assert!(!verify(
            APPKEY,
            SECRET,
            TS,
            BODY,
            EXPECTED_SIGN,
            TS + TIMESTAMP_TOLERANCE_MS + 1
        ));
        assert!(verify(
            APPKEY,
            SECRET,
            TS,
            BODY,
            EXPECTED_SIGN,
            TS + TIMESTAMP_TOLERANCE_MS
        ));
    }

    #[test]
    fn verify_rejects_bad_hex_inputs() {
        assert!(!verify(APPKEY, "zz", TS, BODY, EXPECTED_SIGN, TS));
        assert!(!verify(APPKEY, SECRET, TS, BODY, "xyz", TS));
    }
}
