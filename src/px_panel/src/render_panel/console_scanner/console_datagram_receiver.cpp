#include "console_datagram_receiver.h"

#include <array>
#include <exception>
#include <mutex>
#include <utility>

#include <asio2/external/asio.hpp>

#include "px_common_new/log.h"

namespace px {
namespace {

using Udp = asio::ip::udp;

constexpr std::size_t kMaxConsoleDatagramBytes = 4096;

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

class ConsoleDatagramReceiver::State final {
  public:
    explicit State(asio::any_io_executor executor) : executor_(std::move(executor)) {}

    void SetSocket(const std::shared_ptr<Udp::socket>& socket) {
        std::lock_guard lock(mutex_);
        socket_ = socket;
    }

    void ClearSocket(const std::shared_ptr<Udp::socket>& socket) {
        std::lock_guard lock(mutex_);
        if (socket_ == socket) {
            socket_.reset();
            bound_port_ = 0;
        }
    }

    void SetTimer(const std::shared_ptr<asio::steady_timer>& timer) {
        std::lock_guard lock(mutex_);
        timer_ = timer;
    }

    void ClearTimer(const std::shared_ptr<asio::steady_timer>& timer) {
        std::lock_guard lock(mutex_);
        if (timer_ == timer) {
            timer_.reset();
        }
    }

    void SetBoundPort(std::uint16_t port) noexcept {
        bound_port_.store(port, std::memory_order_release);
    }

    [[nodiscard]] std::uint16_t BoundPort() const noexcept {
        return bound_port_.load(std::memory_order_acquire);
    }

    void CancelPending() {
        std::shared_ptr<Udp::socket> socket;
        std::shared_ptr<asio::steady_timer> timer;
        {
            std::lock_guard lock(mutex_);
            socket = socket_;
            timer = timer_;
        }
        asio::post(executor_, [socket, timer]() {
            asio::error_code ignored;
            if (socket) {
                socket->cancel(ignored);
                socket->close(ignored);
            }
            CancelTimerNoThrow(timer);
        });
    }

    asio::any_io_executor executor_;
    std::atomic_bool stopping_ = false;

  private:
    mutable std::mutex mutex_;
    std::shared_ptr<Udp::socket> socket_;
    std::shared_ptr<asio::steady_timer> timer_;
    std::atomic_uint16_t bound_port_ = 0;
};

std::shared_ptr<ConsoleDatagramReceiver> ConsoleDatagramReceiver::Create(const std::shared_ptr<PxAsyncRuntime>& runtime,
                                                                         std::chrono::milliseconds retry_delay) {
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    if (!scope || retry_delay <= std::chrono::milliseconds::zero()) {
        return {};
    }
    return std::make_shared<ConsoleDatagramReceiver>(scope, retry_delay);
}

ConsoleDatagramReceiver::ConsoleDatagramReceiver(std::shared_ptr<PxAsyncScope> scope, std::chrono::milliseconds retry_delay)
    : scope_(std::move(scope)), state_(std::make_shared<State>(scope_->Executor())), retry_delay_(retry_delay) {}

ConsoleDatagramReceiver::~ConsoleDatagramReceiver() {
    Stop();
}

bool ConsoleDatagramReceiver::Start(std::uint16_t port, DatagramHandler handler) {
    bool expected = false;
    if (!handler || !scope_ || !state_ || !started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }

    const auto state = state_;
    if (!scope_->Spawn("console-datagram-receiver", [state, port, retry_delay = retry_delay_, handler = std::move(handler)]() mutable {
            return Run(state, port, retry_delay, std::move(handler));
        })) {
        started_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void ConsoleDatagramReceiver::Stop() {
    const auto state = state_;
    const auto scope = scope_;
    if (!state || !scope) {
        return;
    }
    state->stopping_.store(true, std::memory_order_release);
    state->CancelPending();
    scope->BeginStop();
    if (!scope->IsScopeThread()) {
        if (!scope->WaitFor(std::chrono::seconds(3))) {
            LOGE("Console discovery receiver did not stop within 3 seconds");
        }
    }
}

std::uint16_t ConsoleDatagramReceiver::BoundPort() const noexcept {
    return state_ ? state_->BoundPort() : 0;
}

PxAsyncScopeStatistics ConsoleDatagramReceiver::Statistics() const {
    return scope_ ? scope_->GetStatistics() : PxAsyncScopeStatistics{};
}

PxAwaitable<void> ConsoleDatagramReceiver::Run(std::shared_ptr<State> state, std::uint16_t port, std::chrono::milliseconds retry_delay,
                                               DatagramHandler handler) {
    const auto executor = co_await asio::this_coro::executor;
    while (!state->stopping_.load(std::memory_order_acquire)) {
        const auto socket = std::make_shared<Udp::socket>(executor);
        state->SetSocket(socket);
        if (state->stopping_.load(std::memory_order_acquire)) {
            state->ClearSocket(socket);
            break;
        }

        asio::error_code open_error;
        socket->open(Udp::v4(), open_error);
        if (!open_error) {
            socket->bind(Udp::endpoint(Udp::v4(), port), open_error);
        }

        if (!open_error) {
            const auto local_port = socket->local_endpoint(open_error).port();
            if (!open_error) {
                state->SetBoundPort(local_port);
                LOGI("Listening for Console discovery on UDP port: {}", local_port);
            }
        }

        if (!open_error) {
            const auto buffer = std::make_shared<std::array<char, kMaxConsoleDatagramBytes>>();
            const auto sender = std::make_shared<Udp::endpoint>();
            while (!state->stopping_.load(std::memory_order_acquire)) {
                asio::error_code receive_error;
                const auto length =
                    co_await socket->async_receive_from(asio::buffer(*buffer), *sender, asio::redirect_error(asio::use_awaitable, receive_error));
                if (state->stopping_.load(std::memory_order_acquire)) {
                    break;
                }
                if (receive_error) {
                    LOGW("Console discovery receive failed: {}", receive_error.message());
                    break;
                }
                if (length == 0) {
                    continue;
                }
                try {
                    handler(std::string(buffer->data(), length));
                } catch (const std::exception& error) {
                    LOGE("Console discovery handler failed: {}", error.what());
                } catch (...) {
                    LOGE("Console discovery handler failed with a non-standard exception");
                }
            }
        } else if (!state->stopping_.load(std::memory_order_acquire)) {
            LOGW("Console discovery UDP bind failed: {}", open_error.message());
        }

        asio::error_code ignored;
        socket->close(ignored);
        state->ClearSocket(socket);
        if (state->stopping_.load(std::memory_order_acquire)) {
            break;
        }

        const auto timer = std::make_shared<asio::steady_timer>(executor);
        timer->expires_after(retry_delay);
        state->SetTimer(timer);
        if (state->stopping_.load(std::memory_order_acquire)) {
            CancelTimerNoThrow(timer);
            state->ClearTimer(timer);
            break;
        }
        asio::error_code wait_error;
        co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
        state->ClearTimer(timer);
        if (wait_error && wait_error != asio::error::operation_aborted) {
            LOGW("Console discovery retry wait failed: {}", wait_error.message());
        }
    }
    co_return;
}

} // namespace px
