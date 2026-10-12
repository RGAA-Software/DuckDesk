#include "video_diagnostics.h"

#include <format>

#include "px_common/log.h"

namespace px::transport {
std::optional<VideoPacketIdentity> ReadVideoPacketIdentity(std::span<const std::uint8_t> packet) {
    if (packet.size() < 32 || (packet[0] & 0x90) != 0x90) return std::nullopt;
    const auto little = [packet](std::size_t offset) {
        return static_cast<std::uint32_t>(packet[offset]) | (static_cast<std::uint32_t>(packet[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(packet[offset + 2]) << 16) | (static_cast<std::uint32_t>(packet[offset + 3]) << 24);
    };
    const auto fec = little(28);
    const auto data_count = static_cast<std::uint16_t>(fec >> 22);
    const auto parity_count = static_cast<std::uint16_t>((99 + data_count * ((fec >> 4) & 255)) / 100);
    const auto shard = static_cast<std::uint16_t>((fec >> 12) & 1023);
    const auto block = static_cast<std::uint8_t>((packet[27] >> 4) & 3);
    const auto last_block = static_cast<std::uint8_t>((packet[27] >> 6) & 3);
    if (!data_count || shard >= data_count + parity_count || block > last_block) return std::nullopt;
    return VideoPacketIdentity{little(20),
                               static_cast<std::uint32_t>(media::wire::Get(packet, 4, 4)),
                               static_cast<std::uint16_t>(media::wire::Get(packet, 2, 2)),
                               shard,
                               data_count,
                               parity_count,
                               block,
                               last_block};
}

VideoReceiveDiagnostics::~VideoReceiveDiagnostics() { Flush(media::MediaSteadyMicros(), true); }

void VideoReceiveDiagnostics::Observe(const media::MediaDatagram& packet, std::uint64_t now_us) {
    const auto identity = ReadVideoPacketIdentity(packet.payload);
    if (!identity) return;
    const auto key = std::pair{packet.stream, identity->wire_frame};
    if (!frames_.contains(key) && frames_.size() >= 64) Flush(now_us, true);
    auto& frame = frames_[key];
    if (!frame.samples) {
        frame.first_us = now_us;
        frame.receipt.first_packet_received_us = now_us;
        frame.timestamp = identity->timestamp;
        frame.last_block = identity->last_block;
    }
    frame.receipt.latest_packet_received_us = now_us;
    auto& block = frame.blocks[identity->block];
    block.data_count = identity->data_count;
    block.parity_count = identity->parity_count;
    if (block.shards.test(identity->shard))
        ++frame.receipt.duplicate_shards;
    else {
        block.shards.set(identity->shard);
        ++frame.receipt.unique_shards;
    }
    // The first data shard contains a descriptor even when the frame cannot
    // be assembled. Inspect fixed metadata only; never retain encoded payload.
    if (identity->block == 0 && identity->shard == 0 && packet.payload.size() >= 68 && packet.payload[32] == 1 && packet.payload[40] == 'P' &&
        packet.payload[41] == 'V' && packet.payload[42] == 2 && media::wire::Get(packet.payload, 64, 4) == identity->wire_frame) {
        frame.receipt.encoder_frame = media::wire::Get(packet.payload, 52, 8);
        const auto kind = static_cast<media::VideoFrameKind>(packet.payload[35]);
        if (kind == media::VideoFrameKind::kPredicted || kind == media::VideoFrameKind::kIdr || kind == media::VideoFrameKind::kReferenceRecovery)
            frame.receipt.kind = kind;
    }
    // Keep arrival order, duplicates and late parity; absent entries mean unobserved, not proven network loss.
    const bool detailed = Active(now_us) || (frame.receipt.kind && *frame.receipt.kind != media::VideoFrameKind::kPredicted);
    if (detailed && frame.samples < 2048) {
        frame.arrivals += std::format("{}:{}:{}:{}:{}:{}:{};", identity->sequence, identity->block, identity->last_block, identity->shard,
                                      identity->data_count, identity->parity_count, now_us - frame.first_us);
    } else if (detailed) {
        frame.truncated = true;
    }
    ++frame.samples;
}

void VideoReceiveDiagnostics::Completed(std::uint8_t stream, std::uint32_t wire_frame, std::uint64_t encoder_frame,
                                        std::optional<media::VideoFrameKind> kind, bool delivered) {
    const auto position = frames_.find({stream, wire_frame});
    if (position == frames_.end()) return;
    auto& receipt = position->second.receipt;
    receipt.encoder_frame = encoder_frame;
    if (kind) receipt.kind = kind;
    receipt.completed = true;
    receipt.delivered = delivered;
}

std::optional<VideoReceiveDiagnostics::Receipt> VideoReceiveDiagnostics::Inspect(std::uint8_t stream, std::uint32_t wire_frame) const {
    const auto position = frames_.find({stream, wire_frame});
    return position == frames_.end() ? std::nullopt : std::optional{position->second.receipt};
}

void VideoReceiveDiagnostics::Flush(std::uint64_t now_us, bool all) {
    if (!all && now_us < next_flush_us_) return;
    next_flush_us_ = now_us + 20'000;
    for (auto position = frames_.begin(); position != frames_.end();) {
        const auto& frame = position->second;
        if (!all && now_us - frame.first_us < 500'000) {
            ++position;
            continue;
        }
        if (Active(frame.first_us) || !frame.receipt.completed || (frame.receipt.kind && *frame.receipt.kind != media::VideoFrameKind::kPredicted)) {
            std::string coverage{};
            for (std::size_t block_index{}; block_index <= frame.last_block; ++block_index) {
                const auto& block = frame.blocks[block_index];
                std::string missing{};
                for (std::size_t shard_index{}; shard_index < block.data_count; ++shard_index) {
                    if (!block.shards.test(shard_index)) missing += std::format("{},", shard_index);
                }
                coverage += std::format("{}:{}/{}+{}:missing={};", block_index, block.shards.count(), block.data_count, block.parity_count, missing);
            }
            LOGI(
                "event=iroh.diag_rx_packets trace={} stream={} wire_frame={} timestamp={} first_us={} samples={} truncated={} "
                "encoder_frame={} kind={} completed={} delivered={} coverage={} arrivals={}",
                started_us_, position->first.first, position->first.second, frame.timestamp, frame.first_us, frame.samples, frame.truncated,
                frame.receipt.encoder_frame, frame.receipt.kind ? static_cast<unsigned>(*frame.receipt.kind) : 0, frame.receipt.completed,
                frame.receipt.delivered, coverage, frame.arrivals);
        }
        position = frames_.erase(position);
    }
}
}  // namespace px::transport
