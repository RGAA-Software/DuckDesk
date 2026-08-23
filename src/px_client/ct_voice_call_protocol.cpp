#include "ct_voice_call_protocol.h"

#include <atomic>
#include <chrono>

#include "px_voice_call/voice_audio_endpoint.h"

namespace px {

uint64_t NextNativeVoiceCallRequestId() {
    static std::atomic_uint64_t sequence{
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    auto value = ++sequence;
    if (value == 0) {
        value = ++sequence;
    }
    return value;
}

Message MakeVoiceCallRequestMessage(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id, uint64_t request_id, bool connect) {
    Message message;
    message.set_type(kVoiceCallRequest);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* request = message.mutable_voice_call_request();
    request->set_call_id(call_id);
    request->set_request_id(request_id);
    request->set_connect(connect);
    return message;
}

Message MakeVoiceAudioConfigMessage(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id) {
    Message message;
    message.set_type(kVoiceAudioConfig);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* config = message.mutable_voice_audio_config();
    config->set_call_id(call_id);
    config->set_sample_rate(VoiceAudioEndpoint::kSampleRate);
    config->set_channels(VoiceAudioEndpoint::kChannels);
    config->set_frame_ms(VoiceAudioEndpoint::kFrameMs);
    config->set_bitrate_bps(VoiceAudioEndpoint::kBitrateBps);
    config->set_fec(true);
    config->set_dtx(false);
    return message;
}

Message MakeVoiceAudioFrameMessage(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
    const std::vector<uint8_t>& opus) {
    Message message;
    message.set_type(kVoiceAudioFrame);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* frame = message.mutable_voice_audio_frame();
    frame->set_call_id(call_id);
    frame->set_sequence(sequence);
    frame->set_capture_time_ms(capture_time_ms);
    frame->set_opus(opus.data(), opus.size());
    return message;
}

}  // namespace px
