#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "px_message.pb.h"

namespace px {

uint64_t NextNativeVoiceCallRequestId();

Message MakeVoiceCallRequestMessage(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id, uint64_t request_id, bool connect);
Message MakeVoiceAudioConfigMessage(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id);
Message MakeVoiceAudioFrameMessage(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
    const std::vector<uint8_t>& opus);

}  // namespace px
