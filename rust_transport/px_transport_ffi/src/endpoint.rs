use super::*;
use px_transport::{EndpointAddr, EndpointConfig, TransportEndpoint};

pub struct EndpointHandle {
    runtime: Arc<Runtime>,
    endpoint: TransportEndpoint,
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_endpoint_create(
    config_json: *const u8,
    config_size: usize,
    timeout_ms: u32,
) -> *mut EndpointHandle {
    diagnostics::initialize();
    let Ok(payload) = (unsafe { input_bytes(config_json, config_size) }) else {
        return std::ptr::null_mut();
    };
    let Ok(config) = serde_json::from_slice::<EndpointConfig>(payload) else {
        return std::ptr::null_mut();
    };
    let Ok(runtime) = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(2)
        .enable_all()
        .build()
    else {
        return std::ptr::null_mut();
    };
    let uses_relay = !config.relays.is_empty();
    let Ok(Ok(endpoint)) = wait_for(&runtime, timeout_ms, async {
        let endpoint = TransportEndpoint::bind(config).await;
        // Publishing an address before the home Relay is online loses its only reachable route
        // for relay-only and NAT-isolated peers. Match the probe's readiness contract here.
        if uses_relay {
            if let Ok(endpoint) = &endpoint {
                endpoint
                    .online(std::time::Duration::from_millis(u64::from(timeout_ms)))
                    .await?;
            }
        }
        endpoint
    }) else {
        return std::ptr::null_mut();
    };
    Box::into_raw(Box::new(EndpointHandle {
        runtime: Arc::new(runtime),
        endpoint,
    }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_endpoint_close(handle: *const EndpointHandle) {
    if let Some(handle) = unsafe { handle.as_ref() } {
        handle.runtime.block_on(handle.endpoint.close());
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_endpoint_destroy(handle: *mut EndpointHandle) {
    if !handle.is_null() {
        let owner = unsafe { Box::from_raw(handle) };
        owner.runtime.block_on(owner.endpoint.close());
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_endpoint_address(
    handle: *const EndpointHandle,
    buffer: *mut u8,
    capacity: usize,
) -> CallResult {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return CallResult::status(INVALID);
    };
    match serde_json::to_vec(&handle.endpoint.address()) {
        Ok(address) => unsafe { copy_output(&address, buffer, capacity) },
        Err(_) => CallResult::status(FAILED),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_endpoint_update_relays(
    handle: *const EndpointHandle,
    relays_json: *const u8,
    relays_size: usize,
    timeout_ms: u32,
) -> CallResult {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return CallResult::status(INVALID);
    };
    let Ok(payload) = (unsafe { input_bytes(relays_json, relays_size) }) else {
        return CallResult::status(INVALID);
    };
    let Ok(relays) = serde_json::from_slice::<Vec<px_transport::PrivateRelay>>(payload) else {
        return CallResult::status(INVALID);
    };
    match wait_for(
        &handle.runtime,
        timeout_ms,
        handle.endpoint.update_relays(relays),
    ) {
        Ok(Ok(())) => CallResult::success(0),
        Ok(Err(_)) => CallResult::status(INVALID),
        Err(status) => CallResult::status(status),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_connect(
    handle: *const EndpointHandle,
    address_json: *const u8,
    address_size: usize,
    timeout_ms: u32,
) -> *mut ConnectionHandle {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return std::ptr::null_mut();
    };
    let Ok(payload) = (unsafe { input_bytes(address_json, address_size) }) else {
        return std::ptr::null_mut();
    };
    let Ok(address) = serde_json::from_slice::<EndpointAddr>(payload) else {
        return std::ptr::null_mut();
    };
    match wait_for(
        &handle.runtime,
        timeout_ms,
        handle.endpoint.connect(address),
    ) {
        Ok(Ok(connection)) => Box::into_raw(Box::new(ConnectionHandle {
            runtime: handle.runtime.clone(),
            connection,
        })),
        _ => std::ptr::null_mut(),
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn px_transport_accept(
    handle: *const EndpointHandle,
    timeout_ms: u32,
) -> *mut ConnectionHandle {
    let Some(handle) = (unsafe { handle.as_ref() }) else {
        return std::ptr::null_mut();
    };
    match wait_for(&handle.runtime, timeout_ms, handle.endpoint.accept()) {
        Ok(Ok(connection)) => Box::into_raw(Box::new(ConnectionHandle {
            runtime: handle.runtime.clone(),
            connection,
        })),
        _ => std::ptr::null_mut(),
    }
}
