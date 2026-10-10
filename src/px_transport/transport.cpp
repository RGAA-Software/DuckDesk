#include "transport.h"

#include <array>

namespace px::transport {
namespace {
constexpr std::size_t kMaximumBuffer{1024 * 1024};
}

std::expected<void, Error> Stream::SetPriority(std::int32_t priority, std::uint32_t timeout_ms) const {
    const auto result = px_transport_stream_set_priority(owner_.get(), priority, timeout_ms);
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return {};
}

std::expected<std::int32_t, Error> Stream::Priority(std::uint32_t timeout_ms) const {
    const auto result = px_transport_stream_priority(owner_.get(), timeout_ms);
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return result.priority;
}

std::expected<std::string, Error> Connection::SelectedPath() const {
    std::array<std::uint8_t, 4096> output{};
    const auto result = px_transport_connection_selected_path(owner_.get(), output.data(), output.size());
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return std::string(output.begin(), output.begin() + result.size);
}

std::shared_ptr<Endpoint> Endpoint::Bind(const std::string& configuration_json, std::uint32_t timeout_ms) {
    EndpointOwner owner{
        px_transport_endpoint_create(reinterpret_cast<const std::uint8_t*>(configuration_json.data()), configuration_json.size(), timeout_ms)};
    return owner ? std::make_shared<Endpoint>(std::move(owner)) : nullptr;
}

std::expected<std::string, Error> Endpoint::Address() const {
    std::array<std::uint8_t, 16384> output{};
    const auto result = px_transport_endpoint_address(owner_.get(), output.data(), output.size());
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return std::string(output.begin(), output.begin() + result.size);
}

std::shared_ptr<Connection> Endpoint::Connect(const std::string& address_json, std::uint32_t timeout_ms) const {
    ConnectionOwner owner{
        px_transport_connect(owner_.get(), reinterpret_cast<const std::uint8_t*>(address_json.data()), address_json.size(), timeout_ms)};
    return owner ? std::make_shared<Connection>(std::move(owner)) : nullptr;
}

std::shared_ptr<Connection> Endpoint::Accept(std::uint32_t timeout_ms) const {
    ConnectionOwner owner{px_transport_accept(owner_.get(), timeout_ms)};
    return owner ? std::make_shared<Connection>(std::move(owner)) : nullptr;
}

void Endpoint::Close() const { px_transport_endpoint_close(owner_.get()); }

std::expected<void, Error> Endpoint::UpdateRelays(const std::string& relays_json) const {
    const auto result =
        px_transport_endpoint_update_relays(owner_.get(), reinterpret_cast<const std::uint8_t*>(relays_json.data()), relays_json.size(), 1000);
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return {};
}

std::shared_ptr<Stream> Connection::OpenStream(std::int32_t priority, std::uint32_t timeout_ms) const {
    StreamOwner owner{px_transport_stream_open(owner_.get(), priority, timeout_ms)};
    return owner ? std::make_shared<Stream>(std::move(owner)) : nullptr;
}

std::shared_ptr<Stream> Connection::AcceptStream(std::uint32_t timeout_ms) const {
    StreamOwner owner{px_transport_stream_accept(owner_.get(), timeout_ms)};
    return owner ? std::make_shared<Stream>(std::move(owner)) : nullptr;
}

std::size_t Connection::DatagramLimit() const { return px_transport_datagram_limit(owner_.get()); }

ConnectionSnapshot Connection::Snapshot() const {
    const auto snapshot = px_transport_connection_snapshot(owner_.get());
    return {.path = static_cast<PathKind>(snapshot.path_kind),
            .open_paths = snapshot.open_paths,
            .rtt_us = snapshot.rtt_us,
            .sent_packets = snapshot.sent_packets,
            .received_packets = snapshot.received_packets,
            .lost_packets = snapshot.lost_packets,
            .sent_datagrams = snapshot.sent_datagrams,
            .received_datagrams = snapshot.received_datagrams,
            .congestion_window_bytes = snapshot.congestion_window_bytes,
            .datagram_buffer_space = snapshot.datagram_buffer_space,
            .congestion_events = snapshot.congestion_events};
}

std::expected<std::string, Error> Connection::PeerId() const {
    std::array<std::uint8_t, 128> output{};
    const auto result = px_transport_connection_peer_id(owner_.get(), output.data(), output.size());
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return std::string(output.begin(), output.begin() + result.size);
}

bool Connection::IsClosed() const { return px_transport_connection_is_closed(owner_.get()); }
bool Connection::CanReconnect() const {
    std::lock_guard lock(close_mutex_);
    return close_requested_ ? recoverable_close_ : px_transport_connection_can_reconnect(owner_.get());
}

std::expected<void, Error> Connection::SendDatagram(std::span<const std::uint8_t> payload, std::uint32_t timeout_ms) const {
    const auto result = px_transport_datagram_send(owner_.get(), payload.data(), payload.size(), timeout_ms);
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return {};
}

std::expected<Bytes, Error> Connection::ReceiveDatagram(std::uint32_t timeout_ms) const {
    Bytes output(65536);
    const auto result = px_transport_datagram_receive(owner_.get(), output.data(), output.size(), timeout_ms);
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    output.resize(result.size);
    return output;
}

void Connection::Close() const {
    std::lock_guard lock(close_mutex_);
    if (close_requested_) return;
    // noq's explicit close replaces its stored error with LocallyClosed. Preserve
    // the original timeout/reset before MessageSession performs ordinary cleanup.
    recoverable_close_ = px_transport_connection_can_reconnect(owner_.get());
    close_requested_ = true;
    px_transport_connection_close(owner_.get());
}

std::expected<std::size_t, Error> Stream::Write(std::span<const std::uint8_t> payload, std::uint32_t timeout_ms) const {
    const auto result = px_transport_stream_write(owner_.get(), payload.data(), payload.size(), timeout_ms);
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return result.size;
}

std::expected<Bytes, Error> Stream::Read(std::size_t capacity, std::uint32_t timeout_ms) const {
    if (capacity == 0 || capacity > kMaximumBuffer) return std::unexpected(Error::kInvalid);
    Bytes output(capacity);
    const auto result = px_transport_stream_read(owner_.get(), output.data(), output.size(), timeout_ms);
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    output.resize(result.size);
    return output;
}

std::expected<void, Error> Stream::Finish(std::uint32_t timeout_ms) const {
    const auto result = px_transport_stream_finish(owner_.get(), timeout_ms);
    if (result.status != 0) return std::unexpected(static_cast<Error>(result.status));
    return {};
}
}  // namespace px::transport
