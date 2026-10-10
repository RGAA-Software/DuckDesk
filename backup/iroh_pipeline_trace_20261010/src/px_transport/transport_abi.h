#pragma once

#include <cstddef>
#include <cstdint>

// C ABI boundary only: opaque Rust allocations are immediately adopted by the typed owners below.
// No callback or borrowed buffer survives a call. Callers keep owners alive until synchronous calls finish.
extern "C" {
struct PxTransportEndpoint;
struct PxTransportConnection;
struct PxTransportStream;
struct PxTransportCallResult {
    std::uint32_t status{};
    std::size_t size{};
};
struct PxTransportConnectionSnapshot {
    std::uint32_t path_kind{};
    std::uint32_t open_paths{};
    std::uint64_t rtt_us{};
    std::uint64_t sent_packets{};
    std::uint64_t received_packets{};
    std::uint64_t lost_packets{};
    std::uint64_t sent_datagrams{};
    std::uint64_t received_datagrams{};
};
PxTransportConnectionSnapshot px_transport_connection_snapshot(const PxTransportConnection* connection);
PxTransportEndpoint* px_transport_endpoint_create(const std::uint8_t* config, std::size_t size, std::uint32_t timeout_ms);
void px_transport_endpoint_close(const PxTransportEndpoint* endpoint);
void px_transport_endpoint_destroy(PxTransportEndpoint* endpoint);
PxTransportCallResult px_transport_endpoint_address(const PxTransportEndpoint* endpoint, std::uint8_t* output, std::size_t capacity);
PxTransportConnection* px_transport_connect(const PxTransportEndpoint* endpoint, const std::uint8_t* address, std::size_t size,
                                            std::uint32_t timeout_ms);
PxTransportConnection* px_transport_accept(const PxTransportEndpoint* endpoint, std::uint32_t timeout_ms);
void px_transport_connection_close(const PxTransportConnection* connection);
void px_transport_connection_destroy(PxTransportConnection* connection);
PxTransportCallResult px_transport_connection_peer_id(const PxTransportConnection* connection, std::uint8_t* output, std::size_t capacity);
bool px_transport_connection_is_closed(const PxTransportConnection* connection);
bool px_transport_connection_can_reconnect(const PxTransportConnection* connection);
std::size_t px_transport_datagram_limit(const PxTransportConnection* connection);
PxTransportCallResult px_transport_datagram_send(const PxTransportConnection* connection, const std::uint8_t* payload, std::size_t size,
                                                 std::uint32_t timeout_ms);
PxTransportCallResult px_transport_datagram_receive(const PxTransportConnection* connection, std::uint8_t* output, std::size_t capacity,
                                                    std::uint32_t timeout_ms);
PxTransportStream* px_transport_stream_open(const PxTransportConnection* connection, std::int32_t priority, std::uint32_t timeout_ms);
PxTransportStream* px_transport_stream_accept(const PxTransportConnection* connection, std::uint32_t timeout_ms);
void px_transport_stream_destroy(PxTransportStream* stream);
PxTransportCallResult px_transport_stream_finish(const PxTransportStream* stream, std::uint32_t timeout_ms);
PxTransportCallResult px_transport_stream_write(const PxTransportStream* stream, const std::uint8_t* payload, std::size_t size,
                                                std::uint32_t timeout_ms);
PxTransportCallResult px_transport_stream_read(const PxTransportStream* stream, std::uint8_t* output, std::size_t capacity, std::uint32_t timeout_ms);
}
