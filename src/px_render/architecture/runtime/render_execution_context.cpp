#include "runtime/render_execution_context.h"

#include <thread>
#include <utility>

#include <asio2/external/asio.hpp>

#include "px_common/log.h"

namespace px {
namespace {

PxAwaitable<void> RunTask(std::function<void()> task) {
    task();
    co_return;
}

PxAwaitable<void> RunDelayedTask(asio::any_io_executor executor, const std::chrono::milliseconds delay, std::function<void()> task) {
    asio::steady_timer timer(std::move(executor));
    timer.expires_after(delay);
    asio::error_code error;
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
    if (!error) {
        task();
    }
}

void WaitForScopeDrain(const std::shared_ptr<PxAsyncScope>& scope, const std::string& owner_name) {
    if (!scope->WaitFor(std::chrono::seconds(5))) {
        const auto statistics = scope->GetStatistics();
        LOGE("event=execution_context.stop component={} code=SCOPE_DRAIN_TIMEOUT operation=stop outcome=timeout recoverable=true outstanding={}",
             owner_name, statistics.outstanding);
    }
}

} // namespace

std::shared_ptr<RenderExecutionContext> RenderExecutionContext::Create(const std::shared_ptr<PxAsyncRuntime>& runtime, std::string owner_name) {
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
    if (!runtime || !scope || runtime->IsStopping()) {
        return {};
    }
    return std::make_shared<RenderExecutionContext>(runtime, scope, std::move(owner_name));
}

RenderExecutionContext::RenderExecutionContext(std::shared_ptr<PxAsyncRuntime> runtime, std::shared_ptr<PxAsyncScope> scope, std::string owner_name)
    : runtime_(std::move(runtime)), scope_(std::move(scope)), owner_name_(std::move(owner_name)) {}

RenderExecutionContext::~RenderExecutionContext() {
    Stop();
}

bool RenderExecutionContext::Post(std::function<void()> task) {
    if (!task || stopping_.load() || !scope_) {
        return false;
    }
    return scope_->Spawn(owner_name_ + ".post", [task = std::move(task)]() mutable { return RunTask(std::move(task)); });
}

bool RenderExecutionContext::PostDelayed(const std::chrono::milliseconds delay, std::function<void()> task) {
    if (!task || delay.count() < 0 || stopping_.load() || !scope_) {
        return false;
    }
    const auto executor = scope_->Executor();
    return scope_->Spawn(owner_name_ + ".delay",
                         [executor, delay, task = std::move(task)]() mutable { return RunDelayedTask(executor, delay, std::move(task)); });
}

void RenderExecutionContext::Stop() {
    if (stopping_.exchange(true) || !scope_) {
        return;
    }
    auto scope = std::move(scope_);
    scope->BeginStop();
    if (scope->GetStatistics().outstanding != 0) {
        std::thread drain_thread([scope, owner_name = owner_name_] { WaitForScopeDrain(scope, owner_name); });
        PxAsyncRuntime::DeferJoin(std::move(drain_thread));
    }
    runtime_.reset();
}

bool RenderExecutionContext::IsStopping() const noexcept {
    return stopping_.load();
}

} // namespace px
