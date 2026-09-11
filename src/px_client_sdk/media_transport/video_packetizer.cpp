// Algorithm derived from Sunshine stream.cpp at 3cba9baebac882b336be3ebe129ee612cb189853.
// Retain GPL-3.0 source attribution; see third_party/moonlight_media_reference/UPSTREAM.md.
#include "video_packetizer.h"
#include <algorithm>

namespace px::media {
namespace {
constexpr std::size_t kPacketHeader = 32; // RTP 12 + extension 4 + NV_VIDEO_PACKET 16.
constexpr std::size_t kFrameHeader = 8;
constexpr std::uint8_t kPictureData = 1;
constexpr std::uint8_t kEnd = 2;
constexpr std::uint8_t kStart = 4;

void Little32(Packet& packet, std::size_t offset, std::uint32_t value) {
    for (std::size_t index{}; index < 4; ++index)
        packet[offset + index] = static_cast<std::uint8_t>(value >> (8 * index));
}
void Big32(Packet& packet, std::size_t offset, std::uint32_t value) {
    for (std::size_t index{}; index < 4; ++index)
        packet[offset + index] = static_cast<std::uint8_t>(value >> (8 * (3 - index)));
}
} // namespace

std::optional<PacketizedVideo> PacketizeVideo(std::span<const std::uint8_t> encoded, VideoPacketParameters parameters) {
    if (encoded.empty() || encoded.size() > 4 * 1023 * (65507 - kPacketHeader) || parameters.datagram_size <= kPacketHeader + kFrameHeader ||
        parameters.datagram_size > 65507)
        return std::nullopt;
    const std::size_t block_size = parameters.datagram_size;
    const auto payload_size = block_size - kPacketHeader;
    const auto frame_bytes = encoded.size() + kFrameHeader;
    const auto data_packets = (frame_bytes + payload_size - 1) / payload_size;
    const auto packed_bytes = frame_bytes + data_packets * kPacketHeader;
    auto percentage = static_cast<std::size_t>(parameters.fec_percent);
    const auto max_block_bytes = (255 * 100 / (100 + percentage)) * block_size;
    auto block_count = (packed_bytes + max_block_bytes - 1) / max_block_bytes;
    const bool oversized = block_count > 4;
    if (oversized) {
        percentage = 0;
        block_count = 4;
    }
    const auto block_packets = ((packed_bytes / block_count) + block_size - 1) / block_size;
    // Upstream logs and continues at this limit. Reject instead of emitting aliased 10-bit indices.
    if (block_packets >= 1024)
        return std::nullopt;

    Packet frame(frame_bytes, 0);
    frame[0] = 1;
    frame[3] = static_cast<std::uint8_t>(parameters.kind);
    const auto final_payload_size = static_cast<std::uint16_t>((frame_bytes - 1) % payload_size + 1);
    frame[4] = static_cast<std::uint8_t>(final_payload_size);
    frame[5] = static_cast<std::uint8_t>(final_payload_size >> 8);
    std::ranges::copy(encoded, frame.begin() + kFrameHeader);

    PacketizedVideo result{};
    result.block_count = static_cast<std::uint8_t>(block_count);
    result.fec_disabled_for_large_frame = oversized;
    auto sequence = parameters.sequence;
    std::size_t frame_offset{};
    for (std::size_t block{}; block < block_count; ++block) {
        const auto count = std::min(block_packets, data_packets - block * block_packets);
        auto parity = (99 + count * percentage) / 100;
        auto effective_percentage = percentage;
        if (percentage != 0 && parity < parameters.minimum_parity) {
            parity = parameters.minimum_parity;
            effective_percentage = 100 * parity / count;
        }
        // Invalid minimum-parity configurations must not overrun the RS matrix or wire fields.
        if (effective_percentage > 255 || (parity != 0 && count + parity > 255))
            return std::nullopt;
        std::vector<Packet> shards(count + parity, Packet(block_size, 0));
        for (std::size_t index{}; index < count; ++index) {
            auto& shard = shards[index];
            Little32(shard, 16, (static_cast<std::uint32_t>(sequence) + static_cast<std::uint32_t>(index)) << 8);
            Little32(shard, 20, parameters.frame_index);
            shard[24] = kPictureData | (index == 0 ? kStart : 0) | (index + 1 == count ? kEnd : 0);
            shard[26] = 0x10;
            shard[27] = static_cast<std::uint8_t>((block << 4) | ((block_count - 1) << 6));
            const auto bytes = std::min(payload_size, frame.size() - frame_offset);
            std::copy_n(frame.begin() + static_cast<std::ptrdiff_t>(frame_offset), bytes, shard.begin() + kPacketHeader);
            frame_offset += bytes;
        }
        if (parity != 0 && !NanorsCodec::Encode(shards, count))
            return std::nullopt;
        for (std::size_t index{}; index < shards.size(); ++index) {
            auto& shard = shards[index];
            shard[0] = 0x90;
            const auto packet_sequence = static_cast<std::uint16_t>(sequence + index);
            shard[2] = static_cast<std::uint8_t>(packet_sequence >> 8);
            shard[3] = static_cast<std::uint8_t>(packet_sequence);
            Big32(shard, 4, parameters.timestamp_90khz);
            Little32(shard, 20, parameters.frame_index);
            shard[27] = static_cast<std::uint8_t>((block << 4) | ((block_count - 1) << 6));
            Little32(shard, 28, static_cast<std::uint32_t>((index << 12) | (count << 22) | (effective_percentage << 4)));
            result.packets.push_back(std::move(shard));
        }
        sequence = static_cast<std::uint16_t>(sequence + shards.size());
    }
    result.next_sequence = sequence;
    return result;
}
} // namespace px::media
