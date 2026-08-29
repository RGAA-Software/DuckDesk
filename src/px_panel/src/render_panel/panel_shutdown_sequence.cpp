#include "panel_shutdown_sequence.h"

#include <exception>
#include <utility>

#include "px_common_new/log.h"

namespace px {

std::shared_ptr<PanelShutdownSequence> PanelShutdownSequence::Make(
    Scheduler ui_scheduler,
    Scheduler worker_scheduler,
    Hooks hooks) {
    return std::make_shared<PanelShutdownSequence>(
        ConstructionToken{}, std::move(ui_scheduler),
        std::move(worker_scheduler), std::move(hooks));
}

PanelShutdownSequence::PanelShutdownSequence(
    ConstructionToken,
    Scheduler ui_scheduler,
    Scheduler worker_scheduler,
    Hooks hooks)
    : ui_scheduler_(std::move(ui_scheduler)),
      worker_scheduler_(std::move(worker_scheduler)),
      hooks_(std::move(hooks)) {}

void PanelShutdownSequence::Start() {
    if (started_.exchange(true) || !ui_scheduler_) {
        return;
    }
    const auto weak_self = weak_from_this();
    ui_scheduler_([weak_self]() {
        const auto self = weak_self.lock();
        if (self) {
            self->Prepare();
        }
    }, 500);
}

bool PanelShutdownSequence::IsCompleted() const {
    return completed_.load();
}

void PanelShutdownSequence::Prepare() {
    if (completed_) {
        return;
    }
    try {
        if (hooks_.prepare) {
            hooks_.prepare();
        }
    } catch (const std::exception& error) {
        LOGE("Panel shutdown prepare failed: {}", error.what());
    } catch (...) {
        LOGE("Panel shutdown prepare failed with unknown exception");
    }

    if (!worker_scheduler_) {
        RunFallbackCleanup();
        return;
    }
    const auto weak_self = weak_from_this();
    worker_scheduler_([weak_self]() {
        const auto self = weak_self.lock();
        if (self) {
            self->LaunchServiceHelper();
        }
    }, 800);
}

void PanelShutdownSequence::LaunchServiceHelper() {
    if (completed_) {
        return;
    }
    bool helper_launched = false;
    try {
        if (hooks_.launch_service_helper) {
            helper_launched = hooks_.launch_service_helper();
        }
    } catch (const std::exception& error) {
        LOGE("Panel shutdown helper failed: {}", error.what());
    } catch (...) {
        LOGE("Panel shutdown helper failed with unknown exception");
    }

    if (!worker_scheduler_) {
        RunFallbackCleanup();
        return;
    }
    const auto weak_self = weak_from_this();
    worker_scheduler_([weak_self]() {
        const auto self = weak_self.lock();
        if (self) {
            self->RunFallbackCleanup();
        }
    }, helper_launched ? 1500 : 0);
}

void PanelShutdownSequence::RunFallbackCleanup() {
    if (completed_.exchange(true)) {
        return;
    }
    try {
        if (hooks_.fallback_cleanup) {
            hooks_.fallback_cleanup();
        }
    } catch (const std::exception& error) {
        LOGE("Panel shutdown fallback failed: {}", error.what());
    } catch (...) {
        LOGE("Panel shutdown fallback failed with unknown exception");
    }
}

}  // namespace px
