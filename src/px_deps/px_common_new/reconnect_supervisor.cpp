#include "reconnect_supervisor.h"

#include <utility>

#include "connection_attempt_workflow.h"
#include "log.h"

namespace px {

std::shared_ptr<PxReconnectSupervisor> PxReconnectSupervisor::Create(
    const std::shared_ptr<PxAsyncRuntime>& runtime,
    PxReconnectSupervisorOptions options) {
    if (!runtime || runtime->IsStopping() || options.component.empty() ||
        options.connection_timeout <= std::chrono::milliseconds::zero() ||
        options.adapter_stop_timeout <= std::chrono::milliseconds::zero()) {
        return {};
    }
    auto workflow = PxConnectionAttemptWorkflow::Create(runtime, options.connection_timeout);
    auto backoff = PxReconnectBackoff::Create(options.backoff);
    if (!workflow || !backoff) {
        return {};
    }
    return std::make_shared<PxReconnectSupervisor>(
        std::move(options), std::move(workflow), std::move(backoff));
}

PxReconnectSupervisor::PxReconnectSupervisor(
    PxReconnectSupervisorOptions options,
    std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
    std::shared_ptr<PxReconnectBackoff> backoff)
    : options_(std::move(options)), workflow_(std::move(workflow)), backoff_(std::move(backoff)) {}

PxReconnectSupervisor::~PxReconnectSupervisor() {
    Stop();
}

bool PxReconnectSupervisor::IsStopResult(const PxAsyncError& error) {
    return error.code == PxAsyncErrorCode::kServiceStopped || error.code == PxAsyncErrorCode::kCancelled;
}

PxReconnectBackoffStep PxReconnectSupervisor::NextBackoff() {
    reconnect_waits_.fetch_add(1, std::memory_order_acq_rel);
    consecutive_failures_.fetch_add(1, std::memory_order_acq_rel);
    return backoff_->Next();
}

PxResult<void> PxReconnectSupervisor::StartAttemptIfRunning(
    const PxReconnectStartAttempt& start_attempt,
    const std::uint64_t generation) {
    std::lock_guard lock(lifecycle_mutex_);
    if (stopping_.load(std::memory_order_acquire)) {
        return PxResult<void>::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kServiceStopped,
            "connect.start",
            "reconnect supervisor stopped before the adapter attempt could start"));
    }
    return start_attempt(generation);
}

