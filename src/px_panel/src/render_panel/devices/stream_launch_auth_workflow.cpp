#include "stream_launch_auth_workflow.h"

#include <algorithm>
#include <utility>

#include <asio2/external/asio.hpp>

#include "px_common_new/async_blocking_call.h"

namespace px {
namespace {

template<typename T>
PxAwaitable<PxResult<StreamLaunchConsoleCall<T>>> RunBlockingCall(
    const StreamLaunchAuthHooks& hooks,
    asio::any_io_executor executor,
    std::chrono::steady_clock::time_point deadline,
    std::shared_ptr<std::atomic_bool> cancelled,
    std::string stage,
    std::function<StreamLaunchConsoleCall<T>()> call) {
    co_return co_await AwaitBlockingCall<StreamLaunchConsoleCall<T>>(
        hooks.post_blocking,
        std::move(executor),
        deadline,
        std::move(cancelled),
        std::move(stage),
        [call = std::move(call)](const std::shared_ptr<std::atomic_bool>&) mutable {
            return call();
        });
}

PxAsyncError ConsoleFailure(
    std::string stage,
    px_console::ConsoleApiError error,
    std::string server_message) {
    const auto message = server_message.empty()
        ? std::string(px_console::ConsoleApiErrorAsString(error))
        : std::move(server_message);
    const bool retryable = error == px_console::ConsoleApiError::kNetworkUnavailable
        || error == px_console::ConsoleApiError::kRateLimited
        || error == px_console::ConsoleApiError::kServiceUnavailable;
    return MakePxAsyncError(
        PxAsyncErrorCode::kServiceRejected,
        std::move(stage),
        message,
        retryable,
        std::to_string(static_cast<int>(error)));
}

template<typename T>
PxResult<T> TakeConsoleValue(
    PxResult<StreamLaunchConsoleCall<T>> call_result,
    std::string stage) {
    if (!call_result) {
        return PxResult<T>::Failure(call_result.Error());
    }
    auto call = call_result.TakeValue();
    if (!call.value) {
        return PxResult<T>::Failure(ConsoleFailure(
            std::move(stage), call.error, std::move(call.server_message)));
    }
    return PxResult<T>::Success(std::move(*call.value));
}

PxAwaitable<bool> WaitForPoll(
    asio::any_io_executor executor,
    std::chrono::steady_clock::time_point deadline,
    const std::shared_ptr<std::atomic_bool>& cancelled) {
    if (cancelled->load(std::memory_order_acquire)
        || std::chrono::steady_clock::now() >= deadline) {
        co_return false;
    }
    const auto timer = std::make_shared<asio::steady_timer>(executor);
    timer->expires_at(std::min(
        deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(800)));
    asio::error_code error;
    co_await timer->async_wait(asio::redirect_error(asio::use_awaitable, error));
    co_return !error && !cancelled->load(std::memory_order_acquire)
        && std::chrono::steady_clock::now() < deadline;
}

PxResult<void> ValidateRequest(
    const StreamLaunchAuthRequest& request,
    const StreamLaunchAuthHooks& hooks) {
    const bool common_valid = !request.client_nonce.empty()
        && !request.permissions.empty()
        && request.deadline > std::chrono::steady_clock::now()
        && hooks.post_blocking && hooks.resolve_ticket && hooks.probe_direct;
    const bool target_valid = request.target == StreamLaunchTicketTarget::kDevice
        ? (!request.device_id.empty() && hooks.issue_device_ticket)
        : (!request.app_id.empty() && hooks.start_app && hooks.query_apps
            && hooks.issue_instance_ticket);
    if (!common_valid || !target_valid) {
        return PxResult<void>::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kInvalidArgument,
            "stream-launch.validate",
            "stream launch authentication request is incomplete"));
    }
    return PxResult<void>::Success();
}

} // namespace

std::shared_ptr<StreamLaunchAuthWorkflow> StreamLaunchAuthWorkflow::Create(
    const std::shared_ptr<PxAsyncRuntime>& runtime) {
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kWorker);
    return scope ? std::make_shared<StreamLaunchAuthWorkflow>(scope) : nullptr;
}

StreamLaunchAuthWorkflow::StreamLaunchAuthWorkflow(std::shared_ptr<PxAsyncScope> scope)
    : scope_(std::move(scope)) {}

StreamLaunchAuthWorkflow::~StreamLaunchAuthWorkflow() {
    Stop();
}

