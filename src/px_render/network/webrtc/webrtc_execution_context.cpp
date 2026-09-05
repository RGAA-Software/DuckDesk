#include "webrtc_execution_context.h"

#include <utility>

#include <asio2/external/asio.hpp>

#include "px_common/log.h"

namespace px {

std::shared_ptr<WebRtcExecutionContext> WebRtcExecutionContext::Create(const std::shared_ptr<PxAsyncRuntime>& runtime, std::string component_id) {
    if (!runtime || runtime->IsStopping()) {
        return {};
    }
    return std::make_shared<WebRtcExecutionContext>(runtime, std::move(component_id));
}

WebRtcExecutionContext::WebRtcExecutionContext(std::shared_ptr<PxAsyncRuntime> runtime, std::string component_id)
    : runtime_(std::move(runtime)), scope_(PxAsyncScope::Create(runtime_, PxAsyncLane::kWorker)), component_id_(std::move(component_id)) {}

WebRtcExecutionContext::~WebRtcExecutionContext() {
    BeginStop();
    if (scope_ && !scope_->IsScopeThread()) {
        static_cast<void>(scope_->WaitFor(std::chrono::seconds(5)));
    }
}

void WebRtcExecutionContext::SetEventCallback(WebRtcEventCallback callback) {
    std::lock_guard lock(callback_mutex_);
    if (!accepting_.load(std::memory_order_acquire)) {
        callback_ = {};
        return;
    }
    callback_ = std::move(callback);
}

void WebRtcExecutionContext::Publish(WebRtcEvent event, const bool immediate) {
    if (!IsAccepting()) {
        return;
    }
    if (immediate) {
        Deliver(event);
        return;
    }
    const auto weak_context = weak_from_this();
    static_cast<void>(PostWork([weak_context, event = std::move(event)]() {
        if (const auto context = weak_context.lock()) {
            context->Deliver(event);
        }
    }));
}

bool WebRtcExecutionContext::PostWork(std::function<void()> task) {
    if (!task || !IsAccepting() || !scope_) {
        return false;
    }
    const auto weak_context = weak_from_this();
    return scope_->Spawn("webrtc.work", [weak_context, task = std::move(task)]() mutable { return RunWork(weak_context, std::move(task)); });
}

bool WebRtcExecutionContext::StartRepeatingTask(const std::chrono::milliseconds interval, std::function<void()> task) {
    if (interval <= std::chrono::milliseconds::zero() || !task || !IsAccepting() || !scope_) {
        return false;
    }
    const auto weak_context = weak_from_this();
    return scope_->Spawn("webrtc.periodic", [weak_context, interval, task = std::move(task)]() mutable {
        return RunRepeatingTask(weak_context, interval, std::move(task));
    });
}

void WebRtcExecutionContext::BeginStop() {
    if (!accepting_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    {
        std::lock_guard lock(callback_mutex_);
        callback_ = {};
    }
    if (scope_) {
        scope_->BeginStop();
    }
}

bool WebRtcExecutionContext::StopAndWait(const std::chrono::milliseconds timeout) {
    BeginStop();
    return !scope_ || (!scope_->IsScopeThread() && scope_->WaitFor(timeout));
}

bool WebRtcExecutionContext::IsAccepting() const {
    return accepting_.load(std::memory_order_acquire) && scope_ && scope_->IsAccepting();
}

PxAwaitable<void> WebRtcExecutionContext::RunWork(std::weak_ptr<WebRtcExecutionContext> weak_context, std::function<void()> task) {
    if (const auto context = weak_context.lock(); context && context->IsAccepting() && task) {
        task();
    }
    co_return;
}

PxAwaitable<void> WebRtcExecutionContext::RunRepeatingTask(std::weak_ptr<WebRtcExecutionContext> weak_context,
                                                           const std::chrono::milliseconds interval, std::function<void()> task) {
    const auto executor = co_await asio::this_coro::executor;
    auto timer = std::make_shared<asio::steady_timer>(executor);
    for (;;) {
        timer->expires_after(interval);
        asio::error_code wait_error;
        co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, wait_error));
        const auto cancellation = co_await asio::this_coro::cancellation_state;
        const auto context = weak_context.lock();
        if (wait_error || cancellation.cancelled() != asio::cancellation_type::none || !context || !context->IsAccepting()) {
            co_return;
        }
        task();
    }
}

void WebRtcExecutionContext::Deliver(const WebRtcEvent& event) const {
    WebRtcEventCallback callback;
    {
        std::lock_guard lock(callback_mutex_);
        if (!accepting_.load(std::memory_order_acquire) || !callback_) {
            return;
        }
        callback = callback_;
    }
    callback(event);
}

} // namespace px
