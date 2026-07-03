use std::collections::HashMap;
use std::sync::mpsc::Sender;
use std::sync::{Arc, Mutex};

use tracing::{info, warn};

use crate::clipboard::content::{files_signature, ClipboardFileEntry};
use crate::clipboard::virtual_file::stream::{ReadChunkRequest, RespBufferData, VirtualFileStreamCore};
use crate::proto::{self, StreamRoute};

#[derive(Debug, Clone)]
pub struct VirtualFileSession {
    pub route: StreamRoute,
    pub files: Vec<ClipboardFileEntry>,
}

/// Bridges COM `IStream::Read` (sync) and tokio websocket (async).
pub struct VirtualFileCoordinator {
    session: Mutex<Option<VirtualFileSession>>,
    streams: Mutex<HashMap<u32, Arc<VirtualFileStreamCore>>>,
    active_stream: Mutex<Option<Arc<VirtualFileStreamCore>>>,
    outbound_tx: Mutex<Option<Sender<Vec<u8>>>>,
}

impl VirtualFileCoordinator {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            session: Mutex::new(None),
            streams: Mutex::new(HashMap::new()),
            active_stream: Mutex::new(None),
            outbound_tx: Mutex::new(None),
        })
    }

    pub fn set_outbound_sender(&self, tx: Sender<Vec<u8>>) {
        *self.outbound_tx.lock().expect("lock") = Some(tx);
    }

    pub fn install_session(&self, session: VirtualFileSession) {
        let mut streams = self.streams.lock().expect("lock");
        streams.clear();
        for (index, file) in session.files.iter().enumerate() {
            streams.insert(
                index as u32,
                Arc::new(VirtualFileStreamCore::new(file.clone())),
            );
        }
        *self.session.lock().expect("lock") = Some(session);
        *self.active_stream.lock().expect("lock") = None;
        info!(
            "virtual file session installed, count={}",
            streams.len()
        );
    }

    pub fn session_files(&self) -> Option<Vec<ClipboardFileEntry>> {
        self.session
            .lock()
            .expect("lock")
            .as_ref()
            .map(|session| session.files.clone())
    }

    pub fn session_route(&self) -> Option<StreamRoute> {
        self.session
            .lock()
            .expect("lock")
            .as_ref()
            .map(|session| session.route.clone())
    }

    pub fn files_signature(&self) -> Option<String> {
        self.session_files().map(|files| files_signature(&files))
    }

    pub fn stream_for_index(&self, index: u32) -> Option<Arc<VirtualFileStreamCore>> {
        self.streams.lock().expect("lock").get(&index).cloned()
    }

    pub fn activate_stream(&self, index: u32) -> Option<Arc<VirtualFileStreamCore>> {
        let stream = self.stream_for_index(index)?;
        *self.active_stream.lock().expect("lock") = Some(stream.clone());
        Some(stream)
    }

    pub fn active_stream(&self) -> Option<Arc<VirtualFileStreamCore>> {
        self.active_stream.lock().expect("lock").clone()
    }

    pub fn clear_active_stream(&self) {
        *self.active_stream.lock().expect("lock") = None;
    }

    pub fn on_resp_buffer(&self, resp: RespBufferData) -> bool {
        if let Some(stream) = self.active_stream() {
            if stream.on_resp_buffer(resp.clone()) {
                return true;
            }
        }
        for stream in self.streams.lock().expect("lock").values() {
            if stream.file().full_path == resp.full_name && stream.on_resp_buffer(resp.clone()) {
                return true;
            }
        }
        warn!("virtual file resp buffer with no matching stream, full_name={}", resp.full_name);
        false
    }

    pub fn send_req_buffer(&self, req: &ReadChunkRequest) -> anyhow::Result<()> {
        let route = self
            .session_route()
            .ok_or_else(|| anyhow::anyhow!("virtual file session missing"))?;
        let bytes = proto::build_tc_req_buffer(req, &route);
        self.send_outbound(bytes)
    }

    pub fn send_req_at_begin(&self, full_name: &str) -> anyhow::Result<()> {
        let route = self
            .session_route()
            .ok_or_else(|| anyhow::anyhow!("virtual file session missing"))?;
        self.send_outbound(proto::build_tc_req_at_begin(full_name, &route))
    }

    pub fn send_req_at_end(&self, full_name: &str, success: bool) -> anyhow::Result<()> {
        let route = self
            .session_route()
            .ok_or_else(|| anyhow::anyhow!("virtual file session missing"))?;
        self.send_outbound(proto::build_tc_req_at_end(full_name, success, &route))
    }

    fn send_outbound(&self, bytes: Vec<u8>) -> anyhow::Result<()> {
        let tx = self
            .outbound_tx
            .lock()
            .expect("lock")
            .clone()
            .ok_or_else(|| anyhow::anyhow!("virtual file outbound sender not configured"))?;
        tx.send(bytes)
            .map_err(|_| anyhow::anyhow!("virtual file outbound channel closed"))
    }

    pub fn clear_session(&self) {
        *self.session.lock().expect("lock") = None;
        self.streams.lock().expect("lock").clear();
        self.clear_active_stream();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::mpsc;
    use std::time::Duration;

    fn sample_session() -> VirtualFileSession {
        VirtualFileSession {
            route: StreamRoute {
                stream_id: "stream-1".to_string(),
                device_id: "device-1".to_string(),
            },
            files: vec![ClipboardFileEntry {
                file_name: "a.txt".to_string(),
                full_path: "Z:/missing/a.txt".to_string(),
                ref_path: "a.txt".to_string(),
                total_size: 5,
            }],
        }
    }

    #[test]
    fn install_session_exposes_streams() {
        let coord = VirtualFileCoordinator::new();
        coord.install_session(sample_session());
        assert_eq!(coord.session_files().expect("files").len(), 1);
        assert!(coord.stream_for_index(0).is_some());
    }

    #[test]
    fn outbound_req_buffer_wraps_data_channel() {
        let coord = VirtualFileCoordinator::new();
        let (tx, rx) = mpsc::channel();
        coord.set_outbound_sender(tx);
        coord.install_session(sample_session());

        let stream = coord.stream_for_index(0).expect("stream");
        let req = stream.begin_read(1024).expect("begin");
        coord.send_req_buffer(&req).expect("send");

        let bytes = rx.recv_timeout(Duration::from_secs(1)).expect("recv");
        let rp = proto::parse_rp_message(&bytes).expect("rp");
        let sub = rp.raw_render_msg.expect("raw");
        assert!(sub.data_channel);
        assert_eq!(sub.stream_id, "stream-1");
        assert_eq!(sub.device_id, "device-1");

        let tc = proto::parse_tc_message(&sub.msg).expect("tc");
        assert_eq!(
            tc.r#type,
            proto::tc::MessageType::KClipboardReqBuffer as i32
        );
        let req_buf = tc.cp_req_buffer.expect("req");
        assert_eq!(req_buf.full_name, "Z:/missing/a.txt");
        assert_eq!(req_buf.req_index, 0);
    }

    #[test]
    fn resp_buffer_routes_to_active_stream() {
        let coord = VirtualFileCoordinator::new();
        coord.install_session(sample_session());
        let stream = coord.activate_stream(0).expect("activate");
        let req = stream.begin_read(3).expect("begin");

        assert!(coord.on_resp_buffer(RespBufferData {
            full_name: "Z:/missing/a.txt".to_string(),
            req_index: req.req_index,
            req_start: 0,
            req_size: 3,
            read_size: 3,
            buffer: b"abc".to_vec(),
        }));

        let mut buf = [0u8; 3];
        let n = stream.complete_read(&mut buf).expect("read");
        assert_eq!(n, 3);
        assert_eq!(&buf, b"abc");
    }
}
