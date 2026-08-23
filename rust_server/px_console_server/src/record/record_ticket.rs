//! Ticket signing for the panel `/records` api (design doc
//! docs/console_render_records_view_design.md section 5.3).
//!
//! Must stay byte-compatible with the panel side verifier
//! (src/px_panel/src/render_panel/network/records_ticket.cpp):
//!   message = "{device_id}|{filename_or_*}|{exp}"   (exp = unix seconds)
//!   key     = device.safety_pwd_md5                (md5 hex string, as bytes)
//!   tk      = lowercase hex of HMAC-SHA256(key, message)
//!
//! Pinned vectors (also asserted by the panel gtest
//! test_records_ticket.cpp / CrossSidePinnedVector):
//!   key=0123456789abcdef0123456789abcdef dev123|rec_A_20260817_10.30.00.mp4|1700000000
//!     -> d80a475bd9e12baab82e41b8b6eb080e23c4b60d87e5bfb33f7b3ed98955166d
//!   key=0123456789abcdef0123456789abcdef dev123|*|1700000000
//!     -> 09931dc9fa11e5a1b462e41b6821568fcd1ee5c00e4f656e16e00b2a14e06469

use ring::hmac;

/// ticket lifetime: 10 minutes
pub const RECORD_TICKET_TTL_SECS: i64 = 600;

pub fn hmac_sha256_hex(key: &str, message: &str) -> String {
    let key = hmac::Key::new(hmac::HMAC_SHA256, key.as_bytes());
    let tag = hmac::sign(&key, message.as_bytes());
    let mut out = String::with_capacity(tag.as_ref().len() * 2);
    for b in tag.as_ref() {
        out.push_str(&format!("{:02x}", b));
    }
    out
}

/// ticket_key = the device's safety_pwd_md5 stored in the device table.
/// `file` may be "*" (covers list / info requests).
pub fn sign_record_ticket(device_id: &str, file: &str, exp_unix: i64, ticket_key: &str) -> String {
    let message = format!("{}|{}|{}", device_id, file, exp_unix);
    hmac_sha256_hex(ticket_key, &message)
}

/// exp value (unix seconds) for a ticket issued now.
pub fn make_ticket_exp() -> i64 {
    px_base::get_current_timestamp() / 1000 + RECORD_TICKET_TTL_SECS
}

#[cfg(test)]
mod tests {
    use super::*;

    // RFC 4231 test case 2 to pin the ring::hmac usage
    #[test]
    fn hmac_sha256_rfc4231_case2() {
        let hex = hmac_sha256_hex("Jefe", "what do ya want for nothing?");
        assert_eq!(
            hex,
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
        );
    }

    #[test]
    fn ticket_format_matches_panel_contract() {
        // message = device_id|file|exp, key = safety_pwd_md5 (as bytes)
        let tk = sign_record_ticket(
            "dev123",
            "rec_A_20260817_10.30.00.mp4",
            1700000000,
            "0123456789abcdef0123456789abcdef",
        );
        assert_eq!(tk.len(), 64);
        assert!(tk
            .chars()
            .all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase()));
        // deterministic
        assert_eq!(
            tk,
            sign_record_ticket(
                "dev123",
                "rec_A_20260817_10.30.00.mp4",
                1700000000,
                "0123456789abcdef0123456789abcdef"
            )
        );
        // different file -> different ticket
        assert_ne!(
            tk,
            sign_record_ticket(
                "dev123",
                "*",
                1700000000,
                "0123456789abcdef0123456789abcdef"
            )
        );
    }

    /// pinned vectors shared with the panel gtest (CrossSidePinnedVector);
    /// if either side changes the byte format, both tests fail
    #[test]
    fn cross_side_pinned_vector() {
        let key = "0123456789abcdef0123456789abcdef";
        assert_eq!(
            sign_record_ticket("dev123", "rec_A_20260817_10.30.00.mp4", 1700000000, key),
            "d80a475bd9e12baab82e41b8b6eb080e23c4b60d87e5bfb33f7b3ed98955166d"
        );
        assert_eq!(
            sign_record_ticket("dev123", "*", 1700000000, key),
            "09931dc9fa11e5a1b462e41b6821568fcd1ee5c00e4f656e16e00b2a14e06469"
        );
    }

    #[test]
    fn exp_is_unix_seconds_in_the_future() {
        let exp = make_ticket_exp();
        let now_secs = px_base::get_current_timestamp() / 1000;
        assert!(exp >= now_secs + RECORD_TICKET_TTL_SECS - 1);
        assert!(exp <= now_secs + RECORD_TICKET_TTL_SECS + 1);
    }
}
