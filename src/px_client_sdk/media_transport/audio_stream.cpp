// Audio RTP/FEC and queue transitions adapted from Sunshine stream.cpp and Moonlight RtpAudioQueue.c.
// GPL-3.0; pinned pristine sources and notices: third_party/moonlight_media_reference/UPSTREAM.md.
#include "audio_stream.h"
#include <algorithm>
#include <bit>

namespace px::media {
namespace {
constexpr std::size_t kAudioRtpHeader = 12;
constexpr std::size_t kAudioFecHeader = 12;
constexpr std::uint8_t kAudioPayloadType = 97;
constexpr std::uint8_t kFecPayloadType = 127;
bool Before(std::uint16_t first, std::uint16_t second) {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(first - second)) < 0;
}
Packet MakeRtp(std::uint8_t type, std::uint16_t sequence, std::uint32_t timestamp) {
    Packet packet(kAudioRtpHeader, 0);
    packet[0] = 0x80;
    packet[1] = type;
    wire::Put(packet, 2, sequence, 2);
    wire::Put(packet, 4, timestamp, 4);
    return packet;
}
} // namespace

void AudioPacketizer::Reset() {
    sequence_ = 0;
    timestamp_ = 0;
    shards_.clear();
}
std::vector<Packet> AudioPacketizer::Push(std::span<const std::uint8_t> opus, std::uint16_t datagram_size) {
    std::vector<Packet> output{};
    if (datagram_size < 576 || datagram_size > 1500 || opus.empty() || opus.size() > 1275 ||
        opus.size() + 2 > datagram_size - kEnvelopeSize - kAudioRtpHeader - kAudioFecHeader)
        return output;
    const auto shard_size = datagram_size - kEnvelopeSize - kAudioRtpHeader - kAudioFecHeader;
    if (shards_.empty())
        shards_.assign(6, Packet(shard_size, 0));
    if (shards_.front().size() != shard_size)
        return output; // Packet size is immutable for this sender lifetime; do not mix FEC profiles mid-block.
    auto& shard = shards_[sequence_ % 4];
    std::ranges::fill(shard, 0);
    wire::Put(shard, 0, opus.size(), 2);
    std::ranges::copy(opus, shard.begin() + 2);
    auto data = MakeRtp(kAudioPayloadType, sequence_, timestamp_);
    data.insert(data.end(), shard.begin(), shard.end());
    output.push_back(WrapMedia(MediaKind::kAudio, 0, data));
    if (sequence_ % 4 == 3 && NanorsCodec::Encode(shards_, 4, FecProfile::kAudio4Plus2)) {
        for (std::size_t index{}; index < 2; ++index) {
            auto parity = MakeRtp(kFecPayloadType, static_cast<std::uint16_t>(sequence_ + index + 1), 0);
            parity.resize(kAudioRtpHeader + kAudioFecHeader, 0);
            parity[12] = static_cast<std::uint8_t>(index);
            parity[13] = kAudioPayloadType;
            wire::Put(parity, 14, static_cast<std::uint16_t>(sequence_ - 3), 2);
            wire::Put(parity, 16, timestamp_ - 3 * kAudioPacketDurationMs, 4);
            parity.insert(parity.end(), shards_[4 + index].begin(), shards_[4 + index].end());
            output.push_back(WrapMedia(MediaKind::kAudio, 0, parity));
        }
    }
    ++sequence_;
    timestamp_ += kAudioPacketDurationMs;
    return output;
}
std::optional<Packet> UnwrapAudioPayload(std::span<const std::uint8_t> protected_payload) {
    if (protected_payload.size() < 3)
        return std::nullopt;
    const auto size = static_cast<std::size_t>(wire::Get(protected_payload, 0, 2));
    if (size == 0 || size > 1275 || size + 2 > protected_payload.size())
        return std::nullopt;
    return Packet(protected_payload.begin() + 2, protected_payload.begin() + 2 + size);
}

