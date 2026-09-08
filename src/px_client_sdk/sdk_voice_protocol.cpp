#include "sdk_voice_protocol.h"

#include "px_voice_call/voice_audio_format.h"

namespace px {

Message MakeVoiceCallRequestMessage(const std::string& device_id, const std::string& stream_id, const std::string& call_id, std::uint64_t request_id,
                                    bool connect) {
    Message message{};
    message.set_type(kVoiceCallRequest);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& request = *message.mutable_voice_call_request();
    request.set_call_id(call_id);
    request.set_request_id(request_id);
    request.set_connect(connect);
    return message;
}

Message MakeVoiceAudioConfigMessage(const std::string& device_id, const std::string& stream_id, const std::string& call_id) {
    Message message{};
    message.set_type(kVoiceAudioConfig);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& config = *message.mutable_voice_audio_config();
    config.set_call_id(call_id);
    config.set_sample_rate(VoiceAudioFormat::kSampleRate);
    config.set_channels(VoiceAudioFormat::kChannels);
    config.set_frame_ms(VoiceAudioFormat::kFrameMs);
    config.set_bitrate_bps(VoiceAudioFormat::kBitrateBps);
    config.set_fec(true);
    config.set_dtx(false);
    return message;
}

Message MakeVoiceAudioFrameMessage(const std::string& device_id, const std::string& stream_id, const std::string& call_id, std::uint32_t sequence,
                                   std::uint64_t capture_time_ms, std::span<const std::uint8_t> opus) {
    Message message{};
    message.set_type(kVoiceAudioFrame);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& frame = *message.mutable_voice_audio_frame();
    frame.set_call_id(call_id);
    frame.set_sequence(sequence);
    frame.set_capture_time_ms(capture_time_ms);
    if (!opus.empty()) {
        frame.set_opus(opus.data(), opus.size());
    }
    return message;
}

Message MakeVoiceCallResponseMessage(const std::string& device_id, const std::string& stream_id, const std::string& call_id, std::uint64_t request_id,
                                     bool accepted, const std::string& reason) {
    Message message{};
    message.set_type(kVoiceCallResponse);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto& response = *message.mutable_voice_call_response();
    response.set_call_id(call_id);
    response.set_request_id(request_id);
    response.set_accepted(accepted);
    response.set_reason(reason);
    return message;
}

} // namespace px
