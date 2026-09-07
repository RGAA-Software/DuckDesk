#include "ft_async_session.h"

#include <mutex>
#include <future>
#include <deque>
#include <stdexcept>

#include "px_common/async_blocking_call.h"
#include "px_common/blocking_executor.h"
#include "px_common/log.h"

namespace px::ft {
namespace {

void CancelTimerNoThrow(const std::shared_ptr<asio::steady_timer>& timer) noexcept {
    if (!timer) {
        return;
    }
    try {
        static_cast<void>(timer->cancel());
    } catch (const asio::system_error&) {
    }
}

} // namespace

class FtAsyncSession::State final {
  public:
    State(std::shared_ptr<FtEngine> engine_value, Sender sender_value) : engine(std::move(engine_value)), sender(std::move(sender_value)) {}

    std::shared_ptr<FtEngine> engine;
    Sender sender;
    std::shared_ptr<PxBlockingExecutor> blocking_executor;
    std::shared_ptr<std::atomic_bool> blocking_cancellation = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<asio::steady_timer> timer;
    std::shared_ptr<asio::steady_timer> writable_timer;
    mutable std::mutex engine_mutex;
    struct QueuedCommand {
        std::string name{};
        Command command{};
    };
    std::mutex commands_mutex{};
    std::deque<QueuedCommand> commands{};
    std::atomic_bool stopping{false};
    std::atomic_bool has_jobs{false};
    mutable std::mutex statistics_mutex;
    FtAsyncSessionStatistics statistics;
};

std::shared_ptr<FtAsyncSession> FtAsyncSession::Create(Sender sender, Configure configure) {
    return std::make_shared<FtAsyncSession>(std::move(sender), std::move(configure));
}

std::shared_ptr<FtAsyncSession> FtAsyncSession::CreateOnRuntime(const std::shared_ptr<PxAsyncRuntime>& runtime,
                                                                const std::shared_ptr<FtEngine>& engine, Sender sender, Configure configure,
                                                                PxAsyncLane lane) {
    if (!runtime || !engine) {
        throw std::invalid_argument("FtAsyncSession shared runtime and engine must not be null");
    }
    return std::make_shared<FtAsyncSession>(runtime, engine, std::move(sender), std::move(configure), false, lane);
}

FtAsyncSession::FtAsyncSession(Sender sender, Configure configure)
    : FtAsyncSession(PxAsyncRuntime::Create({.worker_threads = 1}), std::make_shared<FtEngine>(), std::move(sender), std::move(configure), true,
                     PxAsyncLane::kWorker) {}

FtAsyncSession::FtAsyncSession(std::shared_ptr<PxAsyncRuntime> runtime, std::shared_ptr<FtEngine> engine, Sender sender, Configure configure,
                               bool owns_runtime, PxAsyncLane lane)
    : runtime_(std::move(runtime)), state_(std::make_shared<State>(std::move(engine), std::move(sender))), configure_(std::move(configure)),
      owns_runtime_(owns_runtime), lane_(lane) {
    if (!state_->sender) {
        throw std::invalid_argument("FtAsyncSession requires a sender");
    }
}

FtAsyncSession::~FtAsyncSession() {
    static_cast<void>(StopAndWait());
}

bool FtAsyncSession::Start() {
    // A stopped session is terminal; reconnect creates a fresh owner/runtime.
    if (state_->stopping.load(std::memory_order_acquire))
        return false;
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    if (configure_) {
        configure_(state_->engine);
    }
    if (owns_runtime_ && !runtime_->Start()) {
        started_.store(false, std::memory_order_release);
        return false;
    }
    if (runtime_->IsStopping()) {
        started_.store(false, std::memory_order_release);
        return false;
    }
    state_->blocking_executor = runtime_->BlockingExecutor();
    if (!state_->blocking_executor) {
        if (owns_runtime_) {
            runtime_->RequestStop();
            runtime_->Join();
        }
        started_.store(false, std::memory_order_release);
        return false;
    }
    state_->blocking_cancellation->store(false, std::memory_order_release);
    scope_ = PxAsyncScope::Create(runtime_, lane_);
    const auto state = state_;
    return scope_->Spawn("ft-session-pump", [state]() { return Run(state); });
}

bool FtAsyncSession::Post(std::string name, Command command) {
    if (!scope_ || !command || !started_.load(std::memory_order_acquire)) {
        return false;
    }
    const auto state = state_;
    {
        std::lock_guard lock(state->commands_mutex);
        if (state->stopping.load(std::memory_order_acquire))
            return false;
        state->commands.push_back({std::move(name), std::move(command)});
    }
    // Only Run consumes commands. A mutex around independent pool tasks does
    // not preserve network arrival order (including block/EOF ordering).
    const auto weak_state = std::weak_ptr<State>(state);
    asio::post(scope_->Executor(), [weak_state]() {
        if (const auto active = weak_state.lock()) {
            CancelTimerNoThrow(active->timer);
            CancelTimerNoThrow(active->writable_timer);
        }
    });
    return true;
}

bool FtAsyncSession::PostAndWait(std::string name, Command command, std::chrono::milliseconds timeout) {
    if (!scope_ || scope_->IsScopeThread() || !command || (state_->blocking_executor && state_->blocking_executor->IsWorkerThread())) {
        return false;
    }
    const auto completed = std::make_shared<std::promise<bool>>();
    auto future = completed->get_future();
    const bool posted = Post(std::move(name), [command = std::move(command), completed](const auto& engine) mutable {
        try {
            command(engine);
            completed->set_value(true);
        } catch (...) {
            completed->set_value(false);
        }
    });
    try {
        return posted && future.wait_for(timeout) == std::future_status::ready && future.get();
    } catch (const std::future_error&) {
        return false; // Shutdown discarded a queued command.
    }
}

bool FtAsyncSession::StopAndWait(std::chrono::milliseconds timeout) {
    if (!started_.exchange(false, std::memory_order_acq_rel)) {
        return true;
    }
    state_->stopping.store(true, std::memory_order_release);
    state_->blocking_cancellation->store(true, std::memory_order_release);
    {
        std::deque<State::QueuedCommand> discarded{};
        {
            std::lock_guard lock(state_->commands_mutex);
            discarded.swap(state_->commands);
        }
        // Destroy captures outside the queue lock (owners may stop recursively).
    }
    if (scope_) {
        const auto state = state_;
        asio::post(scope_->Executor(), [state]() {
            CancelTimerNoThrow(state->timer);
            CancelTimerNoThrow(state->writable_timer);
        });
        const bool inside_callback = scope_->IsScopeThread() || (state_->blocking_executor && state_->blocking_executor->IsWorkerThread());
        scope_->BeginStop();
        const bool stopped = !inside_callback && scope_->WaitFor(timeout);
        if (owns_runtime_) {
            runtime_->RequestStop();
            runtime_->Join();
        }
        return stopped;
    }
    if (owns_runtime_) {
        runtime_->RequestStop();
        runtime_->Join();
    }
    return true;
}

bool FtAsyncSession::HasJobs() const {
    return state_->has_jobs.load(std::memory_order_acquire);
}

FtAsyncSessionStatistics FtAsyncSession::GetStatistics() const {
    std::lock_guard lock(state_->statistics_mutex);
    return state_->statistics;
}

PxAwaitable<FtAsyncSession::WritableWaitResult> FtAsyncSession::WaitForWritable(std::shared_ptr<State> state,
                                                                                std::shared_ptr<FileTransferWritableSignal> signal) {
    const auto executor = co_await asio::this_coro::executor;
    const auto timer = std::make_shared<asio::steady_timer>(executor);
    state->writable_timer = timer;
    // Completion callbacks are authoritative. This deadline is only a safety
    // net for transports/OS versions that occasionally omit a low-water
    // callback; one preflight per second cannot recreate the old 2 ms spin.
    timer->expires_after(std::chrono::seconds(1));
    const auto weak_timer = std::weak_ptr<asio::steady_timer>(timer);
    signal->Subscribe([weak_timer](FileTransferWritableOutcome) {
        if (const auto active_timer = weak_timer.lock()) {
            asio::post(active_timer->get_executor(), [weak_timer]() {
                if (const auto posted_timer = weak_timer.lock()) {
                    CancelTimerNoThrow(posted_timer);
                }
            });
        }
    });
    asio::error_code ignored{};
    co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, ignored));
    if (state->writable_timer == timer) {
        state->writable_timer.reset();
    }
    const auto outcome = signal->outcome();
    if (outcome == FileTransferWritableOutcome::kWritable) {
        co_return WritableWaitResult::kWritable;
    }
    if (outcome == FileTransferWritableOutcome::kClosed) {
        co_return WritableWaitResult::kClosed;
    }
    co_return ignored == asio::error::operation_aborted ? WritableWaitResult::kInterrupted : WritableWaitResult::kTimedOut;
}

