// Adapted state transitions from Moonlight RtpVideoQueue.c at e41355ea01670fd4c830b384009d31dd0339a705.
// GPL-3.0; upstream source and notices are retained in third_party/moonlight_media_reference.
#include "video_receive_queue.h"
#include <algorithm>
#include <bit>

namespace px::media {
namespace {
std::uint32_t Little32(std::span<const std::uint8_t> packet, std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index{}; index < 4; ++index)
        value |= static_cast<std::uint32_t>(packet[offset + index]) << (8 * index);
    return value;
}
bool Before(std::uint32_t first, std::uint32_t second) {
    return std::bit_cast<std::int32_t>(first - second) < 0;
}
} // namespace

VideoReceiveQueue::VideoReceiveQueue(std::uint16_t datagram_size) : datagram_size_(datagram_size) {}
void VideoReceiveQueue::Reset() {
    frame_index_ = 1;
    next_block_ = 0;
    reported_loss_ = false;
    seen_reordering_ = false;
    last_reordered_us_ = 0;
    block_.reset();
    staged_.clear();
    statistics_ = {};
    observed_frame_ = false;
}
void VideoReceiveQueue::Lose(VideoQueueOutput& output, std::uint32_t frame, bool speculative) {
    output.losses.push_back({frame, speculative});
    if (speculative)
        ++statistics_.predicted_losses;
    else
        ++statistics_.final_loss_events;
    reported_loss_ = true;
}

