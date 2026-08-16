use prost::Message as ProstMessage;

pub mod pxrp {
    include!(concat!(env!("OUT_DIR"), "/pxrp.rs"));
}

pub mod px {
    include!(concat!(env!("OUT_DIR"), "/px.rs"));
}

use pxrp::{
    RpClipboardFile, RpClipboardInfo, RpMessage, RpMessageType, RpRawRenderMessage, RpClipboardType,
};
use px::{
    ClipboardFile, ClipboardInfo, ClipboardInfoResp, ClipboardReqAtBegin, ClipboardReqAtEnd,
    ClipboardReqBuffer, ClipboardRespBuffer, Message, MessageType, ClipboardType,
};

use crate::clipboard::content::ClipboardFileEntry;
use crate::clipboard::virtual_file::stream::{ReadChunkRequest, RespBufferData};

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct StreamRoute {
    pub stream_id: String,
    pub device_id: String,
}

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

fn to_px_clipboard_file(file: &ClipboardFileEntry) -> ClipboardFile {
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

pub fn build_px_clipboard_files(files: &[ClipboardFileEntry]) -> Vec<u8> {
    let msg = Message {
        r#type: MessageType::KClipboardInfo as i32,
        clipboard_info: Some(ClipboardInfo {
            r#type: ClipboardType::KClipboardFiles as i32,
            files: files.iter().map(to_px_clipboard_file).collect(),
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn build_px_clipboard_files_resp(files: &[ClipboardFileEntry]) -> Vec<u8> {
    let msg = Message {
        r#type: MessageType::KClipboardInfoResp as i32,
        clipboard_info_resp: Some(ClipboardInfoResp {
            r#type: ClipboardType::KClipboardFiles as i32,
            files: files.iter().map(to_px_clipboard_file).collect(),
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

pub fn clipboard_files_from_px(msg: &Message) -> Option<Vec<ClipboardFileEntry>> {
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

pub fn build_px_clipboard_info(text: &str) -> Vec<u8> {
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

pub fn build_px_clipboard_info_resp(text: &str) -> Vec<u8> {
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
    build_raw_render_message_routed(inner, data_channel, None)
}

pub fn build_raw_render_message_routed(
    inner: &[u8],
    data_channel: bool,
    route: Option<&StreamRoute>,
) -> Vec<u8> {
    let (stream_id, device_id) = route
        .map(|route| (route.stream_id.clone(), route.device_id.clone()))
        .unwrap_or_default();
    let msg = RpMessage {
        r#type: RpMessageType::KRpRawRenderMessage as i32,
        raw_render_msg: Some(RpRawRenderMessage {
            msg: inner.to_vec(),
            data_channel,
            stream_id,
            device_id,
            ..Default::default()
        }),
        ..Default::default()
    };
    msg.encode_to_vec()
}

fn build_px_message_routed(
    msg_type: MessageType,
    route: &StreamRoute,
    fill: impl FnOnce(&mut Message),
) -> Vec<u8> {
    let mut msg = Message {
        r#type: msg_type as i32,
        stream_id: route.stream_id.clone(),
        device_id: route.device_id.clone(),
        ..Default::default()
    };
    fill(&mut msg);
    msg.encode_to_vec()
}

pub fn build_px_req_buffer(req: &ReadChunkRequest, route: &StreamRoute) -> Vec<u8> {
    let inner = build_px_message_routed(MessageType::KClipboardReqBuffer, route, |msg| {
        msg.cp_req_buffer = Some(ClipboardReqBuffer {
            full_name: req.full_name.clone(),
            req_size: req.req_size,
            req_start: req.req_start,
            req_index: req.req_index,
        });
    });
    build_raw_render_message_routed(&inner, true, Some(route))
}

pub fn build_px_req_at_begin(full_name: &str, route: &StreamRoute) -> Vec<u8> {
    let inner = build_px_message_routed(MessageType::KClipboardReqAtBegin, route, |msg| {
        msg.cp_req_at_begin = Some(ClipboardReqAtBegin {
            full_name: full_name.to_string(),
        });
    });
    build_raw_render_message_routed(&inner, true, Some(route))
}

pub fn build_px_req_at_end(full_name: &str, success: bool, route: &StreamRoute) -> Vec<u8> {
    let inner = build_px_message_routed(MessageType::KClipboardReqAtEnd, route, |msg| {
        msg.cp_req_at_end = Some(ClipboardReqAtEnd {
            full_name: full_name.to_string(),
            success,
        });
    });
    build_raw_render_message_routed(&inner, true, Some(route))
}

pub fn build_px_resp_buffer(resp: &RespBufferData, route: &StreamRoute) -> Vec<u8> {
    build_px_message_routed(MessageType::KClipboardRespBuffer, route, |msg| {
        msg.cp_resp_buffer = Some(ClipboardRespBuffer {
            full_name: resp.full_name.clone(),
            req_size: resp.req_size,
            req_start: resp.req_start,
            req_index: resp.req_index,
            read_size: resp.read_size,
            buffer: resp.buffer.clone(),
        });
    })
}

pub fn clipboard_resp_buffer_from_px(msg: &Message) -> Option<RespBufferData> {
    if msg.r#type != MessageType::KClipboardRespBuffer as i32 {
        return None;
    }
    let resp = msg.cp_resp_buffer.as_ref()?;
    Some(RespBufferData {
        full_name: resp.full_name.clone(),
        req_index: resp.req_index,
        req_start: resp.req_start,
        req_size: resp.req_size,
        read_size: resp.read_size,
        buffer: resp.buffer.clone(),
    })
}

pub fn stream_route_from_px(msg: &Message) -> StreamRoute {
    StreamRoute {
        stream_id: msg.stream_id.clone(),
        device_id: msg.device_id.clone(),
    }
}

pub fn stream_route_from_rp_raw(sub: &RpRawRenderMessage) -> StreamRoute {
    StreamRoute {
        stream_id: sub.stream_id.clone(),
        device_id: sub.device_id.clone(),
    }
}

pub fn parse_px_message(bytes: &[u8]) -> Result<Message, prost::DecodeError> {
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
    fn px_clipboard_info_roundtrip() {
        let bytes = build_px_clipboard_info("host-text");
        let parsed = parse_px_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, MessageType::KClipboardInfo as i32);
        let info = parsed.clipboard_info.expect("info");
        assert_eq!(info.r#type, ClipboardType::KClipboardText as i32);
        assert_eq!(String::from_utf8_lossy(&info.msg), "host-text");
    }

    #[test]
    fn px_clipboard_info_resp_roundtrip() {
        let bytes = build_px_clipboard_info_resp("resp-text");
        let parsed = parse_px_message(&bytes).expect("decode");
        assert_eq!(parsed.r#type, MessageType::KClipboardInfoResp as i32);
    }

    #[test]
    fn nested_raw_render_carries_px_message() {
        let inner = build_px_clipboard_info("nested");
        let outer = build_raw_render_message(&inner, false);
        let rp = parse_rp_message(&outer).expect("decode rp");
        let raw = rp.raw_render_msg.expect("raw").msg;
        let msg = parse_px_message(&raw).expect("decode msg");
        let info = msg.clipboard_info.expect("info");
        assert_eq!(String::from_utf8_lossy(&info.msg), "nested");
    }

    #[test]
    fn unknown_bytes_fails_gracefully() {
        assert!(parse_rp_message(&[0xFF, 0x01]).is_err());
        assert!(parse_px_message(&[0xAA]).is_err());
    }

    #[test]
    fn raw_render_message_carries_route() {
        let route = StreamRoute {
            stream_id: "s1".to_string(),
            device_id: "d1".to_string(),
        };
        let bytes = build_raw_render_message_routed(b"payload", true, Some(&route));
        let parsed = parse_rp_message(&bytes).expect("decode");
        let sub = parsed.raw_render_msg.expect("raw");
        assert!(sub.data_channel);
        assert_eq!(sub.stream_id, "s1");
        assert_eq!(sub.device_id, "d1");
    }

    #[test]
    fn px_req_buffer_roundtrip() {
        let route = StreamRoute {
            stream_id: "stream-a".to_string(),
            device_id: "device-a".to_string(),
        };
        let req = ReadChunkRequest {
            full_name: "C:/x.bin".to_string(),
            req_index: 2,
            req_start: 100,
            req_size: 4096,
        };
        let outer = build_px_req_buffer(&req, &route);
        let rp = parse_rp_message(&outer).expect("rp");
        let sub = rp.raw_render_msg.expect("raw");
        assert!(sub.data_channel);
        assert_eq!(sub.stream_id, "stream-a");
        let msg = parse_px_message(&sub.msg).expect("msg");
        assert_eq!(msg.stream_id, "stream-a");
        assert_eq!(msg.device_id, "device-a");
        let buf = msg.cp_req_buffer.expect("req");
        assert_eq!(buf.full_name, "C:/x.bin");
        assert_eq!(buf.req_index, 2);
        assert_eq!(buf.req_start, 100);
        assert_eq!(buf.req_size, 4096);
    }

    #[test]
    fn px_resp_buffer_roundtrip() {
        let route = StreamRoute {
            stream_id: "s".to_string(),
            device_id: "d".to_string(),
        };
        let resp = RespBufferData {
            full_name: "f".to_string(),
            req_index: 0,
            req_start: 0,
            req_size: 4,
            read_size: 4,
            buffer: b"data".to_vec(),
        };
        let bytes = build_px_resp_buffer(&resp, &route);
        let msg = parse_px_message(&bytes).expect("msg");
        let parsed = clipboard_resp_buffer_from_px(&msg).expect("resp");
        assert_eq!(parsed, resp);
    }

    #[test]
    fn px_req_at_begin_end_roundtrip() {
        let route = StreamRoute {
            stream_id: "s".to_string(),
            device_id: "d".to_string(),
        };
        let begin_outer = build_px_req_at_begin("C:/a.txt", &route);
        let begin_rp = parse_rp_message(&begin_outer).expect("rp");
        let begin_msg =
            parse_px_message(&begin_rp.raw_render_msg.expect("raw").msg).expect("msg");
        assert_eq!(
            begin_msg.r#type,
            MessageType::KClipboardReqAtBegin as i32
        );
        assert_eq!(
            begin_msg.cp_req_at_begin.expect("begin").full_name,
            "C:/a.txt"
        );

        let end_outer = build_px_req_at_end("C:/a.txt", true, &route);
        let end_rp = parse_rp_message(&end_outer).expect("rp");
        let end_msg = parse_px_message(&end_rp.raw_render_msg.expect("raw").msg).expect("msg");
        assert_eq!(end_msg.r#type, MessageType::KClipboardReqAtEnd as i32);
        let end = end_msg.cp_req_at_end.expect("end");
        assert_eq!(end.full_name, "C:/a.txt");
        assert!(end.success);
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