std::optional<std::uint64_t> StreamLaunchAuthWorkflow::Start(
    StreamLaunchAuthRequest request,
    StreamLaunchAuthHooks hooks,
    StreamLaunchAuthCompletion completion) {
    if (!completion || !ValidateRequest(request, hooks)) {
        return std::nullopt;
    }

    std::shared_ptr<PxAsyncScope> scope;
    std::shared_ptr<std::atomic_bool> previous;
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    std::uint64_t generation = 0;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !scope_ || !scope_->IsAccepting()) {
            return std::nullopt;
        }
        scope = scope_;
        previous = std::move(active_cancellation_);
        active_cancellation_ = cancellation;
        generation = ++generation_;
    }
    if (previous) {
        previous->store(true, std::memory_order_release);
    }

    if (!scope->Spawn("stream-launch-auth",
            [request = std::move(request), hooks = std::move(hooks),
             completion = std::move(completion), cancellation, generation]() mutable {
                return Run(std::move(request), std::move(hooks),
                           std::move(completion), cancellation, generation);
            })) {
        cancellation->store(true, std::memory_order_release);
        std::lock_guard lock(mutex_);
        if (active_cancellation_ == cancellation) {
            active_cancellation_.reset();
        }
        return std::nullopt;
    }
    return generation;
}

