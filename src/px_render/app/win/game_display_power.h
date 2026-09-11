#pragma once

#include "px_common/win32/unique_win_handle.h"

namespace px {

// An application-scoped request, not a machine power-plan change. Closing the
// handle also releases the request if the owning Render process is terminated.
class GameDisplayPower final {
    struct ConstructionKey {};

  public:
    explicit GameDisplayPower(ConstructionKey) {}
    GameDisplayPower(const GameDisplayPower&) = delete;
    GameDisplayPower& operator=(const GameDisplayPower&) = delete;
    ~GameDisplayPower() {
        Stop();
    }

    [[nodiscard]] static std::unique_ptr<GameDisplayPower> Acquire() {
        auto lease = std::make_unique<GameDisplayPower>(ConstructionKey{});
        wchar_t reason[]{L"Pixels active game rendering"};
        REASON_CONTEXT context{};
        context.Version = POWER_REQUEST_CONTEXT_VERSION;
        context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        context.Reason.SimpleReasonString = reason;
        lease->request_.reset(PowerCreateRequest(&context));
        if (!lease->request_ || lease->request_.get() == INVALID_HANDLE_VALUE) {
            return {};
        }
        lease->system_active_ = PowerSetRequest(lease->request_.get(), PowerRequestSystemRequired) != FALSE;
        if (!lease->system_active_) {
            return {};
        }
        lease->display_active_ = PowerSetRequest(lease->request_.get(), PowerRequestDisplayRequired) != FALSE;
        // DisplayRequired prevents idle blanking; the one-shot notification also
        // wakes an already idle display before Unity starts its graphics loop.
        if (!lease->display_active_ || SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) == 0) {
            return {};
        }
        return lease;
    }

    [[nodiscard]] bool Active() const {
        return system_active_ && display_active_;
    }

    void Stop() noexcept {
        if (display_active_) {
            PowerClearRequest(request_.get(), PowerRequestDisplayRequired);
            display_active_ = false;
        }
        if (system_active_) {
            PowerClearRequest(request_.get(), PowerRequestSystemRequired);
            system_active_ = false;
        }
        request_.reset();
    }

  private:
    UniqueWinHandle request_{};
    bool system_active_{};
    bool display_active_{};
};

} // namespace px
