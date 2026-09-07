#ifndef GAMMARAYPC_VIRTUAL_DISPLAY_UI_STATE_H
#define GAMMARAYPC_VIRTUAL_DISPLAY_UI_STATE_H

#include <cstdint>
#include <string>

#include "px_common/virtual_display_limits.h"

namespace px {

    enum class VirtualDisplayUiOperation {
        kCreate,
        kRemoveLast,
    };

    enum class VirtualDisplayUiPhase {
        kIdle,
        kRequesting,
        kReconnecting,
    };

    enum class VirtualDisplayUiResultEffect {
        kIgnored,
        kFailed,
        kCompleted,
        kAwaitingReconnect,
    };

    class VirtualDisplayUiState {
    public:
        void ApplyStatus(bool enabled, uint32_t owned, uint32_t maximum, uint64_t generation);
        bool BeginRequest(VirtualDisplayUiOperation operation, std::string request_id);
        VirtualDisplayUiResultEffect ApplyResult(
            const std::string& request_id,
            bool accepted,
            bool need_reconnect,
            uint32_t owned,
            uint32_t maximum,
            uint64_t generation);
        bool CompleteReconnect();
        bool Timeout(const std::string& request_id);

        [[nodiscard]] bool CanAdd() const;
        [[nodiscard]] bool CanRemove() const;
        [[nodiscard]] bool IsBusy() const;
        [[nodiscard]] bool Enabled() const { return enabled_; }
        [[nodiscard]] uint32_t Owned() const { return owned_; }
        [[nodiscard]] uint32_t Maximum() const { return maximum_; }
        [[nodiscard]] uint64_t Generation() const { return generation_; }
        [[nodiscard]] VirtualDisplayUiPhase Phase() const { return phase_; }
        [[nodiscard]] const std::string& PendingRequestId() const { return pending_request_id_; }

    private:
        void FinishRequest();

        bool enabled_ = false;
        uint32_t owned_ = 0;
        uint32_t maximum_ = kVirtualDisplayMaximumCount;
        uint64_t generation_ = 0;
        VirtualDisplayUiPhase phase_ = VirtualDisplayUiPhase::kIdle;
        std::string pending_request_id_;
        uint32_t reconnect_owned_ = 0;
        uint64_t reconnect_generation_ = 0;
    };

}

#endif // GAMMARAYPC_VIRTUAL_DISPLAY_UI_STATE_H
