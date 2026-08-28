#include "panel_rtc_config_refresh_gate.h"

#include <algorithm>

namespace px {

std::shared_ptr<PanelRtcConfigRefreshGate> PanelRtcConfigRefreshGate::Create() {
    return std::make_shared<PanelRtcConfigRefreshGate>();
}

PanelRtcConfigRefreshRequest PanelRtcConfigRefreshGate::Request(
    std::uint64_t expected_revision) {
    std::lock_guard lock(mutex_);
    if (stopped_) {
        return PanelRtcConfigRefreshRequest::kStopped;
    }
    ++request_sequence_;
    expected_revision_ = std::max(expected_revision_, expected_revision);
    if (active_) {
        return PanelRtcConfigRefreshRequest::kCoalesced;
    }
    active_ = true;
    return PanelRtcConfigRefreshRequest::kStarted;
}

PanelRtcConfigRefreshAttempt PanelRtcConfigRefreshGate::CurrentAttempt() const {
    std::lock_guard lock(mutex_);
    return {
        .sequence = request_sequence_,
        .expected_revision = expected_revision_,
    };
}

bool PanelRtcConfigRefreshGate::FinishAttempt(std::uint64_t sequence) {
    std::lock_guard lock(mutex_);
    if (stopped_) {
        active_ = false;
        return false;
    }
    if (sequence != request_sequence_) {
        return true;
    }
    active_ = false;
    return false;
}

void PanelRtcConfigRefreshGate::AbortStart() {
    std::lock_guard lock(mutex_);
    active_ = false;
}

void PanelRtcConfigRefreshGate::Stop() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    active_ = false;
}

} // namespace px
