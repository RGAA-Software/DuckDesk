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

PxAwaitable<void> PxConnectionAttemptWorkflow::RunAttempt(std::shared_ptr<ReadyOperation> operation,
                                                          std::chrono::steady_clock::time_point deadline,
                                                          PxConnectionAttemptCompletion completion) {
    auto result = co_await ReadyOperation::WaitUntil(std::move(operation), deadline);
    completion(std::move(result));
    co_return;
}

PxConnectionAttemptWorkflow::AttemptStateResult PxConnectionAttemptWorkflow::StartAttemptState() {
    std::shared_ptr<PxAsyncScope> scope;
    std::shared_ptr<ReadyOperation> previous_ready;
    std::shared_ptr<DisconnectedOperation> previous_disconnected;
    std::shared_ptr<ReadyOperation> ready_operation;
    std::shared_ptr<DisconnectedOperation> disconnected_operation;
    PxConnectionAttemptTicket ticket{};
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !scope_ || !scope_->IsAccepting()) {
            return AttemptStateResult::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "connect.start", "connection workflow is stopping"));
        }
        scope = scope_;
        previous_ready = std::move(active_ready_);
        previous_disconnected = std::move(active_disconnected_);
        ready_operation = ReadyOperation::Create(scope->Executor());
        disconnected_operation = DisconnectedOperation::Create(scope->Executor());
        active_ready_ = ready_operation;
        active_disconnected_ = disconnected_operation;
        ticket.generation = ++generation_;
        ready_ = false;
    }

    const auto replaced = MakePxAsyncError(PxAsyncErrorCode::kCancelled, "connect.replaced",
                                           "connection attempt was replaced by a newer generation", true,
                                           "CONNECTION_ATTEMPT_REPLACED");
    if (previous_ready) {
        static_cast<void>(
            previous_ready->TryFail(replaced));
    }
    if (previous_disconnected) {
        static_cast<void>(previous_disconnected->TryFail(replaced));
    }

    return AttemptStateResult::Success(AttemptState{.ready_operation = std::move(ready_operation),
                                                    .disconnected_operation = std::move(disconnected_operation),
                                                    .ticket = ticket});
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

    std::shared_ptr<ReadyOperation> operation;
    {
        std::lock_guard lock(workflow->mutex_);
        if (workflow->stopping_) {
            co_return PxConnectionAttemptResult::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "connect.wait", "connection workflow is stopping"));
        }
        if (ticket.generation != workflow->generation_ || !workflow->active_ready_) {
            co_return PxConnectionAttemptResult::Failure(MakePxAsyncError(PxAsyncErrorCode::kCancelled, "connect.wait",
                                                                          "connection attempt generation is no longer active", true,
                                                                          "CONNECTION_ATTEMPT_STALE_GENERATION"));
        }
        operation = workflow->active_ready_;
    }

    auto result = co_await ReadyOperation::WaitUntil(operation, deadline);
    if (!result) {
        std::lock_guard lock(workflow->mutex_);
        if (workflow->active_ready_ == operation) {
            workflow->ready_ = false;
        }
    }
    co_return result;
}

PxAwaitable<PxConnectionDisconnectedResult> PxConnectionAttemptWorkflow::WaitUntilDisconnected(
    std::shared_ptr<PxConnectionAttemptWorkflow> workflow, PxConnectionAttemptTicket ticket) {
    if (!workflow || ticket.generation == 0) {
        co_return PxConnectionDisconnectedResult::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "disconnect.wait", "connection attempt is invalid"));
    }

    std::shared_ptr<DisconnectedOperation> operation;
    {
        std::lock_guard lock(workflow->mutex_);
        if (workflow->stopping_) {
            co_return PxConnectionDisconnectedResult::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "disconnect.wait", "connection workflow is stopping"));
        }
        if (ticket.generation != workflow->generation_ || !workflow->active_disconnected_) {
            co_return PxConnectionDisconnectedResult::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kCancelled, "disconnect.wait", "connection attempt generation is no longer active", true,
                "CONNECTION_ATTEMPT_STALE_GENERATION"));
        }
        operation = workflow->active_disconnected_;
    }

    auto result = co_await DisconnectedOperation::WaitUntil(operation, std::chrono::steady_clock::time_point::max());
    {
        std::lock_guard lock(workflow->mutex_);
        if (workflow->active_disconnected_ == operation) {
            workflow->active_disconnected_.reset();
            workflow->active_ready_.reset();
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
    const bool spawned = scope->Spawn("connection-attempt",
                                      [operation = state.ready_operation, deadline, completion = std::move(completion)]() mutable {
        return RunAttempt(std::move(operation), deadline, std::move(completion));
    });
    if (spawned) {
        return true;
    }

    std::lock_guard lock(mutex_);
    if (active_ready_ == state.ready_operation) {
        active_ready_.reset();
        active_disconnected_.reset();
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
    std::shared_ptr<ReadyOperation> operation;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || generation == 0 || generation != generation_ || !active_ready_ || ready_) {
            return false;
        }
        operation = active_ready_;
        ready_ = true;
    }
    if (operation->TryComplete(PxConnectionAttemptResult::Success(PxConnectionAttemptReady{.generation = generation}))) {
        return true;
    }
    std::lock_guard lock(mutex_);
    if (active_ready_ == operation) {
        ready_ = false;
    }
    return false;
}

bool PxConnectionAttemptWorkflow::MarkDisconnected(std::uint64_t generation, PxAsyncError reason) {
    std::shared_ptr<ReadyOperation> ready_operation;
    std::shared_ptr<DisconnectedOperation> disconnected_operation;
    bool was_ready{false};
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || generation == 0 || generation != generation_ || !active_ready_ || !active_disconnected_) {
            return false;
        }
        ready_operation = active_ready_;
        disconnected_operation = active_disconnected_;
        was_ready = ready_;
        ready_ = false;
    }

    if (!was_ready) {
        static_cast<void>(ready_operation->TryFail(reason));
    }
    return disconnected_operation->TryComplete(
        PxConnectionDisconnectedResult::Success(PxConnectionDisconnected{.generation = generation, .reason = std::move(reason)}));
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
    std::shared_ptr<ReadyOperation> operation;
    {
        std::lock_guard lock(mutex_);
        if (generation == 0 || generation != generation_ || !active_ready_) {
            return false;
        }
        operation = active_ready_;
        ready_ = false;
    }
    return operation->TryFail(std::move(error));
}

void PxConnectionAttemptWorkflow::Stop() {
    std::shared_ptr<ReadyOperation> ready_operation;
    std::shared_ptr<DisconnectedOperation> disconnected_operation;
    std::shared_ptr<PxAsyncScope> scope;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        ready_ = false;
        ready_operation = std::move(active_ready_);
        disconnected_operation = std::move(active_disconnected_);
        scope = scope_;
    }
    const auto stopped = MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "connect.stop", "connection workflow stopped");
    if (ready_operation) {
        static_cast<void>(ready_operation->TryFail(stopped));
    }
    if (disconnected_operation) {
        static_cast<void>(disconnected_operation->TryFail(stopped));
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
