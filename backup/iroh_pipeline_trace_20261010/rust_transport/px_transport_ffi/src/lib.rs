//! Synchronous C boundary for dedicated C++ transport workers. No callbacks cross the ABI.
//! Owners must keep handles alive throughout calls; reads/writes may run concurrently, destruction may not.

mod endpoint;
mod stream;

use std::{future::Future, sync::Arc, time::Duration};

use tokio::runtime::Runtime;

pub use endpoint::*;
pub use stream::*;

pub const OK: u32 = 0;
pub const TIMEOUT: u32 = 1;
pub const CLOSED: u32 = 2;
pub const INVALID: u32 = 3;
pub const BUFFER_TOO_SMALL: u32 = 4;
pub const FAILED: u32 = 5;
pub const MAX_BUFFER: usize = 1024 * 1024;

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct CallResult {
    pub status: u32,
    pub size: usize,
}

impl CallResult {
    fn status(status: u32) -> Self {
        Self { status, size: 0 }
    }

    fn success(size: usize) -> Self {
        Self { status: OK, size }
    }
}

fn wait_for<Output>(
    runtime: &Runtime,
    timeout_ms: u32,
    future: impl Future<Output = Output>,
) -> Result<Output, u32> {
    runtime.block_on(async {
        tokio::time::timeout(Duration::from_millis(timeout_ms.into()), future)
            .await
            .map_err(|_| TIMEOUT)
    })
}

// These slices live only for the synchronous ABI call. Rust workers never retain caller buffers.
unsafe fn input_bytes<'buffer>(buffer: *const u8, size: usize) -> Result<&'buffer [u8], u32> {
    if buffer.is_null() || size > MAX_BUFFER {
        return Err(INVALID);
    }
    Ok(unsafe { std::slice::from_raw_parts(buffer, size) })
}

unsafe fn output_bytes<'buffer>(
    buffer: *mut u8,
    capacity: usize,
) -> Result<&'buffer mut [u8], u32> {
    if buffer.is_null() || capacity == 0 || capacity > MAX_BUFFER {
        return Err(INVALID);
    }
    Ok(unsafe { std::slice::from_raw_parts_mut(buffer, capacity) })
}

unsafe fn copy_output(payload: &[u8], buffer: *mut u8, capacity: usize) -> CallResult {
    if payload.len() > capacity {
        return CallResult {
            status: BUFFER_TOO_SMALL,
            size: payload.len(),
        };
    }
    let output = match unsafe { output_bytes(buffer, capacity) } {
        Ok(output) => output,
        Err(status) => return CallResult::status(status),
    };
    output[..payload.len()].copy_from_slice(payload);
    CallResult::success(payload.len())
}

pub struct ConnectionHandle {
    runtime: Arc<Runtime>,
    connection: iroh::endpoint::Connection,
}

#[repr(C)]
#[derive(Default)]
pub struct ConnectionSnapshot {
    pub path_kind: u32,
    pub open_paths: u32,
    pub rtt_us: u64,
    pub sent_packets: u64,
    pub received_packets: u64,
    pub lost_packets: u64,
    pub sent_datagrams: u64,
    pub received_datagrams: u64,
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_connection_snapshot(
    handle: *const ConnectionHandle,
) -> ConnectionSnapshot {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return ConnectionSnapshot::default();
    };
    let paths = handle.connection.paths();
    let selected = paths.iter().find(|path| path.is_selected());
    let totals = handle.connection.stats();
    ConnectionSnapshot {
        path_kind: selected.as_ref().map_or(0, |path| {
            if path.is_ip() {
                1
            } else if path.is_relay() {
                2
            } else {
                0
            }
        }),
        open_paths: paths.len() as u32,
        rtt_us: selected.map_or(0, |path| path.stats().rtt.as_micros() as u64),
        sent_packets: totals.udp_tx.datagrams,
        received_packets: totals.udp_rx.datagrams,
        lost_packets: totals.lost_packets,
        sent_datagrams: totals.frame_tx.datagram,
        received_datagrams: totals.frame_rx.datagram,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_connection_peer_id(
    handle: *const ConnectionHandle,
    buffer: *mut u8,
    capacity: usize,
) -> CallResult {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return CallResult::status(INVALID);
    };
    let peer_id = handle.connection.remote_id().to_string();
    unsafe { copy_output(peer_id.as_bytes(), buffer, capacity) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_connection_is_closed(
    handle: *const ConnectionHandle,
) -> bool {
    unsafe { handle.as_ref() }.is_none_or(|handle| handle.connection.close_reason().is_some())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_connection_can_reconnect(
    handle: *const ConnectionHandle,
) -> bool {
    use iroh::endpoint::ConnectionError;
    unsafe { handle.as_ref() }.is_some_and(|handle| {
        matches!(
            handle.connection.close_reason(),
            Some(ConnectionError::TimedOut | ConnectionError::Reset)
        )
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_connection_close(handle: *const ConnectionHandle) {
    if let Some(handle) = unsafe { handle.as_ref() } {
        handle
            .connection
            .close(0u32.into(), b"Pixels connection closed");
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_connection_destroy(handle: *mut ConnectionHandle) {
    if !handle.is_null() {
        let owner = unsafe { Box::from_raw(handle) };
        owner
            .connection
            .close(0u32.into(), b"Pixels connection destroyed");
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_datagram_limit(handle: *const ConnectionHandle) -> usize {
    unsafe { handle.as_ref() }
        .and_then(|handle| handle.connection.max_datagram_size())
        .unwrap_or(0)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_datagram_send(
    handle: *const ConnectionHandle,
    buffer: *const u8,
    size: usize,
    timeout_ms: u32,
) -> CallResult {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return CallResult::status(INVALID);
    };
    let payload = match unsafe { input_bytes(buffer, size) } {
        Ok(payload) => payload,
        Err(status) => return CallResult::status(status),
    };
    if size > handle.connection.max_datagram_size().unwrap_or(0) {
        return CallResult::status(INVALID);
    }
    match wait_for(
        &handle.runtime,
        timeout_ms,
        handle
            .connection
            .send_datagram_wait(bytes::Bytes::copy_from_slice(payload)),
    ) {
        Ok(Ok(())) => CallResult::success(size),
        Ok(Err(_)) if handle.connection.close_reason().is_some() => CallResult::status(CLOSED),
        Ok(Err(_)) => CallResult::status(FAILED),
        Err(status) => CallResult::status(status),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_datagram_receive(
    handle: *const ConnectionHandle,
    buffer: *mut u8,
    capacity: usize,
    timeout_ms: u32,
) -> CallResult {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return CallResult::status(INVALID);
    };
    if buffer.is_null() || capacity == 0 || capacity > MAX_BUFFER {
        return CallResult::status(INVALID);
    }
    match wait_for(
        &handle.runtime,
        timeout_ms,
        handle.connection.read_datagram(),
    ) {
        Ok(Ok(payload)) => unsafe { copy_output(&payload, buffer, capacity) },
        Ok(Err(_)) => CallResult::status(CLOSED),
        Err(status) => CallResult::status(status),
    }
}
