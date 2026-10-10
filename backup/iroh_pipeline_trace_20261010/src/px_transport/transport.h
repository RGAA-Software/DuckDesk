#pragma once

#include <expected>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "transport_handles.h"

namespace px::transport {

enum class Error : std::uint32_t { kTimeout = 1, kClosed = 2, kInvalid = 3, kBufferTooSmall = 4, kFailed = 5 };
using Bytes = std::vector<std::uint8_t>;

enum class PathKind : std::uint32_t { kUnknown = 0, kDirect = 1, kRelay = 2 };
struct ConnectionSnapshot final {
    PathKind path{PathKind::kUnknown};
    std::uint32_t open_paths{};
    std::uint64_t rtt_us{};
    std::uint64_t sent_packets{};
    std::uint64_t received_packets{};
    std::uint64_t lost_packets{};
    std::uint64_t sent_datagrams{};
    std::uint64_t received_datagrams{};
};

// These calls block only their calling worker. Separate QUIC streams may read/write concurrently.
// Shared ownership at async boundaries prevents destruction during a pending read; Close cancels it.
class Stream final {
public:
    explicit Stream(StreamOwner owner) : owner_(std::move(owner)) {}
    [[nodiscard]] std::expected<std::size_t, Error> Write(std::span<const std::uint8_t> payload, std::uint32_t timeout_ms) const;
    [[nodiscard]] std::expected<Bytes, Error> Read(std::size_t capacity, std::uint32_t timeout_ms) const;
    [[nodiscard]] std::expected<void, Error> Finish(std::uint32_t timeout_ms) const;

private:
    StreamOwner owner_{};
};

class Connection final {
public:
    explicit Connection(ConnectionOwner owner) : owner_(std::move(owner)) {}
    [[nodiscard]] std::shared_ptr<Stream> OpenStream(std::int32_t priority, std::uint32_t timeout_ms) const;
    [[nodiscard]] std::shared_ptr<Stream> AcceptStream(std::uint32_t timeout_ms) const;
    [[nodiscard]] std::size_t DatagramLimit() const;
    [[nodiscard]] ConnectionSnapshot Snapshot() const;
    [[nodiscard]] std::expected<std::string, Error> PeerId() const;
    [[nodiscard]] bool IsClosed() const;
    // Only transport timeout/reset is retryable; policy rejection and intentional close are terminal.
    [[nodiscard]] bool CanReconnect() const;
    [[nodiscard]] std::expected<void, Error> SendDatagram(std::span<const std::uint8_t> payload, std::uint32_t timeout_ms = 20) const;
    [[nodiscard]] std::expected<Bytes, Error> ReceiveDatagram(std::uint32_t timeout_ms) const;
    void Close() const;

private:
    ConnectionOwner owner_{};
    mutable std::mutex close_mutex_{};
    mutable bool close_requested_{};
    mutable bool recoverable_close_{};
};

class Endpoint final {
public:
    explicit Endpoint(EndpointOwner owner) : owner_(std::move(owner)) {}
    [[nodiscard]] static std::shared_ptr<Endpoint> Bind(const std::string& configuration_json, std::uint32_t timeout_ms);
    [[nodiscard]] std::expected<std::string, Error> Address() const;
    [[nodiscard]] std::shared_ptr<Connection> Connect(const std::string& address_json, std::uint32_t timeout_ms) const;
    [[nodiscard]] std::shared_ptr<Connection> Accept(std::uint32_t timeout_ms) const;
    void Close() const;

private:
    EndpointOwner owner_{};
};

}  // namespace px::transport
