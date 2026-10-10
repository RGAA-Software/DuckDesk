use super::*;
use iroh::endpoint::{RecvStream, SendStream};
use tokio::sync::Mutex;

pub struct StreamHandle {
    runtime: Arc<Runtime>,
    sender: Mutex<SendStream>,
    receiver: Mutex<RecvStream>,
}

#[repr(C)]
pub struct StreamPriority {
    pub status: u32,
    pub priority: i32,
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_stream_set_priority(
    handle: *const StreamHandle,
    priority: i32,
    timeout_ms: u32,
) -> CallResult {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return CallResult::status(INVALID);
    };
    match wait_for(&handle.runtime, timeout_ms, async {
        handle.sender.lock().await.set_priority(priority)
    }) {
        Ok(Ok(())) => CallResult::success(0),
        Ok(Err(_)) => CallResult::status(CLOSED),
        Err(status) => CallResult::status(status),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_stream_priority(
    handle: *const StreamHandle,
    timeout_ms: u32,
) -> StreamPriority {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return StreamPriority {
            status: INVALID,
            priority: 0,
        };
    };
    match wait_for(&handle.runtime, timeout_ms, async {
        handle.sender.lock().await.priority()
    }) {
        Ok(Ok(priority)) => StreamPriority {
            status: OK,
            priority,
        },
        Ok(Err(_)) => StreamPriority {
            status: CLOSED,
            priority: 0,
        },
        Err(status) => StreamPriority {
            status,
            priority: 0,
        },
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_stream_open(
    handle: *const ConnectionHandle,
    priority: i32,
    timeout_ms: u32,
) -> *mut StreamHandle {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return std::ptr::null_mut();
    };
    let Ok(Ok((sender, receiver))) =
        wait_for(&handle.runtime, timeout_ms, handle.connection.open_bi())
    else {
        return std::ptr::null_mut();
    };
    if sender.set_priority(priority).is_err() {
        return std::ptr::null_mut();
    }
    Box::into_raw(Box::new(StreamHandle {
        runtime: handle.runtime.clone(),
        sender: Mutex::new(sender),
        receiver: Mutex::new(receiver),
    }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_stream_accept(
    handle: *const ConnectionHandle,
    timeout_ms: u32,
) -> *mut StreamHandle {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return std::ptr::null_mut();
    };
    let Ok(Ok((sender, receiver))) =
        wait_for(&handle.runtime, timeout_ms, handle.connection.accept_bi())
    else {
        return std::ptr::null_mut();
    };
    Box::into_raw(Box::new(StreamHandle {
        runtime: handle.runtime.clone(),
        sender: Mutex::new(sender),
        receiver: Mutex::new(receiver),
    }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_stream_destroy(handle: *mut StreamHandle) {
    if !handle.is_null() {
        drop(unsafe { Box::from_raw(handle) });
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_stream_finish(
    handle: *const StreamHandle,
    timeout_ms: u32,
) -> CallResult {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return CallResult::status(INVALID);
    };
    match wait_for(&handle.runtime, timeout_ms, async {
        handle.sender.lock().await.finish()
    }) {
        Ok(Ok(())) => CallResult::success(0),
        Ok(Err(_)) => CallResult::status(CLOSED),
        Err(status) => CallResult::status(status),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_stream_write(
    handle: *const StreamHandle,
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
    // On timeout a prefix may have reached the peer. Reset this stream; callers must not retry the entire buffer on it.
    let outcome = handle.runtime.block_on(async {
        let mut sender = match tokio::time::timeout(
            Duration::from_millis(timeout_ms.into()),
            handle.sender.lock(),
        )
        .await
        {
            Ok(sender) => sender,
            Err(_) => return CallResult::status(TIMEOUT),
        };
        match tokio::time::timeout(
            Duration::from_millis(timeout_ms.into()),
            sender.write_all(payload),
        )
        .await
        {
            Ok(Ok(())) => CallResult::success(size),
            Ok(Err(_)) => CallResult::status(CLOSED),
            Err(_) => {
                let _ = sender.reset(1u32.into());
                CallResult::status(TIMEOUT)
            }
        }
    });
    outcome
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_stream_read(
    handle: *const StreamHandle,
    buffer: *mut u8,
    capacity: usize,
    timeout_ms: u32,
) -> CallResult {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return CallResult::status(INVALID);
    };
    let output = match unsafe { output_bytes(buffer, capacity) } {
        Ok(output) => output,
        Err(status) => return CallResult::status(status),
    };
    match wait_for(&handle.runtime, timeout_ms, async {
        handle.receiver.lock().await.read(output).await
    }) {
        Ok(Ok(Some(size))) => CallResult::success(size),
        Ok(Ok(None) | Err(_)) => CallResult::status(CLOSED),
        Err(status) => CallResult::status(status),
    }
}
