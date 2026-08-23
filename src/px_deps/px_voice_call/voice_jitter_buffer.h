#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace px {

struct VoiceEncodedPacket {
    uint32_t sequence = 0;
    uint64_t capture_time_ms = 0;
    uint64_t arrival_time_ms = 0;
    std::vector<uint8_t> opus;
};

enum class VoiceJitterPushResult {
    kAccepted,
    kDuplicate,
    kLate,
    kInvalid,
};

enum class VoiceJitterPopKind {
    kNotReady,
    kPacket,
    kMissing,
};

struct VoiceJitterPopResult {
    VoiceJitterPopKind kind = VoiceJitterPopKind::kNotReady;
    std::optional<VoiceEncodedPacket> packet;
};

struct VoiceJitterStats {
    uint64_t accepted = 0;
    uint64_t duplicates = 0;
    uint64_t late = 0;
    uint64_t invalid = 0;
    uint64_t overflow_drops = 0;
    uint64_t missing = 0;
    size_t queued = 0;
    size_t peak_queued = 0;
};

// A bounded, wrap-safe Opus packet reorder buffer. The consumer calls Pop()
// at the negotiated 20 ms cadence. Missing packets are reported explicitly so
// the decoder can invoke PLC instead of extending latency.
class VoiceJitterBuffer {
public:
    static constexpr size_t kDefaultTargetPackets = 3;  // 60 ms
    static constexpr size_t kDefaultMaxPackets = 10;    // 200 ms
    static constexpr size_t kMaxOpusPacketBytes = 1'275;
    static constexpr uint64_t kPacketDurationMs = 20;

    explicit VoiceJitterBuffer(
        size_t target_packets = kDefaultTargetPackets,
        size_t max_packets = kDefaultMaxPackets);

    VoiceJitterPushResult Push(VoiceEncodedPacket packet);
    VoiceJitterPopResult Pop(uint64_t now_ms);
    void Reset();

    [[nodiscard]] VoiceJitterStats Stats() const;
    [[nodiscard]] size_t TargetPackets() const { return target_packets_; }
    [[nodiscard]] size_t MaxPackets() const { return max_packets_; }

private:
    static bool SequenceBefore(uint32_t lhs, uint32_t rhs);
    void DropOldestForOverflow();

    size_t target_packets_;
    size_t max_packets_;
    std::unordered_map<uint32_t, VoiceEncodedPacket> packets_;
    uint32_t first_sequence_ = 0;
    uint32_t next_sequence_ = 0;
    uint64_t first_arrival_ms_ = 0;
    bool has_first_sequence_ = false;
    bool started_ = false;
    VoiceJitterStats stats_;
};

}  // namespace px
