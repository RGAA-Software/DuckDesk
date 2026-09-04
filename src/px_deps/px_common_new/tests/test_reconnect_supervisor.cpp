#include <atomic>
#include <chrono>
#include <future>
#include <memory>

#include <gtest/gtest.h>

#include "px_common_new/reconnect_supervisor.h"

namespace px {
namespace {

using namespace std::chrono_literals;

struct SupervisorTestState final {
    std::atomic_uint32_t starts{0};
    std::atomic_uint32_t stops{0};
    std::atomic_bool start_during_failed_reset{false};
};

std::shared_ptr<PxReconnectSupervisor> MakeSupervisor(const std::shared_ptr<PxAsyncRuntime>& runtime) {
    return PxReconnectSupervisor::Create(runtime, PxReconnectSupervisorOptions{
        .component = "test",
        .connection_timeout = 100ms,
        .adapter_stop_timeout = 20ms,
        .backoff = PxReconnectBackoffOptions{
            .initial_delay = 1ms,
            .maximum_delay = 2ms,
            .multiplier = 2.0,
            .jitter_ratio = 0.0,
            .random_seed = 1,
        },
    });
}

void StopRuntime(
    const std::shared_ptr<PxReconnectSupervisor>& supervisor,
    const std::shared_ptr<PxAsyncScope>& scope,
    const std::shared_ptr<PxAsyncRuntime>& runtime) {
    supervisor->Stop();
    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestDrain();
    runtime->Join();
}

TEST(ReconnectSupervisor, RetriesRecoverableStartupFailuresUntilReady) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto supervisor = MakeSupervisor(runtime);
    const auto state = std::make_shared<SupervisorTestState>();
    const auto ready = std::make_shared<std::promise<void>>();
    auto ready_future = ready->get_future();

    PxReconnectSupervisorHooks hooks{
        .start_attempt = [supervisor, state](std::uint64_t) {
            const auto attempt = state->starts.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (attempt < 4) {
                return PxResult<void>::Failure(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected, "test.start", "offline", true));
            }
            static_cast<void>(supervisor->MarkReady());
            return PxResult<void>::Success();
        },
        .stop_attempt = [state](std::chrono::steady_clock::time_point) -> PxAwaitable<PxResult<void>> {
            state->stops.fetch_add(1, std::memory_order_acq_rel);
            co_return PxResult<void>::Success();
        },
        .on_ready = [ready](std::uint64_t) { ready->set_value(); },
    };
    ASSERT_TRUE(scope->Spawn("retry-until-ready", [supervisor, hooks = std::move(hooks)]() mutable {
        return PxReconnectSupervisor::Run(supervisor, std::move(hooks));
    }));

    ASSERT_EQ(ready_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(state->starts.load(std::memory_order_acquire), 4U);
    EXPECT_EQ(state->stops.load(std::memory_order_acquire), 3U);
    const auto statistics = supervisor->Statistics();
    EXPECT_EQ(statistics.connection_attempts, 4U);
    EXPECT_EQ(statistics.successful_connections, 1U);
    EXPECT_EQ(statistics.reconnect_waits, 3U);
    StopRuntime(supervisor, scope, runtime);
}

TEST(ReconnectSupervisor, DisconnectAfterReadyStartsANewGeneration) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto supervisor = MakeSupervisor(runtime);
    const auto state = std::make_shared<SupervisorTestState>();
    const auto reconnected = std::make_shared<std::promise<void>>();
    auto reconnected_future = reconnected->get_future();