void AudioReceiveQueue::Reset() {
    blocks_.clear();
    oldest_.reset();
    next_ = 0;
    last_oos_ = 0;
    synchronizing_ = true;
    received_oos_ = false;
}
bool AudioReceiveQueue::Ready() const {
    if (blocks_.empty())
        return false;
    const auto& head = blocks_.front();
    return head.discontinuity || (!head.missing[head.next] && static_cast<std::uint16_t>(head.base + head.next) == next_);
}
void AudioReceiveQueue::RemoveHead() {
    oldest_ = static_cast<std::uint16_t>(blocks_.front().base + 4);
    blocks_.pop_front();
    synchronizing_ = false;
}
AudioQueueOutput AudioReceiveQueue::Feed(std::span<const std::uint8_t> rtp, std::uint64_t now_us) {
    AudioQueueOutput output{};
    if (rtp.size() <= kAudioRtpHeader || rtp.size() > 1492 || rtp[0] != 0x80 || (rtp[1] != kAudioPayloadType && rtp[1] != kFecPayloadType)) {
        output.rejected = true;
        return output;
    }
    const bool data = rtp[1] == kAudioPayloadType;
    const auto sequence = static_cast<std::uint16_t>(wire::Get(rtp, 2, 2));
    auto base = static_cast<std::uint16_t>(sequence / 4 * 4);
    auto timestamp = static_cast<std::uint32_t>(wire::Get(rtp, 4, 4) - (sequence - base) * kAudioPacketDurationMs);
    auto ssrc = static_cast<std::uint32_t>(wire::Get(rtp, 8, 4));
    std::size_t index = sequence % 4;
    std::size_t offset = kAudioRtpHeader;
    if (!data) {
        if (rtp.size() <= kAudioRtpHeader + kAudioFecHeader || rtp[12] >= 2 || rtp[13] != kAudioPayloadType || wire::Get(rtp, 14, 2) % 4 != 0) {
            output.rejected = true;
            return output;
        }
        base = static_cast<std::uint16_t>(wire::Get(rtp, 14, 2));
        timestamp = static_cast<std::uint32_t>(wire::Get(rtp, 16, 4));
        ssrc = static_cast<std::uint32_t>(wire::Get(rtp, 20, 4));
        index = 4 + rtp[12];
        offset += kAudioFecHeader;
    }
    if (!oldest_) {
        oldest_ = static_cast<std::uint16_t>(base + 4);
        next_ = *oldest_;
        return output; // Like Moonlight, start on the next group rather than declare losses from a partial initial group.
    }
    if (data && !synchronizing_ && Before(sequence, *oldest_)) {
        last_oos_ = sequence;
        received_oos_ = true;
    } else if (data && received_oos_ && Before(*oldest_, last_oos_)) {
        received_oos_ = false;
    }
    if (Before(base, *oldest_))
        return output;
    auto position = std::ranges::find_if(blocks_, [base](const Block& block) { return !Before(block.base, base); });
    if (position == blocks_.end() || position->base != base) {
        if (blocks_.size() >= 64) {
            output.rejected = true;
            return output; // Explicit resource ceiling; not a second jitter algorithm or legacy compatibility fallback.
        }
        Block block{};
        block.base = base;
        block.timestamp = timestamp;
        block.ssrc = ssrc;
        block.queued_us = now_us;
        block.shards.assign(6, Packet(rtp.size() - offset, 0));
        position = blocks_.insert(position, std::move(block));
    }
    auto& block = *position;
    if (block.complete || !block.missing[index])
        return output;
    if (block.timestamp != timestamp || block.ssrc != ssrc || block.shards.front().size() != rtp.size() - offset) {
        output.rejected = true;
        return output;
    }
    block.shards[index].assign(rtp.begin() + offset, rtp.end());
    block.missing[index] = 0;
    ++block.received;
    if (data) {
        ++block.data_received;
        if (sequence == next_) {
            output.packets.push_back({sequence, block.shards[index]});
            ++next_;
            ++block.next;
            if (block.next == 4)
                RemoveHead();
            return output; // Upstream fast path intentionally does not drain other queued packets here.
        }
    }
    if (block.received >= 4 && (block.data_received == 4 || NanorsCodec::Decode(block.shards, block.missing, 4, FecProfile::kAudio4Plus2))) {
        output.recovered += 4 - block.data_received;
        std::fill_n(block.missing.begin(), 4, 0);
        block.complete = true;
    }
    if (!Ready() && !blocks_.empty()) {
        auto& head = blocks_.front();
        if (Before(next_, head.base)) {
            next_ = head.base;
            oldest_ = head.base; // A fully absent group resynchronizes without inventing unbounded PLC packets.
        } else if (blocks_.size() > 1 &&
                   (!received_oos_ || (now_us >= head.queued_us && now_us - head.queued_us > kAudioPacketDurationMs * 4 + 10000))) {
            // Preserve the pinned upstream expression (duration is not multiplied by 1000 here).
            head.discontinuity = true;
        }
    }
    while (Ready()) {
        auto& head = blocks_.front();
        output.packets.push_back({next_, head.missing[head.next] ? Packet{} : head.shards[head.next]});
        ++next_;
        ++head.next;
        if (head.next == 4)
            RemoveHead();
    }
    return output;
}
} // namespace px::media
