#include "voice_call_state.h"
#include "px_common_new/privacy_log.h"

#include <utility>

namespace px {

std::string VoiceCallLogId(std::string_view value) {
    return PrivacyLogId(value);
}

bool VoiceCallState::BeginOutgoing(
    std::string call_id, uint64_t request_id, uint64_t now_ms) {
    if (phase_ != VoiceCallPhase::kIdle || call_id.empty() ||
        call_id.size() > kMaxCallIdBytes || request_id == 0) {
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
    if (call_id.empty() || call_id.size() > kMaxCallIdBytes || request_id == 0) {
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
    highest_rx_sequence_ = 0;
    rx_sequence_window_ = 0;
    has_rx_sequence_ = false;
}

bool VoiceCallState::AcceptMedia(const std::string& call_id, uint32_t sequence) {
    if (!IsMediaAllowed(call_id)) {
        return false;
    }
    if (!has_rx_sequence_) {
        highest_rx_sequence_ = sequence;
        rx_sequence_window_ = 1;
        has_rx_sequence_ = true;
        return true;
    }

    // A 64-packet replay window admits bounded reordering for the jitter
    // buffer while rejecting duplicates and packets too old to be useful.
    const int32_t delta = static_cast<int32_t>(sequence - highest_rx_sequence_);
    if (delta > 0) {
        rx_sequence_window_ = delta >= 64
            ? 1
            : (rx_sequence_window_ << delta) | 1;
        highest_rx_sequence_ = sequence;
        return true;
    }
    const uint32_t distance = static_cast<uint32_t>(-static_cast<int64_t>(delta));
    if (distance >= 64) {
        return false;
    }
    const uint64_t bit = uint64_t{1} << distance;
    if ((rx_sequence_window_ & bit) != 0) {
        return false;
    }
    rx_sequence_window_ |= bit;
    return true;
}

bool VoiceCallState::IsMediaAllowed(const std::string& call_id) const {
    return phase_ == VoiceCallPhase::kConnected && !call_id.empty() && call_id == call_id_;
}

bool VoiceCallState::Matches(const std::string& call_id, uint64_t request_id) const {
    return call_id == call_id_ && request_id == request_id_;
}

}  // namespace px