PxAwaitable<void> StreamLaunchAuthWorkflow::Run(
    StreamLaunchAuthRequest request,
    StreamLaunchAuthHooks hooks,
    StreamLaunchAuthCompletion completion,
    std::shared_ptr<std::atomic_bool> cancelled,
    std::uint64_t generation) {
    const auto executor = co_await asio::this_coro::executor;
    std::optional<px_console::ConsoleUserAppInstance> instance;

    if (request.target == StreamLaunchTicketTarget::kApplicationInstance) {
        if (request.instance_id.empty()) {
            auto start_call = co_await RunBlockingCall<px_console::ConsoleUserAppInstance>(
                hooks, executor, request.deadline, cancelled, "stream-launch.start-app",
                [hooks, app_id = request.app_id, nonce = request.client_nonce]() {
                    return hooks.start_app(app_id, nonce);
                });
            auto start_result = TakeConsoleValue(
                std::move(start_call), "stream-launch.start-app");
            if (!start_result) {
                completion(generation, StreamLaunchAuthResult::Failure(start_result.Error()));
                co_return;
            }
            instance = start_result.TakeValue();
            if (instance->instance_id.empty()) {
                completion(generation, StreamLaunchAuthResult::Failure(MakePxAsyncError(
                    PxAsyncErrorCode::kProtocolError,
                    "stream-launch.start-app",
                    "Console returned an application instance without an ID",
                    false,
                    "INVALID_APP_INSTANCE")));
                co_return;
            }
            request.instance_id = instance->instance_id;
        }

        while (instance && instance->state == "starting") {
            if (!co_await WaitForPoll(executor, request.deadline, cancelled)) {
                completion(generation, StreamLaunchAuthResult::Failure(MakePxAsyncError(
                    cancelled->load(std::memory_order_acquire)
                        ? PxAsyncErrorCode::kCancelled : PxAsyncErrorCode::kTimeout,
                    "stream-launch.wait-running",
                    cancelled->load(std::memory_order_acquire)
                        ? "stream launch was replaced"
                        : "Console application did not reach running before the deadline",
                    !cancelled->load(std::memory_order_acquire))));
                co_return;
            }
            auto query_call = co_await RunBlockingCall<
                std::vector<px_console::ConsoleUserApplication>>(
                hooks, executor, request.deadline, cancelled, "stream-launch.query-apps",
                [hooks]() { return hooks.query_apps(); });
            auto query_result = TakeConsoleValue(
                std::move(query_call), "stream-launch.query-apps");
            if (!query_result) {
                if (query_result.Error().code == PxAsyncErrorCode::kCancelled
                    || query_result.Error().code == PxAsyncErrorCode::kTimeout) {
                    completion(generation, StreamLaunchAuthResult::Failure(query_result.Error()));
                    co_return;
                }
                continue;
            }
            auto applications = query_result.TakeValue();
            const auto app = std::find_if(
                applications.begin(), applications.end(),
                [&request](const px_console::ConsoleUserApplication& candidate) {
                    return candidate.app_id == request.app_id;
                });
            if (app != applications.end() && app->running_instance
                && app->running_instance->instance_id == request.instance_id) {
                instance = *app->running_instance;
            }
        }

        if (instance && instance->state != "running") {
            completion(generation, StreamLaunchAuthResult::Failure(MakePxAsyncError(
                PxAsyncErrorCode::kServiceRejected,
                "stream-launch.wait-running",
                "Console application entered state: " + instance->state,
                false,
                instance->error_code.empty() ? "APP_NOT_RUNNING" : instance->error_code)));
            co_return;
        }
    }

    PxResult<px_console::ConsoleConnectionTicket> ticket_result =
        PxResult<px_console::ConsoleConnectionTicket>::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kProtocolError,
            "stream-launch.issue-ticket",
            "ticket request was not executed"));
    if (request.target == StreamLaunchTicketTarget::kApplicationInstance) {
        auto ticket_call = co_await RunBlockingCall<px_console::ConsoleConnectionTicket>(
            hooks, executor, request.deadline, cancelled, "stream-launch.issue-instance-ticket",
            [hooks, instance_id = request.instance_id, nonce = request.client_nonce,
             permissions = request.permissions]() {
                return hooks.issue_instance_ticket(instance_id, nonce, permissions);
            });
        ticket_result = TakeConsoleValue(
            std::move(ticket_call), "stream-launch.issue-instance-ticket");
    }
    else {
        auto ticket_call = co_await RunBlockingCall<px_console::ConsoleConnectionTicket>(
            hooks, executor, request.deadline, cancelled, "stream-launch.issue-device-ticket",
            [hooks, device_id = request.device_id, nonce = request.client_nonce,
             permissions = request.permissions]() {
                return hooks.issue_device_ticket(device_id, nonce, permissions);
            });
        ticket_result = TakeConsoleValue(
            std::move(ticket_call), "stream-launch.issue-device-ticket");
    }
    if (!ticket_result) {
        completion(generation, StreamLaunchAuthResult::Failure(ticket_result.Error()));
        co_return;
    }

    auto resolved = hooks.resolve_ticket(ticket_result.TakeValue(), request.target);
    if (!resolved) {
        completion(generation, StreamLaunchAuthResult::Failure(resolved.Error()));
        co_return;
    }
    auto resolved_ticket = resolved.TakeValue();
    if (resolved_ticket.ticket.stream_id.empty()
        || resolved_ticket.ticket.renewal_token.empty()) {
        completion(generation, StreamLaunchAuthResult::Failure(MakePxAsyncError(
            PxAsyncErrorCode::kProtocolError,
            "stream-launch.resolve-ticket",
            "Console returned a ticket without a runtime stream ID or renewal capability",
            false,
            "INVALID_CONSOLE_TICKET")));
        co_return;
    }
    bool direct_available = false;
    const bool should_probe = !request.force_relay
        && (request.force_direct_transport || resolved_ticket.direct_probe_enabled);
    if (should_probe) {
        auto probe_result = co_await AwaitBlockingCall<bool>(
            hooks.post_blocking,
            executor,
            request.deadline,
            cancelled,
            "stream-launch.probe",
            [probe = hooks.probe_direct,
             host = resolved_ticket.host,
             port = resolved_ticket.port](const std::shared_ptr<std::atomic_bool>&) {
                return probe(host, port);
            });
        if (!probe_result) {
            completion(generation, StreamLaunchAuthResult::Failure(probe_result.Error()));
            co_return;
        }
        direct_available = probe_result.Value();
    }

    completion(generation, StreamLaunchAuthResult::Success(StreamLaunchAuthPayload{
        .generation = generation,
        .client_nonce = std::move(request.client_nonce),
        .resolved = std::move(resolved_ticket),
        .instance = std::move(instance),
        .direct_available = direct_available,
    }));
    co_return;
}

void StreamLaunchAuthWorkflow::Stop() {
    std::shared_ptr<std::atomic_bool> cancellation;
    std::shared_ptr<PxAsyncScope> scope;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        cancellation = std::move(active_cancellation_);
        scope = scope_;
    }
    if (cancellation) {
        cancellation->store(true, std::memory_order_release);
    }
    if (scope) {
        if (scope->IsScopeThread()) {
            scope->BeginStop();
        }
        else {
            static_cast<void>(scope->StopAndWait(std::chrono::seconds(2)));
        }
    }
}

bool StreamLaunchAuthWorkflow::IsCurrent(std::uint64_t generation) const {
    std::lock_guard lock(mutex_);
    return !stopping_ && generation == generation_;
}

PxAsyncScopeStatistics StreamLaunchAuthWorkflow::Statistics() const {
    std::shared_ptr<PxAsyncScope> scope;
    {
        std::lock_guard lock(mutex_);
        scope = scope_;
    }
    return scope ? scope->GetStatistics() : PxAsyncScopeStatistics{};
}

} // namespace px