PxAwaitable<bool> PxReconnectSupervisor::ResetAdapterUntilStopped(
    const std::shared_ptr<PxReconnectSupervisor>& supervisor,
    const PxReconnectStopAttempt& stop_attempt) {
    for (;;) {
        if (supervisor->IsStopping()) {
            co_return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + supervisor->options_.adapter_stop_timeout;
        auto stopped = co_await stop_attempt(deadline);
        if (stopped) {
            co_return true;
        }
        if (supervisor->IsStopping() || IsStopResult(stopped.Error())) {
            co_return false;
        }

        supervisor->adapter_reset_failures_.fetch_add(1, std::memory_order_acq_rel);
        const auto step = supervisor->NextBackoff();
        LOGE("event=transport.adapter_reset component={} generation={} code={} operation=stop_adapter "
             "outcome=failed recoverable=true attempt={} delay_ms={} reason={}",
             supervisor->options_.component, supervisor->Generation(), stopped.Error().StableCode(),
             step.attempt, step.delay.count(), stopped.Error().message);
        const auto waited = co_await PxReconnectBackoff::Wait(step.delay);
        if (!waited) {
            co_return false;
        }
    }
}

PxAwaitable<void> PxReconnectSupervisor::Run(
    std::shared_ptr<PxReconnectSupervisor> supervisor,
    PxReconnectSupervisorHooks hooks) {
    if (!supervisor || !hooks.start_attempt || !hooks.stop_attempt) {
        co_return;
    }

    for (;;) {
        if (supervisor->IsStopping()) {
            co_return;
        }
        const auto attempt = supervisor->workflow_->StartAttempt();
        if (!attempt) {
            co_return;
        }
        const auto ticket = attempt.Value();
        supervisor->connection_attempts_.fetch_add(1, std::memory_order_acq_rel);
        const auto attempt_started_at = std::chrono::steady_clock::now();
        LOGI("event=transport.connection_attempt component={} generation={} outcome=started",
             supervisor->options_.component, ticket.generation);

        const auto started = supervisor->StartAttemptIfRunning(hooks.start_attempt, ticket.generation);
        if (!started) {
            static_cast<void>(supervisor->workflow_->FailActive(ticket.generation, started.Error()));
        }

        const auto ready = co_await PxConnectionAttemptWorkflow::WaitUntilReady(
            supervisor->workflow_, ticket, attempt_started_at + supervisor->options_.connection_timeout);
        PxAsyncError failure{};
        bool was_ready{false};
        if (ready) {
            was_ready = true;
            supervisor->backoff_->Reset();
            supervisor->consecutive_failures_.store(0, std::memory_order_release);
            supervisor->successful_connections_.fetch_add(1, std::memory_order_acq_rel);
            const auto ready_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - attempt_started_at);
            LOGI("event=transport.connection_ready component={} generation={} latency_ms={}",
                 supervisor->options_.component, ticket.generation, ready_latency.count());
            if (hooks.on_ready) {
                hooks.on_ready(ticket.generation);
            }
            const auto disconnected = co_await PxConnectionAttemptWorkflow::WaitUntilDisconnected(
                supervisor->workflow_, ticket);
            if (disconnected) {
                failure = disconnected.Value().reason;
            } else {
                failure = disconnected.Error();
            }
        } else {
            failure = ready.Error();
        }

        if (supervisor->IsStopping() || IsStopResult(failure)) {
            co_return;
        }
        LOGW("event=transport.connection_lost component={} generation={} stage={} code={} operation=supervise "
             "outcome=failed recoverable={}",
             supervisor->options_.component, ticket.generation, failure.stage, failure.StableCode(), failure.retryable);
        if (hooks.on_lost) {
            hooks.on_lost(ticket.generation, failure, was_ready);
        }

        if (!co_await ResetAdapterUntilStopped(supervisor, hooks.stop_attempt)) {
            co_return;
        }
        if (!failure.retryable) {
            LOGE("event=transport.connection_terminal component={} generation={} stage={} code={} operation=supervise "
                 "outcome=stopped recoverable=false reason={}",
                 supervisor->options_.component, ticket.generation, failure.stage, failure.StableCode(), failure.message);
            if (hooks.on_terminal) {
                hooks.on_terminal(ticket.generation, failure);
            }
            co_return;
        }

        const auto step = supervisor->NextBackoff();
        LOGI("event=transport.reconnect_wait component={} generation={} attempt={} delay_ms={}",
             supervisor->options_.component, ticket.generation, step.attempt, step.delay.count());
        const auto waited = co_await PxReconnectBackoff::Wait(step.delay);
        if (!waited) {
            co_return;
        }
    }
}

bool PxReconnectSupervisor::MarkReady() {
    return workflow_ && workflow_->MarkReady();
}

bool PxReconnectSupervisor::MarkDisconnected(PxAsyncError reason) {
    return workflow_ && workflow_->MarkDisconnected(workflow_->Generation(), std::move(reason));
}

bool PxReconnectSupervisor::FailActive(PxAsyncError error) {
    return workflow_ && workflow_->FailActive(std::move(error));
}

void PxReconnectSupervisor::Stop() {
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopping_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
    }
    if (workflow_) {
        workflow_->Stop();
    }
}

bool PxReconnectSupervisor::IsReady() const {
    return !IsStopping() && workflow_ && workflow_->IsReady();
}

bool PxReconnectSupervisor::IsStopping() const {
    return stopping_.load(std::memory_order_acquire);
}

std::uint64_t PxReconnectSupervisor::Generation() const {
    return workflow_ ? workflow_->Generation() : 0;
}

PxReconnectSupervisorStatistics PxReconnectSupervisor::Statistics() const {
    return PxReconnectSupervisorStatistics{
        .connection_attempts = connection_attempts_.load(std::memory_order_acquire),
        .successful_connections = successful_connections_.load(std::memory_order_acquire),
        .reconnect_waits = reconnect_waits_.load(std::memory_order_acquire),
        .adapter_reset_failures = adapter_reset_failures_.load(std::memory_order_acquire),
        .consecutive_failures = consecutive_failures_.load(std::memory_order_acquire),
        .generation = Generation(),
    };
}

} // namespace px
