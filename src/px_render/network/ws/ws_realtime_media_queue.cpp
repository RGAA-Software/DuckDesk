#include "ws_realtime_media_queue.h"

#include "px_common/data.h"
#include "px_message.pb.h"

#include <limits>
#include <optional>

namespace px {
namespace {

enum class ProtobufWireType : std::uint8_t {
    Varint = 0U,
    Fixed64 = 1U,
    LengthDelimited = 2U,
    Fixed32 = 5U,
};

constexpr std::uint64_t kWireTypeMask{0x07U};
constexpr unsigned int kWireTypeBitCount{3U};
constexpr unsigned int kVarintPayloadBitCount{7U};
constexpr std::uint8_t kVarintPayloadMask{0x7fU};
constexpr std::uint8_t kVarintContinuationMask{0x80U};
constexpr std::size_t kFixed64Bytes{8U};
constexpr std::size_t kFixed32Bytes{4U};

std::optional<std::uint64_t> ExtractMessageType(const std::span<const char> payload) {
    std::size_t offset{};
    const auto read_varint = [&payload, &offset]() -> std::optional<std::uint64_t> {
        std::uint64_t value{};
        for (unsigned int shift{}; shift < std::numeric_limits<std::uint64_t>::digits && offset < payload.size(); shift += kVarintPayloadBitCount) {
            const auto byte = static_cast<std::uint8_t>(payload[offset++]);
            value |= static_cast<std::uint64_t>(byte & kVarintPayloadMask) << shift;
            if ((byte & kVarintContinuationMask) == 0U) {
                return value;
            }
        }
        return std::nullopt;
    };

    while (offset < payload.size()) {
        const auto tag = read_varint();
        if (!tag || *tag == 0U) {
            return std::nullopt;
        }
        const auto field = static_cast<std::uint32_t>(*tag >> kWireTypeBitCount);
        const auto wire = static_cast<ProtobufWireType>(*tag & kWireTypeMask);
        if (field == Message::kTypeFieldNumber) {
            return wire == ProtobufWireType::Varint ? read_varint() : std::nullopt;
        }
        switch (wire) {
        case ProtobufWireType::Varint:
            if (!read_varint()) {
                return std::nullopt;
            }
            break;
        case ProtobufWireType::Fixed64:
            if (payload.size() - offset < kFixed64Bytes) {
                return std::nullopt;
            }
            offset += kFixed64Bytes;
            break;
        case ProtobufWireType::LengthDelimited: {
            const auto length = read_varint();
            if (!length || *length > payload.size() - offset) {
                return std::nullopt;
            }
            offset += static_cast<std::size_t>(*length);
            break;
        }
        case ProtobufWireType::Fixed32:
            if (payload.size() - offset < kFixed32Bytes) {
                return std::nullopt;
            }
            offset += kFixed32Bytes;
            break;
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

WsRealtimeMediaKind ClassifyWsRealtimeMedia(const std::shared_ptr<Data>& message) {
    if (!message || message->Size() == 0U) {
        return WsRealtimeMediaKind::None;
    }
    const auto type = ExtractMessageType(message->Bytes());
    if (!type) {
        return WsRealtimeMediaKind::None;
    }
    if (*type == static_cast<std::uint64_t>(MessageType::kVideoFrame)) {
        return WsRealtimeMediaKind::Video;
    }
    if (*type == static_cast<std::uint64_t>(MessageType::kAudioFrame) || *type == static_cast<std::uint64_t>(MessageType::kVoiceAudioFrame)) {
        return WsRealtimeMediaKind::Audio;
    }
    return WsRealtimeMediaKind::None;
}

bool WsRealtimeMediaQueueBudget::TryReserve(const std::size_t bytes) {
    std::lock_guard lock{mutex_};
    if (pendingMessages_ >= kMaxMessages) {
        return false;
    }
    // Permit one oversized key frame when the queue is empty; otherwise a high
    // resolution stream could be unable to send the recovery frame it needs.
    if (pendingMessages_ != 0U && (bytes > kMaxBytes || pendingBytes_ > kMaxBytes - bytes)) {
        return false;
    }
    ++pendingMessages_;
    pendingBytes_ += bytes;
    return true;
}

void WsRealtimeMediaQueueBudget::Release(const std::size_t bytes) {
    std::lock_guard lock{mutex_};
    if (pendingMessages_ > 0U) {
        --pendingMessages_;
    }
    pendingBytes_ = bytes >= pendingBytes_ ? 0U : pendingBytes_ - bytes;
}

std::size_t WsRealtimeMediaQueueBudget::PendingMessages() const {
    std::lock_guard lock{mutex_};
    return pendingMessages_;
}

std::size_t WsRealtimeMediaQueueBudget::PendingBytes() const {
    std::lock_guard lock{mutex_};
    return pendingBytes_;
}

} // namespace px
