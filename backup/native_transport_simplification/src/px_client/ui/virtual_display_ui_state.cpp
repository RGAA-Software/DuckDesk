#include "virtual_display_ui_state.h"

#include <algorithm>
#include <utility>

namespace px {

    namespace {
        uint32_t NormalizeMaximum(uint32_t maximum) {
            return maximum == 0 ? kVirtualDisplayMaximumCount : maximum;
        }
    }

    void VirtualDisplayUiState::ApplyStatus(
        bool enabled,
        uint32_t owned,
        uint32_t maximum,
        uint64_t generation) {
        enabled_ = enabled;
        owned_ = owned;
        maximum_ = NormalizeMaximum(maximum);
        generation_ = generation;

        if (!enabled_ && IsBusy()) {
            FinishRequest();
            return;
        }
        if (phase_ == VirtualDisplayUiPhase::kReconnecting &&
            generation_ >= reconnect_generation_ && owned_ == reconnect_owned_) {
            FinishRequest();
        }
    }

    bool VirtualDisplayUiState::BeginRequest(
        VirtualDisplayUiOperation operation,
        std::string request_id) {
        if (request_id.empty() || IsBusy()) {
            return false;
        }
        if (operation == VirtualDisplayUiOperation::kCreate ? !CanAdd() : !CanRemove()) {
            return false;
        }
        pending_request_id_ = std::move(request_id);
        phase_ = VirtualDisplayUiPhase::kRequesting;
        return true;
    }

    VirtualDisplayUiResultEffect VirtualDisplayUiState::ApplyResult(
        const std::string& request_id,
        bool accepted,
        bool need_reconnect,
        uint32_t owned,
        uint32_t maximum,
        uint64_t generation) {
        owned_ = owned;
        maximum_ = NormalizeMaximum(maximum);
        generation_ = std::max(generation_, generation);

        if (request_id.empty() || request_id != pending_request_id_) {
            return VirtualDisplayUiResultEffect::kIgnored;
        }
        if (!accepted) {
            FinishRequest();
            return VirtualDisplayUiResultEffect::kFailed;
        }
        if (need_reconnect) {
            phase_ = VirtualDisplayUiPhase::kReconnecting;
            reconnect_owned_ = owned_;
            reconnect_generation_ = generation_;
            return VirtualDisplayUiResultEffect::kAwaitingReconnect;
        }
        FinishRequest();
        return VirtualDisplayUiResultEffect::kCompleted;
    }

    bool VirtualDisplayUiState::CompleteReconnect() {
        if (phase_ != VirtualDisplayUiPhase::kReconnecting) {
            return false;
        }
        FinishRequest();
        return true;
    }

    bool VirtualDisplayUiState::Timeout(const std::string& request_id) {
        if (request_id.empty() || request_id != pending_request_id_ || !IsBusy()) {
            return false;
        }
        FinishRequest();
        return true;
    }

    bool VirtualDisplayUiState::CanAdd() const {
        return enabled_ && !IsBusy() && owned_ < maximum_;
    }

    bool VirtualDisplayUiState::CanRemove() const {
        return enabled_ && !IsBusy() && owned_ > 0;
    }

    bool VirtualDisplayUiState::IsBusy() const {
        return phase_ != VirtualDisplayUiPhase::kIdle;
    }

    void VirtualDisplayUiState::FinishRequest() {
        phase_ = VirtualDisplayUiPhase::kIdle;
        pending_request_id_.clear();
        reconnect_owned_ = 0;
        reconnect_generation_ = 0;
    }

}