    PxReconnectSupervisorHooks hooks{
        .start_attempt = [supervisor, state](std::uint64_t) {
            state->starts.fetch_add(1, std::memory_order_acq_rel);
            static_cast<void>(supervisor->MarkReady());
            return PxResult<void>::Success();
        },
        .stop_attempt = [state](std::chrono::steady_clock::time_point) -> PxAwaitable<PxResult<void>> {
            state->stops.fetch_add(1, std::memory_order_acq_rel);
            co_return PxResult<void>::Success();
        },
        .on_ready = [supervisor, state, reconnected](std::uint64_t) {
            if (state->starts.load(std::memory_order_acquire) == 1) {
                static_cast<void>(supervisor->MarkDisconnected(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected, "test.disconnect", "offline", true)));
            } else {
                reconnected->set_value();
            }
        },
    };
    ASSERT_TRUE(scope->Spawn("disconnect-reconnect", [supervisor, hooks = std::move(hooks)]() mutable {
        return PxReconnectSupervisor::Run(supervisor, std::move(hooks));
    }));

    ASSERT_EQ(reconnected_future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(state->starts.load(std::memory_order_acquire), 2U);
    EXPECT_EQ(state->stops.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(supervisor->Generation(), 2U);
    StopRuntime(supervisor, scope, runtime);
}

TEST(ReconnectSupervisor, AdapterMustStopBeforeAnotherStartAttempt) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto supervisor = MakeSupervisor(runtime);
    const auto state = std::make_shared<SupervisorTestState>();
    const auto ready = std::make_shared<std::promise<void>>();
    auto ready_future = ready->get_future();

    PxReconnectSupervisorHooks hooks{
        .start_attempt = [supervisor, state](std::uint64_t) {
            const auto attempt = state->starts.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (attempt == 1) {
                return PxResult<void>::Failure(MakePxAsyncError(
                    PxAsyncErrorCode::kServiceNotConnected, "test.start", "offline", true));
            }
            if (state->stops.load(std::memory_order_acquire) < 2) {
                state->start_during_failed_reset.store(true, std::memory_order_release);
            }
            static_cast<void>(supervisor->MarkReady());
            return PxResult<void>::Success();
        },
        .stop_attempt = [state](std::chrono::steady_clock::time_point) -> PxAwaitable<PxResult<void>> {
            const auto stop = state->stops.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (stop == 1) {
                co_return PxResult<void>::Failure(MakePxAsyncError(
                    PxAsyncErrorCode::kTimeout, "test.stop", "still stopping", true));
            }
            co_return PxResult<void>::Success();
        },
        .on_ready = [ready](std::uint64_t) { ready->set_value(); },
    };
    ASSERT_TRUE(scope->Spawn("reset-before-retry", [supervisor, hooks = std::move(hooks)]() mutable {
        return PxReconnectSupervisor::Run(supervisor, std::move(hooks));
    }));

    ASSERT_EQ(ready_future.wait_for(1s), std::future_status::ready);
    EXPECT_FALSE(state->start_during_failed_reset.load(std::memory_order_acquire));
    EXPECT_EQ(state->starts.load(std::memory_order_acquire), 2U);
    EXPECT_EQ(state->stops.load(std::memory_order_acquire), 2U);
    EXPECT_EQ(supervisor->Statistics().adapter_reset_failures, 1U);
    StopRuntime(supervisor, scope, runtime);
}

TEST(ReconnectSupervisor, NonRetryableFailureStopsAfterAdapterQuiesces) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto supervisor = MakeSupervisor(runtime);
    const auto state = std::make_shared<SupervisorTestState>();
    const auto terminal = std::make_shared<std::promise<void>>();
    auto terminal_future = terminal->get_future();

    PxReconnectSupervisorHooks hooks{
        .start_attempt = [state](std::uint64_t) {
            state->starts.fetch_add(1, std::memory_order_acq_rel);
            return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kProtocolError, "test.auth", "rejected", false));
        },
        .stop_attempt = [state](std::chrono::steady_clock::time_point) -> PxAwaitable<PxResult<void>> {
            state->stops.fetch_add(1, std::memory_order_acq_rel);
            co_return PxResult<void>::Success();
        },
        .on_terminal = [terminal](std::uint64_t, const PxAsyncError&) { terminal->set_value(); },
    };
    ASSERT_TRUE(scope->Spawn("terminal-failure", [supervisor, hooks = std::move(hooks)]() mutable {
        return PxReconnectSupervisor::Run(supervisor, std::move(hooks));
    }));

    ASSERT_EQ(terminal_future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(scope->WaitFor(1s));
    EXPECT_EQ(state->starts.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(state->stops.load(std::memory_order_acquire), 1U);
    EXPECT_EQ(supervisor->Statistics().reconnect_waits, 0U);
    StopRuntime(supervisor, scope, runtime);
}

TEST(ReconnectSupervisor, ScopeStopCancelsLongBackoffPromptly) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime);
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto supervisor = PxReconnectSupervisor::Create(runtime, PxReconnectSupervisorOptions{
        .component = "test",
        .connection_timeout = 100ms,
        .adapter_stop_timeout = 20ms,
        .backoff = PxReconnectBackoffOptions{
            .initial_delay = 5s,
            .maximum_delay = 5s,
            .multiplier = 1.0,
            .jitter_ratio = 0.0,
            .random_seed = 1,
        },
    });
    const auto reset = std::make_shared<std::promise<void>>();
    auto reset_future = reset->get_future();
    PxReconnectSupervisorHooks hooks{
        .start_attempt = [](std::uint64_t) {
            return PxResult<void>::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kServiceNotConnected, "test.start", "offline", true));
        },
        .stop_attempt = [reset](std::chrono::steady_clock::time_point) -> PxAwaitable<PxResult<void>> {
            reset->set_value();
            co_return PxResult<void>::Success();
        },
    };
    ASSERT_TRUE(scope->Spawn("cancel-backoff", [supervisor, hooks = std::move(hooks)]() mutable {
        return PxReconnectSupervisor::Run(supervisor, std::move(hooks));
    }));
    ASSERT_EQ(reset_future.wait_for(1s), std::future_status::ready);

    const auto stop_started = std::chrono::steady_clock::now();
    supervisor->Stop();
    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    EXPECT_LT(std::chrono::steady_clock::now() - stop_started, 1s);
    runtime->RequestDrain();
    runtime->Join();
}

} // namespace
} // namespace px