VideoQueueOutput VideoReceiveQueue::Feed(std::span<const std::uint8_t> packet, std::uint64_t presentation_us) {
    VideoQueueOutput output{};
    if (datagram_size_ <= 40 || packet.size() != datagram_size_ || (packet[0] & 0x90) != 0x90) {
        ++statistics_.malformed_packets;
        output.rejected = output.malformed = true;
        return output;
    }
    const auto frame = Little32(packet, 20);
    const auto fec = Little32(packet, 28);
    const auto data_count = static_cast<std::uint16_t>(fec >> 22);
    const auto percentage = static_cast<std::uint8_t>((fec >> 4) & 255);
    const auto parity_count = static_cast<std::uint16_t>((99 + data_count * percentage) / 100);
    const auto index = static_cast<std::size_t>((fec >> 12) & 1023);
    const auto current_block = static_cast<std::uint8_t>((packet[27] >> 4) & 3);
    const auto last_block = static_cast<std::uint8_t>((packet[27] >> 6) & 3);
    const auto sequence = static_cast<std::uint16_t>((static_cast<unsigned>(packet[2]) << 8) | packet[3]);
    const auto base = static_cast<std::uint16_t>(sequence - index);
    const auto total = static_cast<std::size_t>(data_count + parity_count);
    if (data_count == 0 || index >= total || current_block > last_block || (parity_count > 0 && total > 255)) {
        ++statistics_.malformed_packets;
        output.rejected = output.malformed = true;
        return output;
    }
    if (index < data_count)
        ++statistics_.data_packets;
    else
        ++statistics_.parity_packets;
    if (Before(frame, frame_index_) || (frame == frame_index_ && current_block < next_block_)) {
        ++statistics_.late_packets;
        output.rejected = true;
        return output;
    }

    if (!block_ || frame != frame_index_ || current_block != next_block_) {
        if (observed_frame_ && frame != frame_index_)
            statistics_.unrecoverable_frames += static_cast<std::uint32_t>(frame - frame_index_);
        observed_frame_ = true;
        const auto expected_block = frame == frame_index_ ? next_block_ : 0;
        if (current_block != expected_block) {
            ++statistics_.unrecoverable_frames;
            if (!reported_loss_)
                Lose(output, frame_index_, false);
            frame_index_ = frame + 1;
            next_block_ = 0;
            block_.reset();
            staged_.clear();
            output.rejected = true;
            return output;
        }
        if (frame != frame_index_) {
            if (frame_index_ + 1 != frame || !reported_loss_)
                Lose(output, frame - 1, false);
            staged_.clear();
        }
        frame_index_ = frame;
        next_block_ = current_block;
        reported_loss_ = false;
        block_.emplace();
        auto& block = *block_;
        block.base_sequence = base;
        block.data_count = data_count;
        block.parity_count = parity_count;
        block.percentage = percentage;
        block.index = current_block;
        block.last_index = last_block;
        block.shards.assign(total, Packet(datagram_size_, 0));
        block.missing.assign(total, 1);
    }
    auto& block = *block_;
    if (base != block.base_sequence || data_count != block.data_count || parity_count != block.parity_count || percentage != block.percentage ||
        last_block != block.last_index) {
        ++statistics_.malformed_packets;
        output.rejected = output.malformed = true;
        return output;
    }
    if (!block.missing[index]) {
        ++statistics_.duplicates;
        output.rejected = true;
        return output;
    }
    if (block.received > 0 && index < block.highest_index) {
        ++statistics_.reordered_packets;
        seen_reordering_ = true;
        last_reordered_us_ = presentation_us;
    } else if (seen_reordering_ && presentation_us > last_reordered_us_ && presentation_us - last_reordered_us_ > 300000000) {
        seen_reordering_ = false;
    }
    if (block.received == 0) {
        block.highest_index = index;
        block.missing_before_highest = index;
    } else if (index > block.highest_index) {
        block.missing_before_highest += index - block.highest_index - 1;
        block.highest_index = index;
    } else {
        --block.missing_before_highest;
    }
    block.shards[index].assign(packet.begin(), packet.end());
    block.missing[index] = 0;
    ++block.received;
    if (index < data_count)
        ++block.data_received;
    if (block.received < data_count) {
        if (!reported_loss_ && !seen_reordering_ && block.missing_before_highest > parity_count)
            Lose(output, frame, true);
        return output;
    }
    if (reported_loss_ && !seen_reordering_) {
        seen_reordering_ = true;
        last_reordered_us_ = presentation_us;
    }
    if (block.data_received < data_count) {
        if (!NanorsCodec::Decode(block.shards, block.missing, data_count))
            return output;
        for (std::size_t shard{}; shard < data_count; ++shard) {
            if (!block.missing[shard])
                continue;
            const auto flags = block.shards[shard][24];
            if ((shard == 0 && !(flags & 4)) || (shard + 1 == data_count && !(flags & 2)) || (flags & ~7) ||
                (shard > 0 && shard + 1 < data_count && !(flags & 1)))
                return output;
            // Like RtpVideoQueue.c, restore fields that the sender writes AFTER parity generation.
            // They cannot be trusted as RS output; the validated queue state is authoritative.
            block.shards[shard][0] = 0x90;
            const auto restored_sequence = static_cast<std::uint16_t>(block.base_sequence + shard);
            block.shards[shard][2] = static_cast<std::uint8_t>(restored_sequence >> 8);
            block.shards[shard][3] = static_cast<std::uint8_t>(restored_sequence);
            for (std::size_t byte{}; byte < 4; ++byte) {
                block.shards[shard][4 + byte] = packet[4 + byte];
                block.shards[shard][20 + byte] = static_cast<std::uint8_t>(frame >> (byte * 8));
            }
        }
        output.recovered_data = data_count - block.data_received;
        statistics_.recovered_data += output.recovered_data;
    }
    if (reported_loss_)
        ++statistics_.prediction_corrections;
    for (std::size_t shard{}; shard < data_count; ++shard)
        staged_.push_back(std::move(block.shards[shard]));
    if (current_block == last_block) {
        ++statistics_.completed_frames;
        output.data_packets = std::move(staged_);
        staged_.clear();
        ++frame_index_;
        next_block_ = 0;
    } else {
        ++next_block_;
    }
    block_.reset();
    return output;
}
} // namespace px::media