PxAwaitable<void> FtAsyncSession::Run(std::shared_ptr<State> state) {
    const auto executor = co_await asio::this_coro::executor;
    state->timer = std::make_shared<asio::steady_timer>(executor);

    while (!state->stopping.load(std::memory_order_acquire)) {
        const auto blocking_executor = state->blocking_executor;
        const auto poster = [blocking_executor](std::function<void()> task) {
            if (!blocking_executor || blocking_executor->TryPost(std::move(task)) != PxBlockingSubmitResult::kAccepted) {
                throw std::runtime_error("file-transfer blocking executor rejected tick");
            }
        };
        auto tick_result = co_await AwaitBlockingCall<bool>(poster, executor, std::chrono::steady_clock::now() + std::chrono::seconds(30),
                                                            state->blocking_cancellation, "file_transfer.tick",
                                                            [state](const std::shared_ptr<std::atomic_bool>& cancellation) {
                                                                if (cancellation->load(std::memory_order_acquire))
                                                                    return false;
                                                                std::lock_guard lock(state->engine_mutex);
                                                                for (std::size_t index = 0; index < 64; ++index) {
                                                                    State::QueuedCommand next{};
                                                                    {
                                                                        std::lock_guard queue_lock(state->commands_mutex);
                                                                        if (state->commands.empty())
                                                                            break;
                                                                        next = std::move(state->commands.front());
                                                                        state->commands.pop_front();
                                                                    }
                                                                    if (cancellation->load(std::memory_order_acquire))
                                                                        return false;
                                                                    try {
                                                                        next.command(state->engine);
                                                                    } catch (const std::exception& error) {
                                                                        LOGE("file-transfer command {} failed: {}", next.name, error.what());
                                                                    } catch (...) {
                                                                        LOGE("file-transfer command {} failed", next.name);
                                                                    }
                                                                }
                                                                if (cancellation->load(std::memory_order_acquire))
                                                                    return false;
                                                                state->engine->Tick();
                                                                return true;
                                                            });
        if (!tick_result) {
            if (!state->stopping.load(std::memory_order_acquire)) {
                LOGE("event=file_transfer.tick code={} operation=advance outcome=failed retryable={} detail={}", tick_result.Error().StableCode(),
                     tick_result.Error().retryable, tick_result.Error().message);
                state->stopping.store(true, std::memory_order_release);
            }
            break;
        }
        auto retry_delay = std::chrono::milliseconds(1);
        std::shared_ptr<FileTransferWritableSignal> writable_signal;
        while (!state->stopping.load(std::memory_order_acquire)) {
            std::optional<PreparedOutboundMessage> prepared;
            {
                std::lock_guard lock(state->engine_mutex);
                prepared = state->engine->PrepareOutbound();
            }
            if (!prepared) {
                break;
            }
            if (!prepared->message) {
                std::lock_guard lock(state->engine_mutex);
                static_cast<void>(state->engine->CommitOutbound(prepared->token));
                continue;
            }

            FileTransferSendResult result = FileTransferSendResult::TransportError("sender did not run");
            try {
                result = state->sender(prepared->message);
            } catch (...) {
                result = FileTransferSendResult::TransportError("sender threw an exception");
            }

            {
                std::lock_guard lock(state->statistics_mutex);
                switch (result.status()) {
                case FileTransferSendStatus::kAccepted:
                    ++state->statistics.accepted_messages;
                    break;
                case FileTransferSendStatus::kBusy:
                    ++state->statistics.busy_retries;
                    retry_delay = std::chrono::milliseconds(2);
                    writable_signal = result.writable_signal();
                    break;
                case FileTransferSendStatus::kDisconnected:
                    ++state->statistics.disconnected_retries;
                    retry_delay = std::chrono::milliseconds(50);
                    break;
                case FileTransferSendStatus::kTransportError:
                    ++state->statistics.transport_errors;
                    retry_delay = std::chrono::milliseconds(20);
                    break;
                }
            }

            if (!result.accepted()) {
                std::lock_guard lock(state->engine_mutex);
                static_cast<void>(state->engine->RetryOutbound(prepared->token));
                break;
            }
            std::lock_guard lock(state->engine_mutex);
            if (!state->engine->CommitOutbound(prepared->token)) {
                state->stopping.store(true, std::memory_order_release);
                break;
            }
        }

        bool active = false;
        {
            std::lock_guard lock(state->engine_mutex);
            active = state->engine->HasPendingOutbound() || !state->engine->read_jobs().empty() || !state->engine->write_jobs().empty();
        }
        state->has_jobs.store(active, std::memory_order_release);
        if (state->stopping.load(std::memory_order_acquire))
            break;
        {
            std::lock_guard lock(state->commands_mutex);
            if (!state->commands.empty())
                continue;
        }
        if (active && writable_signal) {
            {
                std::lock_guard lock(state->statistics_mutex);
                ++state->statistics.writable_waits;
            }
            const auto outcome = co_await WaitForWritable(state, writable_signal);
            std::lock_guard lock(state->statistics_mutex);
            if (outcome == WritableWaitResult::kWritable) {
                ++state->statistics.writable_wakeups;
            } else if (outcome == WritableWaitResult::kClosed) {
                ++state->statistics.writable_closures;
            } else if (outcome == WritableWaitResult::kTimedOut) {
                ++state->statistics.writable_timeouts;
            } else {
                ++state->statistics.writable_interruptions;
            }
            continue;
        }
        state->timer->expires_after(active ? retry_delay : std::chrono::seconds(1));
        asio::error_code ignored{};
        co_await state->timer->async_wait(asio::redirect_error(asio::use_awaitable, ignored));
    }

    state->has_jobs.store(false, std::memory_order_release);
    state->timer.reset();
    state->writable_timer.reset();
    co_return;
}

} // namespace px::ft
