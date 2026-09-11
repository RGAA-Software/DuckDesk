#pragma once

#include "media_wire.h"
#include <list>

namespace px::media {
constexpr std::uint32_t kAudioPacketDurationMs = 20;
class AudioPacketizer final {
  public:
    // Opus length + zero padding is a Pixels payload adapter. RTP/FEC layout and matrix follow Sunshine.
    [[nodiscard]] std::vector<Packet> Push(std::span<const std::uint8_t> opus, std::uint16_t datagram_size);
    void Reset();

  private:
    std::uint16_t sequence_{};
    std::uint32_t timestamp_{};
    std::vector<Packet> shards_{};
};
struct AudioDelivery final {
    std::uint16_t sequence{};
    Packet payload{}; // Empty means exactly one lost 20 ms packet, for the existing Opus PLC entry point.
};
struct AudioQueueOutput final {
    std::vector<AudioDelivery> packets{};
    std::size_t recovered{};
    bool rejected{};
};
// Adapted from Moonlight RtpAudioQueue.c. One synchronous, bounded state machine per audio track.
class AudioReceiveQueue final {
  public:
    [[nodiscard]] AudioQueueOutput Feed(std::span<const std::uint8_t> rtp, std::uint64_t now_us);
    void Reset();

  private:
    struct Block final {
        std::uint16_t base{};
        std::uint32_t timestamp{};
        std::uint32_t ssrc{};
        std::uint64_t queued_us{};
        std::size_t next{};
        std::size_t received{};
        std::size_t data_received{};
        bool complete{};
        bool discontinuity{};
        std::vector<Packet> shards{};
        std::vector<std::uint8_t> missing{1, 1, 1, 1, 1, 1};
    };
    [[nodiscard]] bool Ready() const;
    void RemoveHead();
    std::list<Block> blocks_{};
    std::optional<std::uint16_t> oldest_{};
    std::uint16_t next_{};
    std::uint16_t last_oos_{};
    bool synchronizing_{true};
    bool received_oos_{};
};
[[nodiscard]] std::optional<Packet> UnwrapAudioPayload(std::span<const std::uint8_t> protected_payload);
} // namespace px::media
