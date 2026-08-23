#include "voice_call_state.h"

#include <utility>

namespace px {

bool VoiceCallState::BeginOutgoing(
    std::string call_id, uint64_t request_id, uint64_t now_ms) {
    if (phase_ != VoiceCallPhase::kIdle || call_id.empty() || request_id == 0) {
        return false;
    }
    phase_ = VoiceCallPhase::kOutgoingPending;
    call_id_ = std::move(call_id);
    request_id_ = request_id;
    deadline_ms_ = now_ms + kRequestTimeoutMs;
    return true;
}

IncomingVoiceCallResult VoiceCallState::BeginIncoming(
    std::string call_id, uint64_t request_id, uint64_t now_ms) {
    if (call_id.empty() || request_id == 0) {
        return IncomingVoiceCallResult::kInvalid;
    }
    if (phase_ != VoiceCallPhase::kIdle) {
        return Matches(call_id, request_id)
            ? IncomingVoiceCallResult::kDuplicate
            : IncomingVoiceCallResult::kBusy;
    }
    phase_ = VoiceCallPhase::kIncomingPending;
    call_id_ = std::move(call_id);
    request_id_ = request_id;
    deadline_ms_ = now_ms + kRequestTimeoutMs;
    return IncomingVoiceCallResult::kPending;
}

bool VoiceCallState::AcceptIncoming(const std::string& call_id, uint64_t request_id) {
    if (phase_ != VoiceCallPhase::kIncomingPending || !Matches(call_id, request_id)) {
        return false;
    }
    phase_ = VoiceCallPhase::kConnected;
    deadline_ms_ = 0;
    return true;
}

bool VoiceCallState::RejectIncoming(const std::string& call_id, uint64_t request_id) {
    if (phase_ != VoiceCallPhase::kIncomingPending || !Matches(call_id, request_id)) {
        return false;
    }
    Reset();
    return true;
}

bool VoiceCallState::ApplyResponse(
    const std::string& call_id, uint64_t request_id, bool accepted) {
    if (phase_ != VoiceCallPhase::kOutgoingPending || !Matches(call_id, request_id)) {
        return false;
    }
    if (!accepted) {
        Reset();
        return true;
    }
    phase_ = VoiceCallPhase::kConnected;
    deadline_ms_ = 0;
    return true;
}

bool VoiceCallState::HangUp(const std::string& call_id) {
    if (phase_ == VoiceCallPhase::kIdle || call_id.empty() || call_id != call_id_) {
        return false;
    }
    Reset();
    return true;
}

bool VoiceCallState::Expire(uint64_t now_ms) {
    if ((phase_ != VoiceCallPhase::kOutgoingPending &&
         phase_ != VoiceCallPhase::kIncomingPending) ||
        deadline_ms_ == 0 || now_ms < deadline_ms_) {
        return false;
    }
    Reset();
    return true;
}

void VoiceCallState::Reset() {
    phase_ = VoiceCallPhase::kIdle;
    call_id_.clear();
    request_id_ = 0;
    deadline_ms_ = 0;
    last_rx_sequence_ = 0;
    has_rx_sequence_ = false;
}

bool VoiceCallState::AcceptMedia(const std::string& call_id, uint32_t sequence) {
    if (!IsMediaAllowed(call_id)) {
        return false;
    }
    // Signed subtraction is the conventional wrap-safe sequence comparison.
    if (has_rx_sequence_ && static_cast<int32_t>(sequence - last_rx_sequence_) <= 0) {
        return false;
    }
    last_rx_sequence_ = sequence;
    has_rx_sequence_ = true;
    return true;
}

bool VoiceCallState::IsMediaAllowed(const std::string& call_id) const {
    return phase_ == VoiceCallPhase::kConnected && !call_id.empty() && call_id == call_id_;
}

bool VoiceCallState::Matches(const std::string& call_id, uint64_t request_id) const {
    return call_id == call_id_ && request_id == request_id_;
}

}  // namespace px
