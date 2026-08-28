#include "ft_async_session.h"

#include <mutex>
#include <future>
#include <stdexcept>

namespace px::ft {

class FtAsyncSession::State final {
public:
    State(std::shared_ptr<FtEngine> engine_value, Sender sender_value)
        : engine(std::move(engine_value)), sender(std::move(sender_value)) {}

    std::shared_ptr<FtEngine> engine;
    Sender sender;
    std::shared_ptr<asio::steady_timer> timer;
    mutable std::mutex engine_mutex;
    std::atomic_bool stopping{false};
    std::atomic_bool has_jobs{false};
    mutable std::mutex statistics_mutex;
    FtAsyncSessionStatistics statistics;
};

std::shared_ptr<FtAsyncSession> FtAsyncSession::Create(Sender sender,
                                                      Configure configure) {
    return std::make_shared<FtAsyncSession>(std::move(sender), std::move(configure));
}

std::shared_ptr<FtAsyncSession> FtAsyncSession::CreateOnRuntime(
    const std::shared_ptr<PxAsyncRuntime>& runtime,
    const std::shared_ptr<FtEngine>& engine,
    Sender sender,
    Configure configure,
    PxAsyncLane lane) {
    if (!runtime || !engine) {
        throw std::invalid_argument(
            "FtAsyncSession shared runtime and engine must not be null");
    }
    return std::make_shared<FtAsyncSession>(
        runtime, engine, std::move(sender), std::move(configure), false, lane);
}

FtAsyncSession::FtAsyncSession(Sender sender, Configure configure)
    : FtAsyncSession(PxAsyncRuntime::Create({.worker_threads = 1}),
                     std::make_shared<FtEngine>(),
                     std::move(sender),
                     std::move(configure),
                     true,
                     PxAsyncLane::kWorker) {}

FtAsyncSession::FtAsyncSession(std::shared_ptr<PxAsyncRuntime> runtime,
                               std::shared_ptr<FtEngine> engine,
                               Sender sender,
                               Configure configure,
                               bool owns_runtime,
                               PxAsyncLane lane)
    : runtime_(std::move(runtime)),
      state_(std::make_shared<State>(std::move(engine), std::move(sender))),
      configure_(std::move(configure)),
      owns_runtime_(owns_runtime),
      lane_(lane) {
    if (!state_->sender) {
        throw std::invalid_argument("FtAsyncSession requires a sender");
    }
}

FtAsyncSession::~FtAsyncSession() {
    static_cast<void>(StopAndWait());
}

bool FtAsyncSession::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    if (configure_) {
        configure_(state_->engine);
    }
    if (owns_runtime_ && !runtime_->Start()) {
        return false;
    }
    if (runtime_->IsStopping()) {
        return false;
    }
    scope_ = PxAsyncScope::Create(runtime_, lane_);
    const auto state = state_;
    return scope_->Spawn("ft-session-pump", [state]() {
        return Run(state);
    });
}

bool FtAsyncSession::Post(std::string name, Command command) {
    if (!scope_ || !command || state_->stopping.load(std::memory_order_acquire)) {
        return false;
    }
    const auto state = state_;
    return scope_->Spawn(std::move(name), [state, command = std::move(command)]() mutable {
        return ExecuteCommand(state, std::move(command));
    });
}

bool FtAsyncSession::PostAndWait(std::string name,
                                 Command command,
                                 std::chrono::milliseconds timeout) {
    if (!scope_ || scope_->IsScopeThread() || !command) {
        return false;
    }
    const auto completed = std::make_shared<std::promise<bool>>();
    auto future = completed->get_future();
    const bool posted = Post(
        std::move(name),
        [command = std::move(command), completed](const auto& engine) mutable {
            try {
                command(engine);
                completed->set_value(true);
            } catch (...) {
                completed->set_value(false);
            }
        });
    return posted && future.wait_for(timeout) == std::future_status::ready && future.get();
}

bool FtAsyncSession::StopAndWait(std::chrono::milliseconds timeout) {
    if (!started_.exchange(false, std::memory_order_acq_rel)) {
        return true;
    }
    state_->stopping.store(true, std::memory_order_release);
    if (scope_) {
        const auto state = state_;
        asio::post(scope_->Executor(), [state]() {
            if (state->timer) {
                asio::error_code ignored;
                state->timer->cancel(ignored);
            }
        });
        const bool stopped = scope_->StopAndWait(timeout);
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

PxAwaitable<void> FtAsyncSession::ExecuteCommand(std::shared_ptr<State> state,
                                                 Command command) {
    if (!state->stopping.load(std::memory_order_acquire)) {
        {
            std::lock_guard lock(state->engine_mutex);
            command(state->engine);
        }
        if (state->timer) {
            asio::error_code ignored;
            state->timer->cancel(ignored);
        }
    }
    co_return;
}

PxAwaitable<void> FtAsyncSession::Run(std::shared_ptr<State> state) {
    const auto executor = co_await asio::this_coro::executor;
    state->timer = std::make_shared<asio::steady_timer>(executor);

    while (!state->stopping.load(std::memory_order_acquire)) {
        {
            std::lock_guard lock(state->engine_mutex);
            state->engine->Tick();
        }
        auto retry_delay = std::chrono::milliseconds(1);
        for (;;) {
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

            FileTransferSendResult result =
                FileTransferSendResult::TransportError("sender did not run");
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
            active = state->engine->HasPendingOutbound() ||
                     !state->engine->read_jobs().empty() ||
                     !state->engine->write_jobs().empty();
        }
        state->has_jobs.store(active, std::memory_order_release);
        state->timer->expires_after(active ? retry_delay : std::chrono::seconds(1));
        asio::error_code ignored;
        co_await state->timer->async_wait(
            asio::redirect_error(asio::use_awaitable, ignored));
    }

    state->has_jobs.store(false, std::memory_order_release);
    state->timer.reset();
    co_return;
}

} // namespace px::ft
