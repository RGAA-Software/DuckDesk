#include "game_text_backend.h"
#include <charconv>
#include <chrono>

namespace px {
namespace {
void CancelGameTextTimer(const std::shared_ptr<asio::steady_timer>& timer) noexcept {
    try {
        static_cast<void>(timer->cancel());
    } catch (...) {
        // Permission is already cancelled and the request removed. Even if timer
        // cancellation fails, its weak callback cannot write or complete it again.
    }
}

ApplicationTextOutcome Outcome(CaptureTextStatus status) {
    switch (status) {
    case CaptureTextStatus::kSubmitted:
        return TEXT_SUBMITTED;
    case CaptureTextStatus::kTargetChanged:
        return TEXT_TARGET_CHANGED;
    case CaptureTextStatus::kUnavailable:
        return TEXT_TARGET_UNAVAILABLE;
    case CaptureTextStatus::kInvalidText:
        return TEXT_INVALID;
    case CaptureTextStatus::kOutcomeUnknown:
        return TEXT_OUTCOME_UNKNOWN;
    case CaptureTextStatus::kBusy:
        return TEXT_BUSY;
    default:
        return TEXT_FAILED;
    }
}
} // namespace

GameTextBackend::GameTextBackend(std::shared_ptr<AppManagerWinImpl> manager, std::shared_ptr<PxAsyncRuntime> runtime, Send send)
    : manager_(std::move(manager)), runtime_(std::move(runtime)), send_(std::move(send)) {}
GameTextBackend::~GameTextBackend() {
    Stop();
}

std::string GameTextBackend::TargetPrefix(const OwnedGameTextTarget& target) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!target.process || !GetProcessTimes(target.process->get(), &created, &exited, &kernel, &user)) {
        return {};
    }
    const auto creation = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return std::to_string(target.command.target_pid) + ":" + std::to_string(creation) + ":" + std::to_string(target.command.root_window) + ":";
}

ApplicationTextBackend GameTextBackend::Adapter() {
    const auto weak = weak_from_this();
    ApplicationTextBackend adapter{};
    adapter.kind = ApplicationTextCapabilities::OWNED_HOOK_WINDOW;
    adapter.query = [weak](std::function<void(ApplicationTextBackendState)> complete) {
        const auto self = weak.lock();
        const auto manager = self ? self->manager_.lock() : std::shared_ptr<AppManagerWinImpl>{};
        auto target = manager ? manager->AcquireTextTarget() : std::optional<OwnedGameTextTarget>{};
        if (!target) {
            complete({});
            return;
        }
        const auto prefix = TargetPrefix(*target);
        self->Request(std::move(*target), [prefix, complete = std::move(complete)](CaptureTextReply reply) {
            complete(ApplicationTextBackendState{prefix + std::to_string(reply.generation),
                                                 static_cast<ApplicationTextState::Editability>(reply.editability),
                                                 !prefix.empty() && reply.status == CaptureTextStatus::kReady});
        });
    };
    adapter.release_keys = [weak](std::function<void(bool)> complete) {
        const auto self = weak.lock();
        const auto manager = self ? self->manager_.lock() : std::shared_ptr<AppManagerWinImpl>{};
        auto target = manager ? manager->AcquireTextTarget() : std::optional<OwnedGameTextTarget>{};
        if (!target) {
            complete(false);
            return;
        }
        target->command.operation = CaptureTextOperation::kRelease;
        self->Request(std::move(*target),
                      [complete = std::move(complete)](CaptureTextReply reply) { complete(reply.status == CaptureTextStatus::kReady); });
    };
    adapter.commit = [weak](std::string text, std::string generation, std::function<bool()> authorize,
                            std::function<void(ApplicationTextOutcome)> complete) {
        const auto self = weak.lock();
        const auto manager = self ? self->manager_.lock() : std::shared_ptr<AppManagerWinImpl>{};
        auto target = manager ? manager->AcquireTextTarget() : std::optional<OwnedGameTextTarget>{};
        if (!target) {
            complete(TEXT_TARGET_UNAVAILABLE);
            return;
        }
        const auto prefix = TargetPrefix(*target);
        if (prefix.empty() || !generation.starts_with(prefix)) {
            complete(TEXT_TARGET_CHANGED);
            return;
        }
        const auto suffix = std::string_view(generation).substr(prefix.size());
        const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), target->command.expected_generation);
        if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size() || target->command.expected_generation == 0) {
            complete(TEXT_TARGET_CHANGED);
            return;
        }
        if (!authorize || !authorize()) {
            complete(TEXT_PERMISSION_DENIED);
            return;
        }
        target->command.operation = CaptureTextOperation::kSubmit;
        target->command.text = std::move(text);
        self->Request(
            std::move(*target), [complete = std::move(complete)](CaptureTextReply reply) { complete(Outcome(reply.status)); }, std::move(authorize));
    };
    return adapter;
}

