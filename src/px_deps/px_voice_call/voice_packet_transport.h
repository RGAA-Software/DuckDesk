#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace px {

struct VoiceTransportPacket {
    uint32_t sequence = 0;
    uint64_t capture_time_ms = 0;
    std::vector<uint8_t> opus;
};

struct VoicePacketTransportStats {
    uint64_t enqueued = 0;
    uint64_t sent = 0;
    uint64_t congestion_drops = 0;
    size_t queued = 0;
    size_t peak_queued = 0;
};

// A transport-isolation queue for non-WebRTC voice. The producer never calls
// a network plugin and the queue retains only the newest packets under head-of-
// line blocking, keeping latency bounded instead of replaying stale speech.
class VoicePacketTransport {
public:
    using SendCallback = std::function<void(const VoiceTransportPacket&)>;
    static constexpr size_t kMaxQueuedPackets = 5;

    VoicePacketTransport() = default;
    ~VoicePacketTransport();
    VoicePacketTransport(const VoicePacketTransport&) = delete;
    VoicePacketTransport& operator=(const VoicePacketTransport&) = delete;

    bool Start(SendCallback callback);
    void Stop();
    bool Enqueue(VoiceTransportPacket packet);
    [[nodiscard]] VoicePacketTransportStats Stats() const;

private:
    void Worker();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<VoiceTransportPacket> queue_;
    SendCallback callback_;
    std::thread worker_;
    bool running_ = false;
    VoicePacketTransportStats stats_;
};

}  // namespace px
