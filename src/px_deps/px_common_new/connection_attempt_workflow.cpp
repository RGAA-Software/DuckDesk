#include "connection_attempt_workflow.h"

#include <utility>

namespace px {

std::shared_ptr<PxConnectionAttemptWorkflow> PxConnectionAttemptWorkflow::Create(
    const std::shared_ptr<PxAsyncRuntime>& runtime,
    std::chrono::milliseconds timeout) {
    if (!runtime || timeout <= std::chrono::milliseconds::zero()) {
        return {};
    }
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    if (!scope) {
        return {};
    }
    return std::make_shared<PxConnectionAttemptWorkflow>(scope, timeout);
}

PxConnectionAttemptWorkflow::PxConnectionAttemptWorkflow(
    std::shared_ptr<PxAsyncScope> scope,
    std::chrono::milliseconds timeout)
    : scope_(std::move(scope)), timeout_(timeout) {}

PxConnectionAttemptWorkflow::~PxConnectionAttemptWorkflow() {
    Stop();
}

PxAwaitable<void> PxConnectionAttemptWorkflow::RunAttempt(
    std::shared_ptr<Operation> operation,
    std::chrono::steady_clock::time_point deadline,
    PxConnectionAttemptCompletion completion) {
    auto result = co_await Operation::WaitUntil(std::move(operation), deadline);
    completion(std::move(result));
    co_return;
}

bool PxConnectionAttemptWorkflow::BeginAttempt(
    PxConnectionAttemptCompletion completion) {
    if (!completion) {
        return false;
    }

    std::shared_ptr<PxAsyncScope> scope;
    std::shared_ptr<Operation> previous;
    std::shared_ptr<Operation> operation;
    std::uint64_t generation = 0;
    std::chrono::milliseconds timeout;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !scope_ || !scope_->IsAccepting()) {
            return false;
        }
        scope = scope_;
        previous = std::move(active_);
        operation = Operation::Create(scope->Executor());
        active_ = operation;
        generation = ++generation_;
        ready_ = false;
        timeout = timeout_;
    }

    if (previous) {
        static_cast<void>(previous->TryFail(MakePxAsyncError(
            PxAsyncErrorCode::kCancelled,
            "connect.replaced",
            "connection attempt was replaced by a newer generation",
            true,
            "CONNECTION_ATTEMPT_REPLACED")));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const bool spawned = scope->Spawn(
        "connection-attempt",
        [operation, deadline, completion = std::move(completion)]() mutable {
            return RunAttempt(operation, deadline, std::move(completion));
        });
    if (spawned) {
        return true;
    }

    std::lock_guard lock(mutex_);
    if (active_ == operation) {
        active_.reset();
    }
    return false;
}

bool PxConnectionAttemptWorkflow::MarkReady() {
    std::shared_ptr<Operation> operation;
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !active_ || ready_) {
            return false;
        }
        operation = active_;
        generation = generation_;
        ready_ = true;
    }
    if (operation->TryComplete(PxConnectionAttemptResult::Success(
            PxConnectionAttemptReady{.generation = generation}))) {
        return true;
    }
    std::lock_guard lock(mutex_);
    if (active_ == operation) {
        ready_ = false;
    }
    return false;
}

bool PxConnectionAttemptWorkflow::FailActive(PxAsyncError error) {
    std::shared_ptr<Operation> operation;
    {
        std::lock_guard lock(mutex_);
        if (!active_) {
            return false;
        }
        operation = active_;
        ready_ = false;
    }
    return operation->TryFail(std::move(error));
}

void PxConnectionAttemptWorkflow::Stop() {
    std::shared_ptr<Operation> operation;
    std::shared_ptr<PxAsyncScope> scope;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        ready_ = false;
        operation = std::move(active_);
        scope = scope_;
    }
    if (operation) {
        static_cast<void>(operation->TryFail(MakePxAsyncError(
            PxAsyncErrorCode::kServiceStopped,
            "connect.stop",
            "connection workflow stopped")));
    }
    if (scope) {
        static_cast<void>(scope->StopAndWait(std::chrono::seconds(2)));
    }
}

bool PxConnectionAttemptWorkflow::IsReady() const {
    std::lock_guard lock(mutex_);
    return ready_ && !stopping_;
}

std::uint64_t PxConnectionAttemptWorkflow::Generation() const {
    std::lock_guard lock(mutex_);
    return generation_;
}

PxAsyncScopeStatistics PxConnectionAttemptWorkflow::Statistics() const {
    std::shared_ptr<PxAsyncScope> scope;
    {
        std::lock_guard lock(mutex_);
        scope = scope_;
    }
    return scope ? scope->GetStatistics() : PxAsyncScopeStatistics{};
}

} // namespace px
