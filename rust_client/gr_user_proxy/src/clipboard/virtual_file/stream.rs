use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Condvar, Mutex};
use std::time::Duration;

use crate::clipboard::content::ClipboardFileEntry;

/// Parsed `ClipboardRespBuffer` payload used by the virtual-file stream core.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RespBufferData {
    pub full_name: String,
    pub req_index: i64,
    pub req_start: i64,
    pub req_size: i64,
    pub read_size: i64,
    pub buffer: Vec<u8>,
}

/// Outbound read request matching `ClipboardReqBuffer`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ReadChunkRequest {
    pub full_name: String,
    pub req_index: i64,
    pub req_start: i64,
    pub req_size: i64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum StreamError {
    Exited,
    Timeout,
    IndexMismatch { expected: i64, got: i64 },
    InvalidResponse,
}

struct StreamState {
    req_index: i64,
    current_position: i64,
    pending_resp: Option<RespBufferData>,
}

/// SCTP data-channel `max-message-size` is 256 KiB (262144). The client sends the
/// resp buffer as a single message (no split), so a 256 KiB chunk plus the TLV
/// header + protobuf metadata exceeds that limit and libwebrtc closes the channel
/// with `send_result = INVALID_RANGE`. Cap each request well below the limit; the
/// shell re-issues `IStream::Read` for any remaining bytes.
const MAX_READ_CHUNK_SIZE: u32 = 128 * 1024;

/// Port of `CpFileStream` read/wait logic without COM.
///
/// Uses interior mutability so `on_resp_buffer` (websocket thread) can unblock
/// `complete_read` (clipboard STA / COM thread) without deadlocking on an outer
/// `Arc<Mutex<_>>` wrapper.
pub struct VirtualFileStreamCore {
    file: ClipboardFileEntry,
    state: Mutex<StreamState>,
    resp_ready: Condvar,
    exit: AtomicBool,
    read_timeout: Duration,
}

impl VirtualFileStreamCore {
    pub fn new(file: ClipboardFileEntry) -> Self {
        Self {
            file,
            state: Mutex::new(StreamState {
                req_index: 0,
                current_position: 0,
                pending_resp: None,
            }),
            resp_ready: Condvar::new(),
            exit: AtomicBool::new(false),
            read_timeout: Duration::from_secs(60),
        }
    }

    pub fn with_read_timeout(mut self, timeout: Duration) -> Self {
        self.read_timeout = timeout;
        self
    }

    pub fn file(&self) -> &ClipboardFileEntry {
        &self.file
    }

    pub fn req_index(&self) -> i64 {
        self.state.lock().expect("lock").req_index
    }

    pub fn current_position(&self) -> i64 {
        self.state.lock().expect("lock").current_position
    }

    pub fn exit(&self) {
        self.exit.store(true, Ordering::Relaxed);
        self.resp_ready.notify_all();
    }

    pub fn is_exited(&self) -> bool {
        self.exit.load(Ordering::Relaxed)
    }

    pub fn reset_position(&self) {
        self.state.lock().expect("lock").current_position = 0;
    }

    pub fn is_transfer_complete(&self) -> bool {
        let state = self.state.lock().expect("lock");
        self.file.total_size > 0 && state.current_position >= self.file.total_size
    }

    /// Build the next `ClipboardReqBuffer` for a synchronous read of `cb` bytes.
    pub fn begin_read(&self, cb: u32) -> Result<ReadChunkRequest, StreamError> {
        if self.is_exited() {
            return Err(StreamError::Exited);
        }
        let state = self.state.lock().expect("lock");
        Ok(ReadChunkRequest {
            full_name: self.file.full_path.clone(),
            req_index: state.req_index,
            req_start: state.current_position,
            req_size: cb.min(MAX_READ_CHUNK_SIZE) as i64,
        })
    }

    /// Apply a remote `ClipboardRespBuffer` and unblock a waiting `complete_read`.
    pub fn on_resp_buffer(&self, resp: RespBufferData) -> bool {
        if self.is_exited() {
            return false;
        }
        let mut guard = self.state.lock().expect("lock");
        guard.pending_resp = Some(resp);
        drop(guard);
        self.resp_ready.notify_all();
        true
    }

    /// Wait for response and copy into `dest`. Returns bytes copied.
    pub fn complete_read(&self, dest: &mut [u8]) -> Result<usize, StreamError> {
        if self.is_exited() {
            return Err(StreamError::Exited);
        }

        let resp = {
            let mut guard = self.state.lock().expect("lock");
            let deadline = std::time::Instant::now() + self.read_timeout;
            while guard.pending_resp.is_none() && !self.is_exited() {
                let remaining = deadline.saturating_duration_since(std::time::Instant::now());
                if remaining.is_zero() {
                    return Err(StreamError::Timeout);
                }
                let slice = remaining.min(Duration::from_millis(50));
                guard = self
                    .resp_ready
                    .wait_timeout(guard, slice)
                    .expect("wait")
                    .0;
                #[cfg(windows)]
                crate::clipboard::win_platform::pump_sta_messages();
            }
            if self.is_exited() {
                return Err(StreamError::Exited);
            }
            guard.pending_resp.take().ok_or(StreamError::Timeout)?
        };

        let mut state = self.state.lock().expect("lock");
        if resp.req_index != state.req_index {
            return Err(StreamError::IndexMismatch {
                expected: state.req_index,
                got: resp.req_index,
            });
        }

        let read_size = resp.read_size.max(0) as usize;
        if read_size > dest.len() || read_size > resp.buffer.len() {
            return Err(StreamError::InvalidResponse);
        }

        if read_size > 0 {
            dest[..read_size].copy_from_slice(&resp.buffer[..read_size]);
            state.current_position += read_size as i64;
        }

        state.req_index += 1;
        Ok(read_size)
    }

