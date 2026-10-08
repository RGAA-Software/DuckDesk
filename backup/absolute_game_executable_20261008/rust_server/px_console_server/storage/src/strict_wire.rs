//! Internally tagged unit variants otherwise discard their remaining object fields.
//! Decode their payload as a strict empty struct, preserving duplicate-key detection.
use serde::{Deserialize, Deserializer};
pub(crate) fn empty<'de, D: Deserializer<'de>>(deserializer: D) -> Result<(), D::Error> {
    #[derive(Deserialize)]
    #[serde(deny_unknown_fields)]
    struct Empty {}
    Empty::deserialize(deserializer).map(|_| ())
}
#[cfg(test)]
mod tests {
    use crate::{
        ApplicationLaunch, ChannelOutcome, CommandOutcome, DeploymentTarget, PreparationState,
        TransferOutcome,
    };
    fn check<T: serde::de::DeserializeOwned>(tag: &str, value: &str) {
        let valid = format!(r#"{{"{tag}":"{value}"}}"#);
        assert!(serde_json::from_str::<T>(&valid).is_ok(), "{valid}");
        for field in [
            "unexpected",
            "video",
            "install_root",
            "port",
            "reason",
            "received_sha256",
        ] {
            let invalid = format!(r#"{{"{tag}":"{value}","{field}":null}}"#);
            assert!(serde_json::from_str::<T>(&invalid).is_err(), "{invalid}");
        }
        let duplicate = format!(r#"{{"{tag}":"{value}","{tag}":"{value}"}}"#);
        assert!(serde_json::from_str::<T>(&duplicate).is_err());
    }
    #[test]
    fn all_empty_tagged_payloads_reject_extra_and_duplicate_fields() {
        check::<ApplicationLaunch>("kind", "rdp");
        check::<DeploymentTarget>("kind", "rdp");
        check::<DeploymentTarget>("kind", "webview");
        check::<PreparationState>("state", "pending");
        check::<PreparationState>("state", "ready");
        check::<CommandOutcome>("result", "absent");
        check::<CommandOutcome>("result", "unknown");
        check::<TransferOutcome>("kind", "progress");
        check::<TransferOutcome>("kind", "cancelled");
        check::<ChannelOutcome>("kind", "progress");
    }
}
