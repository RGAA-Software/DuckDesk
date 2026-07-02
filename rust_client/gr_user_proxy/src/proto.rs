use prost::Message as ProstMessage;

pub mod tcrp {
    include!(concat!(env!("OUT_DIR"), "/tcrp.rs"));
}

pub mod tc {
    include!(concat!(env!("OUT_DIR"), "/tc.rs"));
}

use tcrp::{
    RpClipboardFile, RpClipboardInfo, RpMessage, RpMessageType, RpRawRenderMessage, RpClipboardType,
};
use tc::{ClipboardFile, ClipboardInfo, ClipboardInfoResp, Message, MessageType, ClipboardType};

use crate::clipboard::content::ClipboardFileEntry;

pub const HEARTBEAT_INTERVAL_SECS: u64 = 30;

pub fn build_hello_message() -> Vec<u8> {
    let msg = RpMessage {
        r#type: RpMessageType::KRpHello as i32,
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn build_hello_resp_message() -> Vec<u8> {
    let msg = RpMessage {
        r#type: RpMessageType::KRpHelloResp as i32,
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn build_heartbeat_message() -> Vec<u8> {
    let msg = RpMessage {
        r#type: RpMessageType::KRpHeartBeat as i32,
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn build_clipboard_files_event(files: &[ClipboardFileEntry]) -> Vec<u8> {
    let msg = RpMessage {
        r#type: RpMessageType::KRpClipboardEvent as i32,
        clipboard_info: Some(RpClipboardInfo {
            r#type: RpClipboardType::KRpClipboardFiles as i32,
            files: files.iter().map(to_rp_clipboard_file).collect(),
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

fn to_rp_clipboard_file(file: &ClipboardFileEntry) -> RpClipboardFile {
    RpClipboardFile {
        file_name: file.file_name.clone(),
        full_path: file.full_path.clone(),
        total_size: file.total_size,
        ref_path: file.ref_path.clone(),
    }
}

fn to_tc_clipboard_file(file: &ClipboardFileEntry) -> ClipboardFile {
    ClipboardFile {
        file_name: file.file_name.clone(),
        full_path: file.full_path.clone(),
        total_size: file.total_size,
        ref_path: file.ref_path.clone(),
    }
}

pub fn clipboard_files_from_rp(msg: &RpMessage) -> Option<Vec<ClipboardFileEntry>> {
    let info = msg.clipboard_info.as_ref()?;
    if info.r#type != RpClipboardType::KRpClipboardFiles as i32 {
        return None;
    }
    Some(
        info.files
            .iter()
            .map(|file| ClipboardFileEntry {
                file_name: file.file_name.clone(),
                full_path: file.full_path.clone(),
                ref_path: file.ref_path.clone(),
                total_size: file.total_size,
            })
            .collect(),
    )
}

pub fn build_tc_clipboard_files(files: &[ClipboardFileEntry]) -> Vec<u8> {
    let msg = Message {
        r#type: MessageType::KClipboardInfo as i32,
        clipboard_info: Some(ClipboardInfo {
            r#type: ClipboardType::KClipboardFiles as i32,
            files: files.iter().map(to_tc_clipboard_file).collect(),
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn build_tc_clipboard_files_resp(files: &[ClipboardFileEntry]) -> Vec<u8> {
    let msg = Message {
        r#type: MessageType::KClipboardInfoResp as i32,
        clipboard_info_resp: Some(ClipboardInfoResp {
            r#type: ClipboardType::KClipboardFiles as i32,
            files: files.iter().map(to_tc_clipboard_file).collect(),
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn clipboard_files_from_tc(msg: &Message) -> Option<Vec<ClipboardFileEntry>> {
    let info = msg.clipboard_info.as_ref()?;
    if info.r#type != ClipboardType::KClipboardFiles as i32 {
        return None;
    }
    Some(
        info.files
            .iter()
            .map(|file| ClipboardFileEntry {
                file_name: file.file_name.clone(),
                full_path: file.full_path.clone(),
                ref_path: file.ref_path.clone(),
                total_size: file.total_size,
            })
            .collect(),
    )
}

pub fn build_clipboard_text_event(text: &str) -> Vec<u8> {
    let msg = RpMessage {
        r#type: RpMessageType::KRpClipboardEvent as i32,
        clipboard_info: Some(RpClipboardInfo {
            r#type: RpClipboardType::KRpClipboardText as i32,
            msg: text.as_bytes().to_vec(),
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn parse_rp_message(bytes: &[u8]) -> Result<RpMessage, prost::DecodeError> {
    RpMessage::decode(bytes)
}

pub fn clipboard_text_from_rp(msg: &RpMessage) -> Option<String> {
    let info = msg.clipboard_info.as_ref()?;
    if info.r#type != RpClipboardType::KRpClipboardText as i32 {
        return None;
    }
    Some(String::from_utf8_lossy(&info.msg).into_owned())
}

pub fn build_tc_clipboard_info(text: &str) -> Vec<u8> {
    let msg = Message {
        r#type: MessageType::KClipboardInfo as i32,
        clipboard_info: Some(ClipboardInfo {
            r#type: ClipboardType::KClipboardText as i32,
            msg: text.as_bytes().to_vec(),
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn build_tc_clipboard_info_resp(text: &str) -> Vec<u8> {
    let msg = Message {
        r#type: MessageType::KClipboardInfoResp as i32,
        clipboard_info_resp: Some(ClipboardInfoResp {
            r#type: ClipboardType::KClipboardText as i32,
            msg: text.as_bytes().to_vec(),
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn build_raw_render_message(inner: &[u8], data_channel: bool) -> Vec<u8> {
    let msg = RpMessage {
        r#type: RpMessageType::KRpRawRenderMessage as i32,
        raw_render_msg: Some(RpRawRenderMessage {
            msg: inner.to_vec(),
            data_channel,
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn parse_tc_message(bytes: &[u8]) -> Result<Message, prost::DecodeError> {
    Message::decode(bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use prost::Message as ProstMessage;

    #[test]
    fn rp_hello_roundtrip() {
        let bytes = build_hello_message();
        let parsed = parse_rp_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpHello as i32);
    }

    #[test]
    fn rp_hello_resp_roundtrip() {
        let bytes = build_hello_resp_message();
        let parsed = parse_rp_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpHelloResp as i32);
    }

    #[test]
    fn rp_heartbeat_roundtrip() {
        let hb = RpMessage {
            r#type: RpMessageType::KRpHeartBeat as i32,
            ..Default::default()
        };
        let parsed = parse_rp_message(&hb.encode_to_vec()).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpHeartBeat as i32);

        let resp = RpMessage {
            r#type: RpMessageType::KRpHeartBeatResp as i32,
            ..Default::default()
        };
        let parsed = parse_rp_message(&resp.encode_to_vec()).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpHeartBeatResp as i32);
    }

    #[test]
    fn rp_clipboard_text_event() {
        let bytes = build_clipboard_text_event("sync-me");
        let parsed = parse_rp_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpClipboardEvent as i32);
        assert_eq!(
            clipboard_text_from_rp(&parsed).as_deref(),
            Some("sync-me")
        );
    }

    #[test]
    fn rp_raw_render_message() {
        let inner = b"inner-payload";
        let bytes = build_raw_render_message(inner, false);
        let parsed = parse_rp_message(&bytes).expect("decode");
        let sub = parsed.raw_render_msg.expect("raw");
        assert_eq!(sub.msg, inner);
        assert!(!sub.data_channel);
    }

    #[test]
    fn tc_clipboard_info_roundtrip() {
        let bytes = build_tc_clipboard_info("host-text");
        let parsed = parse_tc_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, MessageType::KClipboardInfo as i32);
        let info = parsed.clipboard_info.expect("info");
        assert_eq!(info.r#type, ClipboardType::KClipboardText as i32);
        assert_eq!(String::from_utf8_lossy(&info.msg), "host-text");
    }

    #[test]
    fn tc_clipboard_info_resp_roundtrip() {
        let bytes = build_tc_clipboard_info_resp("resp-text");
        let parsed = parse_tc_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, MessageType::KClipboardInfoResp as i32);
    }

    #[test]
    fn nested_raw_render_carries_tc_message() {
        let inner = build_tc_clipboard_info("nested");
        let outer = build_raw_render_message(&inner, false);
        let rp = parse_rp_message(&outer).expect("decode rp");
        let raw = rp.raw_render_msg.expect("raw").msg;
        let tc = parse_tc_message(&raw).expect("decode tc");
        let info = tc.clipboard_info.expect("info");
        assert_eq!(String::from_utf8_lossy(&info.msg), "nested");
    }

    #[test]
    fn unknown_bytes_fails_gracefully() {
        assert!(parse_rp_message(&[0xFF, 0x01]).is_err());
        assert!(parse_tc_message(&[0xAA]).is_err());
    }

    #[test]
    fn rp_clipboard_files_event() {
        let files = vec![crate::clipboard::content::ClipboardFileEntry {
            file_name: "a.txt".to_string(),
            full_path: "D:/a.txt".to_string(),
            ref_path: "a.txt".to_string(),
            total_size: 10,
        }];
        let bytes = build_clipboard_files_event(&files);
        let parsed = parse_rp_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, RpMessageType::KRpClipboardEvent as i32);
        assert_eq!(clipboard_files_from_rp(&parsed).expect("files").len(), 1);
    }
}