void GameTextBackend::Request(OwnedGameTextTarget target, Completion complete, std::function<bool()> authorize) {
    if (!runtime_ || runtime_->IsStopping() || !target.process || WaitForSingleObject(target.process->get(), 0) != WAIT_TIMEOUT) {
        complete({});
        return;
    }
    const auto timer = std::make_shared<asio::steady_timer>(runtime_->Executor(PxAsyncLane::kState));
    const auto permit = std::make_shared<GameTextWritePermit>(std::move(authorize));
    bool rejected{};
    {
        std::lock_guard lock(mutex_);
        rejected = stopped_ || pending_.size() >= 32;
        if (!rejected) {
            target.command.request_id = ++next_request_;
            pending_.emplace(target.command.request_id,
                             Pending{target.command.target_pid, target.command.text.size(), target.process, timer, permit, std::move(complete)});
            const auto request = target.command.request_id;
            timer->expires_after(std::chrono::seconds(3));
            timer->async_wait([weak = weak_from_this(), request](const asio::error_code& error) {
                if (!error) {
                    if (const auto self = weak.lock()) {
                        self->Finish(request, CaptureTextReply{.request_id = request, .status = CaptureTextStatus::kOutcomeUnknown});
                    }
                }
            });
        }
    }
    if (rejected) {
        complete(CaptureTextReply{.status = CaptureTextStatus::kBusy});
        return;
    }
    const auto request = target.command.request_id;
    if (!send_ || !send_(target.command.target_pid, target.command, GameTextWritePermit::Observe(permit))) {
        Finish(request, CaptureTextReply{.request_id = request});
    }
}

void GameTextBackend::HandleReply(std::uint32_t authenticated_pid, const CaptureTextReply& reply) {
    auto validated = reply;
    {
        std::lock_guard lock(mutex_);
        const auto found = pending_.find(reply.request_id);
        if (found == pending_.end() || found->second.pid != authenticated_pid ||
            WaitForSingleObject(found->second.process->get(), 0) != WAIT_TIMEOUT) {
            return;
        }
        if (reply.status == CaptureTextStatus::kSubmitted && reply.accepted_bytes != found->second.text_bytes) {
            validated.status = CaptureTextStatus::kOutcomeUnknown;
        }
    }
    Finish(reply.request_id, validated);
}

void GameTextBackend::Finish(std::uint64_t request, CaptureTextReply reply) {
    std::optional<Pending> pending{};
    {
        std::lock_guard lock(mutex_);
        const auto found = pending_.find(request);
        if (found == pending_.end()) {
            return;
        }
        pending = std::move(found->second);
        pending->permit->Cancel();
        pending_.erase(found);
    }
    CancelGameTextTimer(pending->timer);
    pending->complete(std::move(reply));
}

void GameTextBackend::Stop() {
    std::unordered_map<std::uint64_t, Pending> pending{};
    {
        std::lock_guard lock(mutex_);
        stopped_ = true;
        for (const auto& entry : pending_) {
            entry.second.permit->Cancel();
        }
        pending.swap(pending_);
    }
    for (auto& entry : pending) {
        CancelGameTextTimer(entry.second.timer);
        entry.second.complete(CaptureTextReply{.request_id = entry.first, .status = CaptureTextStatus::kOutcomeUnknown});
    }
}
} // namespace px
