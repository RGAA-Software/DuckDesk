#pragma once

#include <cstdint>
#include <string>

namespace px {

enum class VoiceCallPhase {
    kIdle,
    kOutgoingPending,
    kIncomingPending,
    kConnected,
};

enum class IncomingVoiceCallResult {
    kPending,
    kDuplicate,
    kBusy,
    kInvalid,
};

class VoiceCallState {
public:
    static constexpr uint64_t kRequestTimeoutMs = 30'000;

    bool BeginOutgoing(std::string call_id, uint64_t request_id, uint64_t now_ms);
    IncomingVoiceCallResult BeginIncoming(
        std::string call_id, uint64_t request_id, uint64_t now_ms);
    bool AcceptIncoming(const std::string& call_id, uint64_t request_id);
    bool RejectIncoming(const std::string& call_id, uint64_t request_id);
    bool ApplyResponse(
        const std::string& call_id, uint64_t request_id, bool accepted);
    bool HangUp(const std::string& call_id);
    bool Expire(uint64_t now_ms);
    void Reset();

    [[nodiscard]] bool AcceptMedia(const std::string& call_id, uint32_t sequence);
    [[nodiscard]] bool IsMediaAllowed(const std::string& call_id) const;
    [[nodiscard]] VoiceCallPhase Phase() const { return phase_; }
    [[nodiscard]] const std::string& CallId() const { return call_id_; }
    [[nodiscard]] uint64_t RequestId() const { return request_id_; }

private:
    [[nodiscard]] bool Matches(const std::string& call_id, uint64_t request_id) const;

    VoiceCallPhase phase_ = VoiceCallPhase::kIdle;
    std::string call_id_;
    uint64_t request_id_ = 0;
    uint64_t deadline_ms_ = 0;
    uint32_t last_rx_sequence_ = 0;
    bool has_rx_sequence_ = false;
};

}  // namespace px
