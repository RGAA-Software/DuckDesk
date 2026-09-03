#include "services/input_replay_service.h"

#include <utility>

#include "app/app_messages.h"
#include "px_common_new/log.h"
#include "px_common_new/process_util.h"
#include "px_message.pb.h"
#include "win_event_replayer.h"

namespace px::render {
namespace {

RenderError MakeInputError(std::string operation, std::string reason) {
    return RenderError{
        .code = RenderErrorCode::kModuleLifecycleRejected,
        .component = "input_replay",
        .operation = std::move(operation),
        .stage = "service",
        .reason = std::move(reason),
        .recoverable = true,
    };
}

}  // namespace

std::shared_ptr<InputReplayService> InputReplayService::Create() {
    return std::make_shared<InputReplayService>();
}

InputReplayService::~InputReplayService() {
    static_cast<void>(Stop());
}

BuiltinModuleRegistration InputReplayService::MakeRegistration() {
    const std::weak_ptr<InputReplayService> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kInputReplayModuleId),
            .name = "Input Replay",
            .author = "GammaRay",
            .description = "Built-in lease-gated Windows input service",
            .version_name = "2.0.0",
            .version_code = 200,
            .capability = BuiltinModuleCapability::kService,
            .default_enabled = true,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner
                ? owner->Start()
                : ModuleLifecycleResult(std::unexpected(MakeInputError(
                      "start", "service owner expired")));
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner ? owner->Stop() : ModuleLifecycleResult{};
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            return owner
                ? owner->SetEnabled(enabled)
                : ModuleLifecycleResult(std::unexpected(MakeInputError(
                      "set_enabled", "service owner expired")));
        },
    };
}

ModuleLifecycleResult InputReplayService::Start() {
    std::lock_guard lock(mutex_);
    if (running_) {
        return {};
    }
    replayer_ = std::make_shared<WinEventReplayer>();
    running_ = true;
    LOGI("event=service.start component=input_replay outcome=success");
    return {};
}

ModuleLifecycleResult InputReplayService::Stop() {
    std::shared_ptr<WinEventReplayer> replayer;
    {
        std::lock_guard lock(mutex_);
        if (!running_ && !replayer_) {
            return {};
        }
        running_ = false;
        replayer = std::move(replayer_);
    }
    if (replayer) {
        replayer->HandleFocusOutEvent();
    }
    LOGI("event=service.stop component=input_replay outcome=success");
    return {};
}

ModuleLifecycleResult InputReplayService::SetEnabled(const bool enabled) {
    std::shared_ptr<WinEventReplayer> replayer;
    {
        std::lock_guard lock(mutex_);
        enabled_ = enabled;
        if (!enabled) {
            replayer = replayer_;
        }
    }
    if (replayer) {
        replayer->HandleFocusOutEvent();
    }
    return {};
}

void InputReplayService::HandleMessage(
    const std::shared_ptr<Message>& message) {
    if (!message) {
        return;
    }
    const auto replayer = GetReplayer();
    if (!replayer) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        ++accepted_messages_;
    }
    switch (message->type()) {
        case MessageType::kMouseEvent:
            replayer->HandleMouseEvent(message->mouse_event());
            break;
        case MessageType::kKeyEvent:
            replayer->HandleKeyEvent(message->key_event());
            break;
        case MessageType::kFocusOutEvent:
            replayer->HandleFocusOutEvent();
            break;
        case MessageType::kExitControlledEnd:
            replayer->SimulateCtrlWinShiftB();
            ProcessUtil::KillProcess(GetCurrentProcessId());
            break;
        default:
            break;
    }
}

void InputReplayService::ReleaseInputState() {
    if (const auto replayer = GetReplayer()) {
        replayer->HandleFocusOutEvent();
    }
}

void InputReplayService::UpdateCaptureMonitorInfo(
    const CaptureMonitorInfoMessage& message) {
    if (message.monitors_.empty()) {
        return;
    }
    if (const auto replayer = GetReplayer()) {
        replayer->UpdateCaptureMonitorInfo(message);
    }
}

InputReplaySnapshot InputReplayService::Snapshot() const {
    std::lock_guard lock(mutex_);
    return InputReplaySnapshot{
        .running = running_,
        .enabled = enabled_,
        .accepted_messages = accepted_messages_,
    };
}

std::shared_ptr<WinEventReplayer> InputReplayService::GetReplayer() const {
    std::lock_guard lock(mutex_);
    return running_ && enabled_ ? replayer_ : std::shared_ptr<WinEventReplayer>{};
}

}  // namespace px::render
