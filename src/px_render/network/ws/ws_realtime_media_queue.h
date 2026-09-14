#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace px {

class Data;

enum class WsRealtimeMediaKind : std::uint8_t { None, Video, Audio };

[[nodiscard]] WsRealtimeMediaKind ClassifyWsRealtimeMedia(const std::shared_ptr<Data>& message);

// Bounds only disposable real-time media waiting for one WebSocket client.
// Reliable control, input, clipboard, session, and file-transfer messages do not
// use this budget and therefore cannot be rejected by it.
class WsRealtimeMediaQueueBudget final {
  public:
    static constexpr std::size_t kMaxMessages{4U};
    static constexpr std::size_t kMaxBytes{4U * 1024U * 1024U};

    [[nodiscard]] bool TryReserve(std::size_t bytes);
    void Release(std::size_t bytes);
    [[nodiscard]] std::size_t PendingMessages() const;
    [[nodiscard]] std::size_t PendingBytes() const;

  private:
    mutable std::mutex mutex_{};
    std::size_t pendingMessages_{};
    std::size_t pendingBytes_{};
};

} // namespace px