    /// Single-shot read used by tests.
    pub fn read_chunk(&self, dest: &mut [u8]) -> Result<usize, StreamError> {
        let _req = self.begin_read(dest.len() as u32)?;
        self.complete_read(dest)
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;
    use std::time::Duration;

    use super::*;

    fn sample_file() -> ClipboardFileEntry {
        ClipboardFileEntry {
            file_name: "doc.txt".to_string(),
            full_path: "C:/remote/doc.txt".to_string(),
            ref_path: "doc.txt".to_string(),
            total_size: 12,
        }
    }

    fn sample_resp(index: i64, data: &[u8]) -> RespBufferData {
        RespBufferData {
            full_name: "C:/remote/doc.txt".to_string(),
            req_index: index,
            req_start: 0,
            req_size: data.len() as i64,
            read_size: data.len() as i64,
            buffer: data.to_vec(),
        }
    }

    #[test]
    fn begin_read_uses_current_position_and_index() {
        let core = VirtualFileStreamCore::new(sample_file());
        let req = core.begin_read(4096).expect("begin");
        assert_eq!(req.req_index, 0);
        assert_eq!(req.req_start, 0);
        assert_eq!(req.req_size, 4096);
        assert_eq!(req.full_name, "C:/remote/doc.txt");
    }

    #[test]
    fn complete_read_copies_and_advances() {
        let core = VirtualFileStreamCore::new(sample_file());
        let req = core.begin_read(5).expect("begin");
        assert!(core.on_resp_buffer(sample_resp(req.req_index, b"hello")));
        let mut buf = [0u8; 8];
        let n = core.complete_read(&mut buf).expect("read");
        assert_eq!(n, 5);
        assert_eq!(&buf[..5], b"hello");
        assert_eq!(core.current_position(), 5);
        assert_eq!(core.req_index(), 1);
    }

    #[test]
    fn complete_read_unblocks_from_other_thread() {
        let core = Arc::new(VirtualFileStreamCore::new(sample_file()));
        let worker = core.clone();
        let req = core.begin_read(4).expect("begin");
        let handle = std::thread::spawn(move || {
            std::thread::sleep(Duration::from_millis(20));
            assert!(worker.on_resp_buffer(sample_resp(req.req_index, b"abcd")));
        });
        let mut buf = [0u8; 4];
        let n = core.complete_read(&mut buf).expect("read");
        assert_eq!(n, 4);
        handle.join().expect("join");
    }

    #[test]
    fn multi_chunk_sequential_reads() {
        let core = VirtualFileStreamCore::new(sample_file());
        for (chunk, expected) in [(&b"hello "[..], 6), (&b"world!"[..], 6)] {
            let req = core.begin_read(64).expect("begin");
            assert!(core.on_resp_buffer(sample_resp(req.req_index, chunk)));
            let mut buf = [0u8; 8];
            let n = core.complete_read(&mut buf).expect("read");
            assert_eq!(n, expected);
            assert_eq!(&buf[..n], chunk);
        }
        assert!(core.is_transfer_complete());
    }

    #[test]
    fn index_mismatch_fails() {
        let core = VirtualFileStreamCore::new(sample_file());
        let _req = core.begin_read(4).expect("begin");
        assert!(core.on_resp_buffer(sample_resp(99, b"bad!")));
        let mut buf = [0u8; 4];
        assert!(matches!(
            core.complete_read(&mut buf),
            Err(StreamError::IndexMismatch { expected: 0, got: 99 })
        ));
    }

    #[test]
    fn exit_unblocks_waiters() {
        let core =
            VirtualFileStreamCore::new(sample_file()).with_read_timeout(Duration::from_millis(50));
        let _req = core.begin_read(4).expect("begin");
        core.exit();
        let mut buf = [0u8; 4];
        assert!(matches!(core.complete_read(&mut buf), Err(StreamError::Exited)));
    }

    #[test]
    fn timeout_when_no_response() {
        let core =
            VirtualFileStreamCore::new(sample_file()).with_read_timeout(Duration::from_millis(30));
        let _req = core.begin_read(4).expect("begin");
        let mut buf = [0u8; 4];
        assert!(matches!(core.complete_read(&mut buf), Err(StreamError::Timeout)));
    }

    #[test]
    fn zero_byte_response_is_valid_eof_chunk() {
        let core = VirtualFileStreamCore::new(sample_file());
        let req = core.begin_read(8).expect("begin");
        assert!(core.on_resp_buffer(RespBufferData {
            full_name: core.file().full_path.clone(),
            req_index: req.req_index,
            req_start: 0,
            req_size: 8,
            read_size: 0,
            buffer: Vec::new(),
        }));
        let mut buf = [0u8; 8];
        assert_eq!(core.complete_read(&mut buf).expect("read"), 0);
    }

    #[test]
    fn transfer_complete_requires_total_size() {
        let core = VirtualFileStreamCore::new(ClipboardFileEntry {
            total_size: 4,
            ..sample_file()
        });
        let req = core.begin_read(4).expect("begin");
        assert!(core.on_resp_buffer(sample_resp(req.req_index, b"done")));
        let mut buf = [0u8; 4];
        core.complete_read(&mut buf).expect("read");
        assert!(core.is_transfer_complete());
    }
}
