#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>

#include "px_message.pb.h"

namespace px {

// Correlation is the pair (call UUID, request sequence). Each controller owns its sequence.
class VoiceCallRequestSequence final {
  public:
    [[nodiscard]] std::uint64_t Next() {
        auto value = ++sequence_;
        if (value == 0) {
            value = ++sequence_;
        }
        return value;
    }

  private:
    std::atomic_uint64_t sequence_{static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
};

Message MakeVoiceCallRequestMessage(const std::string& device_id, const std::string& stream_id, const std::string& call_id, std::uint64_t request_id,
                                    bool connect);
Message MakeVoiceAudioConfigMessage(const std::string& device_id, const std::string& stream_id, const std::string& call_id);
Message MakeVoiceAudioFrameMessage(const std::string& device_id, const std::string& stream_id, const std::string& call_id, std::uint32_t sequence,
                                   std::uint64_t capture_time_ms, std::span<const std::uint8_t> opus);
Message MakeVoiceCallResponseMessage(const std::string& device_id, const std::string& stream_id, const std::string& call_id, std::uint64_t request_id,
                                     bool accepted, const std::string& reason);

} // namespace px
