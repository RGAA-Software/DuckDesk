#include "voice_jitter_buffer.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace px {

VoiceJitterBuffer::VoiceJitterBuffer(
    size_t target_packets, size_t max_packets)
    : target_packets_(std::max<size_t>(1, target_packets)),
      max_packets_(std::max(target_packets_, max_packets)) {}

VoiceJitterPushResult VoiceJitterBuffer::Push(VoiceEncodedPacket packet) {
    if (packet.opus.empty() || packet.opus.size() > kMaxOpusPacketBytes) {
        ++stats_.invalid;
        return VoiceJitterPushResult::kInvalid;
    }
    if (started_ && SequenceBefore(packet.sequence, next_sequence_)) {
        ++stats_.late;
        return VoiceJitterPushResult::kLate;
    }
    if (packets_.contains(packet.sequence)) {
        ++stats_.duplicates;
        return VoiceJitterPushResult::kDuplicate;
    }

    if (!has_first_sequence_) {
        first_sequence_ = packet.sequence;
        first_arrival_ms_ = packet.arrival_time_ms;
        has_first_sequence_ = true;
    }
    else if (!started_ && SequenceBefore(packet.sequence, first_sequence_)) {
        first_sequence_ = packet.sequence;
        first_arrival_ms_ = std::min(first_arrival_ms_, packet.arrival_time_ms);
    }

    packets_.emplace(packet.sequence, std::move(packet));
    ++stats_.accepted;
    if (packets_.size() > max_packets_) {
        DropOldestForOverflow();
    }
    stats_.peak_queued = std::max(stats_.peak_queued, packets_.size());
    stats_.queued = packets_.size();
    return VoiceJitterPushResult::kAccepted;
}

VoiceJitterPopResult VoiceJitterBuffer::Pop(uint64_t now_ms) {
    if (!started_) {
        if (!has_first_sequence_ || packets_.empty()) {
            return {};
        }
        const uint64_t target_delay_ms =
            static_cast<uint64_t>(target_packets_) * kPacketDurationMs;
        const bool delay_elapsed = now_ms >= first_arrival_ms_ &&
            now_ms - first_arrival_ms_ >= target_delay_ms;
        if (packets_.size() < target_packets_ && !delay_elapsed) {
            return {};
        }
        started_ = true;
        next_sequence_ = first_sequence_;
    }

    auto it = packets_.find(next_sequence_);
    if (it != packets_.end()) {
        VoiceEncodedPacket packet = std::move(it->second);
        packets_.erase(it);
        ++next_sequence_;
        stats_.queued = packets_.size();
        return VoiceJitterPopResult{
            .kind = VoiceJitterPopKind::kPacket,
            .packet = std::move(packet),
        };
    }

    ++next_sequence_;
    ++stats_.missing;
    stats_.queued = packets_.size();
    return VoiceJitterPopResult{.kind = VoiceJitterPopKind::kMissing};
}

void VoiceJitterBuffer::Reset() {
    packets_.clear();
    first_sequence_ = 0;
    next_sequence_ = 0;
    first_arrival_ms_ = 0;
    has_first_sequence_ = false;
    started_ = false;
    stats_ = {};
}

VoiceJitterStats VoiceJitterBuffer::Stats() const {
    auto stats = stats_;
    stats.queued = packets_.size();
    return stats;
}

bool VoiceJitterBuffer::SequenceBefore(uint32_t lhs, uint32_t rhs) {
    return static_cast<int32_t>(lhs - rhs) < 0;
}

void VoiceJitterBuffer::DropOldestForOverflow() {
    if (packets_.empty()) {
        return;
    }
    auto oldest = packets_.begin();
    const uint32_t reference = started_ ? next_sequence_ : first_sequence_;
    for (auto it = std::next(packets_.begin()); it != packets_.end(); ++it) {
        const auto current_distance = static_cast<uint32_t>(it->first - reference);
        const auto oldest_distance = static_cast<uint32_t>(oldest->first - reference);
        if (current_distance < oldest_distance) {
            oldest = it;
        }
    }
    const bool dropping_first = !started_ && oldest->first == first_sequence_;
    packets_.erase(oldest);
    ++stats_.overflow_drops;

    if (dropping_first && !packets_.empty()) {
        auto new_first = packets_.begin();
        for (auto it = std::next(packets_.begin()); it != packets_.end(); ++it) {
            if (SequenceBefore(it->first, new_first->first)) {
                new_first = it;
            }
        }
        first_sequence_ = new_first->first;
        first_arrival_ms_ = new_first->second.arrival_time_ms;
    }
}

}  // namespace px
