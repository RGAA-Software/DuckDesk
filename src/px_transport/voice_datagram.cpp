#include "voice_datagram.h"

#include <algorithm>
#include <array>

#include "media_datagrams.h"

namespace px::transport {
namespace {
constexpr std::array<std::uint8_t, 4> kVoiceHeader{'P', 'X', 'V', 1};

bool ValidVoice(const Message& message) {
    if (message.type() != kVoiceAudioFrame || !message.has_voice_audio_frame()) return false;
    const auto& frame = message.voice_audio_frame();
    return !frame.call_id().empty() && frame.call_id().size() <= 128 && !frame.opus().empty() && frame.opus().size() <= 1275;
}
}  // namespace

Bytes EncodeVoiceDatagram(const Message& message) {
    if (!ValidVoice(message) || message.ByteSizeLong() > kMediaDatagramBytes - kVoiceHeader.size()) return {};
    const auto encoded = message.SerializeAsString();
    Bytes packet(kVoiceHeader.begin(), kVoiceHeader.end());
    packet.insert(packet.end(), encoded.begin(), encoded.end());
    return packet;
}

std::shared_ptr<Message> DecodeVoiceDatagram(std::span<const std::uint8_t> payload) {
    if (payload.size() <= kVoiceHeader.size() || payload.size() > kMediaDatagramBytes ||
        !std::equal(kVoiceHeader.begin(), kVoiceHeader.end(), payload.begin()))
        return {};
    const auto encoded = payload.subspan(kVoiceHeader.size());
    const auto message = std::make_shared<Message>();
    if (!message->ParseFromArray(encoded.data(), static_cast<int>(encoded.size())) || !ValidVoice(*message)) return {};
    return message;
}
}  // namespace px::transport
