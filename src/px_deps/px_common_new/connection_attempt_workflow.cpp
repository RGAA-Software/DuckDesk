#include "connection_attempt_workflow.h"

#include <utility>

namespace px {

std::shared_ptr<PxConnectionAttemptWorkflow> PxConnectionAttemptWorkflow::Create(const std::shared_ptr<PxAsyncRuntime>& runtime,
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

PxConnectionAttemptWorkflow::PxConnectionAttemptWorkflow(std::shared_ptr<PxAsyncScope> scope, std::chrono::milliseconds timeout)
    : scope_(std::move(scope)), timeout_(timeout) {}

PxConnectionAttemptWorkflow::~PxConnectionAttemptWorkflow() {
    Stop();
}

PxAwaitable<void> PxConnectionAttemptWorkflow::RunAttempt(std::shared_ptr<Operation> operation, std::chrono::steady_clock::time_point deadline,
                                                          PxConnectionAttemptCompletion completion) {
    auto result = co_await Operation::WaitUntil(std::move(operation), deadline);
    completion(std::move(result));
    co_return;
}

PxConnectionAttemptWorkflow::AttemptStateResult PxConnectionAttemptWorkflow::StartAttemptState() {
    std::shared_ptr<PxAsyncScope> scope;
    std::shared_ptr<Operation> previous;
    std::shared_ptr<Operation> operation;
    PxConnectionAttemptTicket ticket{};
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !scope_ || !scope_->IsAccepting()) {
            return AttemptStateResult::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "connect.start", "connection workflow is stopping"));
        }
        scope = scope_;
        previous = std::move(active_);
        operation = Operation::Create(scope->Executor());
        active_ = operation;
        ticket.generation = ++generation_;
        ready_ = false;
    }

    if (previous) {
        static_cast<void>(
            previous->TryFail(MakePxAsyncError(PxAsyncErrorCode::kCancelled, "connect.replaced",
                                               "connection attempt was replaced by a newer generation", true, "CONNECTION_ATTEMPT_REPLACED")));
    }

    return AttemptStateResult::Success(AttemptState{.operation = std::move(operation), .ticket = ticket});
}

PxConnectionAttemptStartResult PxConnectionAttemptWorkflow::StartAttempt() {
    auto state = StartAttemptState();
    if (!state) {
        return PxConnectionAttemptStartResult::Failure(state.Error());
    }
    return PxConnectionAttemptStartResult::Success(state.Value().ticket);
}

PxAwaitable<PxConnectionAttemptResult> PxConnectionAttemptWorkflow::WaitUntilReady(std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
                                                                                   PxConnectionAttemptTicket ticket,
                                                                                   std::chrono::steady_clock::time_point deadline) {
    if (!workflow || ticket.generation == 0) {
        co_return PxConnectionAttemptResult::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "connect.wait", "connection attempt is invalid"));
    }

    std::shared_ptr<Operation> operation;
    {
        std::lock_guard lock(workflow->mutex_);
        if (workflow->stopping_) {
            co_return PxConnectionAttemptResult::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "connect.wait", "connection workflow is stopping"));
        }
        if (ticket.generation != workflow->generation_ || !workflow->active_) {
            co_return PxConnectionAttemptResult::Failure(MakePxAsyncError(PxAsyncErrorCode::kCancelled, "connect.wait",
                                                                          "connection attempt generation is no longer active", true,
                                                                          "CONNECTION_ATTEMPT_STALE_GENERATION"));
        }
        operation = workflow->active_;
    }

    auto result = co_await Operation::WaitUntil(operation, deadline);
    if (!result) {
        std::lock_guard lock(workflow->mutex_);
        if (workflow->active_ == operation) {
            workflow->active_.reset();
            workflow->ready_ = false;
        }
    }
    co_return result;
}

bool PxConnectionAttemptWorkflow::BeginAttempt(PxConnectionAttemptCompletion completion) {
    if (!completion) {
        return false;
    }

    auto state_result = StartAttemptState();
    if (!state_result) {
        return false;
    }
    auto state = state_result.TakeValue();

    std::shared_ptr<PxAsyncScope> scope;
    std::chrono::milliseconds timeout{0};
    {
        std::lock_guard lock(mutex_);
        scope = scope_;
        timeout = timeout_;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const bool spawned = scope->Spawn("connection-attempt", [operation = state.operation, deadline, completion = std::move(completion)]() mutable {
        return RunAttempt(std::move(operation), deadline, std::move(completion));
    });
    if (spawned) {
        return true;
    }

    std::lock_guard lock(mutex_);
    if (active_ == state.operation) {
        active_.reset();
    }
    return false;
}

bool PxConnectionAttemptWorkflow::MarkReady() {
    std::uint64_t generation{0};
    {
        std::lock_guard lock(mutex_);
        generation = generation_;
    }
    return MarkReady(generation);
}

bool PxConnectionAttemptWorkflow::MarkReady(std::uint64_t generation) {
    std::shared_ptr<Operation> operation;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || generation == 0 || generation != generation_ || !active_ || ready_) {
            return false;
        }
        operation = active_;
        ready_ = true;
    }
    if (operation->TryComplete(PxConnectionAttemptResult::Success(PxConnectionAttemptReady{.generation = generation}))) {
        return true;
    }
    std::lock_guard lock(mutex_);
    if (active_ == operation) {
        ready_ = false;
    }
    return false;
}

bool PxConnectionAttemptWorkflow::FailActive(PxAsyncError error) {
    std::uint64_t generation{0};
    {
        std::lock_guard lock(mutex_);
        generation = generation_;
    }
    return FailActive(generation, std::move(error));
}

bool PxConnectionAttemptWorkflow::FailActive(std::uint64_t generation, PxAsyncError error) {
    std::shared_ptr<Operation> operation;
    {
        std::lock_guard lock(mutex_);
        if (generation == 0 || generation != generation_ || !active_) {
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
        static_cast<void>(operation->TryFail(MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "connect.stop", "connection workflow stopped")));
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
