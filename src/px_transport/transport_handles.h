#pragma once

#include <memory>

#include "transport_abi.h"

namespace px::transport {
struct EndpointDeleter final {
    void operator()(PxTransportEndpoint* endpoint) const noexcept {  // NOLINT(pixels-raw-pointer-boundary) Rust deallocation boundary.
        px_transport_endpoint_destroy(endpoint);
    }
};
struct ConnectionDeleter final {
    void operator()(PxTransportConnection* connection) const noexcept {  // NOLINT(pixels-raw-pointer-boundary) Rust deallocation boundary.
        px_transport_connection_destroy(connection);
    }
};
struct StreamDeleter final {
    void operator()(PxTransportStream* stream) const noexcept {  // NOLINT(pixels-raw-pointer-boundary) Rust deallocation boundary.
        px_transport_stream_destroy(stream);
    }
};
using EndpointOwner = std::unique_ptr<PxTransportEndpoint, EndpointDeleter>;
using ConnectionOwner = std::unique_ptr<PxTransportConnection, ConnectionDeleter>;
using StreamOwner = std::unique_ptr<PxTransportStream, StreamDeleter>;
}  // namespace px::transport
