#pragma once

#include <mutex>
#include <optional>

#include "transport.h"

namespace px::transport {

enum class ChannelKind : std::uint8_t { kControl = 1, kInput = 2, kClipboard = 3, kFile = 4, kRdp = 5 };

// Each channel has an independent reliable stream. Control admission happens above this transport framing.
class Channel final {
public:
    static constexpr std::size_t kMaximumMessage{1024 * 1024};
    explicit Channel(std::shared_ptr<Stream> stream, ChannelKind kind) : stream_(std::move(stream)), kind_(kind) {}
    [[nodiscard]] static std::expected<std::shared_ptr<Channel>, Error> Open(const std::shared_ptr<Connection>& connection, ChannelKind kind,
                                                                             std::uint32_t timeout_ms);
    [[nodiscard]] static std::expected<std::shared_ptr<Channel>, Error> Accept(const std::shared_ptr<Connection>& connection,
                                                                               std::uint32_t timeout_ms);
    [[nodiscard]] ChannelKind Kind() const noexcept { return kind_; }
    [[nodiscard]] std::expected<std::int32_t, Error> SendingPriority(std::uint32_t timeout_ms) const { return stream_->Priority(timeout_ms); }
    [[nodiscard]] std::expected<void, Error> Send(std::span<const std::uint8_t> payload, std::uint32_t timeout_ms);
    [[nodiscard]] std::expected<Bytes, Error> Receive(std::uint32_t timeout_ms);
    [[nodiscard]] std::expected<void, Error> Finish(std::uint32_t timeout_ms);

private:
    std::shared_ptr<Stream> stream_{};
    ChannelKind kind_{ChannelKind::kControl};
    std::mutex send_mutex_{};
    std::mutex receive_mutex_{};
    Bytes receive_buffer_{};
    std::optional<std::size_t> receive_message_size_{};
    bool receive_failed_{};
};

}  // namespace px::transport
