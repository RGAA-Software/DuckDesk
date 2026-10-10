#include "iroh_dialer.h"

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>

#include "px_common/async_runtime.h"
#include "px_common/log.h"
#include "px_common/scope_exit.h"

namespace px {
IrohDialer::IrohDialer(IrohDialParameters parameters, Completion completion)
    : parameters_(std::move(parameters)), completion_(std::move(completion)) {}
IrohDialer::~IrohDialer() { Stop(); }

void IrohDialer::Start() {
    std::lock_guard lock(mutex_);
    if (started_once_ || stopped_) return;
    started_once_ = true;
    worker_.emplace([owner = weak_from_this(), parameters = std::move(parameters_)]() mutable { Run(owner, std::move(parameters)); });
}

void IrohDialer::Stop() {
    std::optional<std::jthread> worker{};
    std::shared_ptr<transport::Endpoint> endpoint{};
    {
        std::lock_guard lock(mutex_);
        if (stopped_.exchange(true)) return;
        endpoint = endpoint_;
        worker = std::move(worker_);
    }
    if (endpoint) endpoint->Close();
    // A media callback can stop the client while the dial worker is draining the
    // previous adapter. Joining here would create a cross-worker shutdown cycle.
    if (worker) PxAsyncRuntime::DeferJoin(std::move(*worker));
}

void IrohDialer::Complete(IrohDialResult result) {
    if (!stopped_ && completion_) completion_(std::move(result));
}

bool IrohDialer::IsStopped(const std::weak_ptr<IrohDialer>& owner) {
    const auto dialer = owner.lock();
    return !dialer || dialer->stopped_;
}

bool IrohDialer::RetryDelay(const std::weak_ptr<IrohDialer>& owner) {
    for (std::size_t tick{}; tick < 10; ++tick) {
        if (IsStopped(owner)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !IsStopped(owner);
}

bool IrohDialer::RefreshEndpoint(const std::shared_ptr<transport::Endpoint>& endpoint, IrohDialParameters& parameters) {
    if (!parameters.refresh_endpoint) return true;
    const auto refreshed = parameters.refresh_endpoint();
    if (!refreshed) return false;
    const auto configuration = nlohmann::json::parse(refreshed->endpoint_configuration, nullptr, false);
    if (!configuration.is_object()) return false;
    const auto candidates = configuration.value("relays", nlohmann::json::array());
    if (!endpoint->UpdateRelays(candidates.dump())) {
        LOGW("event=iroh.relay_candidates outcome=update_failed");
        return false;
    }
    auto current_configuration = nlohmann::json::parse(parameters.endpoint_configuration, nullptr, false);
    if (current_configuration.is_object() && current_configuration.value("relays", nlohmann::json::array()) != candidates) {
        current_configuration["relays"] = candidates;
        parameters.endpoint_configuration = current_configuration.dump();
        LOGI("event=iroh.relay_candidates outcome=updated count={}", candidates.size());
    }
    if (refreshed->endpoint_address != parameters.endpoint_address) {
        const auto local_address = endpoint->Address();
        LOGI("event=iroh.connect outcome=address_refreshed local={} remote={}", local_address ? *local_address : "unavailable",
             refreshed->endpoint_address);
        parameters.endpoint_address = refreshed->endpoint_address;
    }
    return true;
}

void IrohDialer::Run(std::weak_ptr<IrohDialer> owner, IrohDialParameters parameters) {
    const auto clear_credentials = PxScopeExit{[&parameters] {
        for (auto& [name, value] : parameters.frontend) std::fill(value.begin(), value.end(), '\0');
    }};
    if (parameters.endpoint_address.empty()) {
        if (const auto dialer = owner.lock()) dialer->Complete({.error_code = "IROH_ENDPOINT_MISSING"});
        return;
    }
    const auto endpoint = transport::Endpoint::Bind(parameters.endpoint_configuration, 5000);
    {
        const auto dialer = owner.lock();
        if (!dialer) return;
        if (!endpoint) {
            dialer->Complete({.error_code = "IROH_ENDPOINT_UNAVAILABLE"});
            return;
        }
        std::lock_guard lock(dialer->mutex_);
        if (dialer->stopped_) return;
        dialer->endpoint_ = endpoint;
    }
    std::optional<std::chrono::steady_clock::time_point> retry_deadline{};
    while (!IsStopped(owner)) {
        // One endpoint preserves the authenticated peer identity across network failure.
        // Reuse the existing business credentials; there is no transport ticket exchange.
        if (const auto dialer = owner.lock()) dialer->Complete({.stage = IrohDialStage::kStarting});
        if (IsStopped(owner)) return;
        if (retry_deadline && std::chrono::steady_clock::now() >= *retry_deadline) {
            if (const auto dialer = owner.lock()) dialer->Complete({.error_code = "IROH_RECONNECT_EXPIRED"});
            return;
        }
        if (retry_deadline && !RefreshEndpoint(endpoint, parameters)) {
            if (RetryDelay(owner)) continue;
            return;
        }
        if (IsStopped(owner)) return;
        const auto connect_started = std::chrono::steady_clock::now();
        const auto remaining_timeout = [&retry_deadline]() -> std::uint32_t {
            if (!retry_deadline) return 10000;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*retry_deadline - std::chrono::steady_clock::now()).count();
            return static_cast<std::uint32_t>(std::clamp<std::int64_t>(remaining, 1, 10000));
        };
        const auto connection = endpoint->Connect(parameters.endpoint_address, remaining_timeout());
        if (retry_deadline) {
            LOGI("event=iroh.reconnect stage=quic outcome={} elapsed_ms={}", connection ? "connected" : "failed",
                 std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - connect_started).count());
        }
        if (!connection) {
            if (!retry_deadline && parameters.refresh_endpoint) {
                retry_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            }
            if (retry_deadline && RetryDelay(owner)) continue;
            if (const auto dialer = owner.lock()) dialer->Complete({.error_code = "IROH_CONNECT_FAILED"});
            return;
        }
        const auto opened = transport::OpenFrontend(connection, parameters.frontend, remaining_timeout());
        if (IsStopped(owner)) return;
        if (!opened || !opened->reply.accepted) {
            LOGW("event=iroh.reconnect stage=admission outcome=failed code={}", opened ? opened->reply.code : "NO_REPLY");
            connection->Close();
            // The server can still be pruning the old binding. Never take it over.
            if (retry_deadline && (!opened || opened->reply.code == "SESSION_OCCUPIED") && RetryDelay(owner)) continue;
            if (const auto dialer = owner.lock()) dialer->Complete({.error_code = opened ? opened->reply.code : "IROH_ADMISSION_FAILED"});
            return;
        }
        LOGI("event=iroh.connect outcome=accepted reconnect={}", retry_deadline.has_value());
        retry_deadline.reset();
        if (const auto dialer = owner.lock())
            dialer->Complete({.stage = IrohDialStage::kConnected, .connection = connection, .channels = opened->channels});
        auto next_refresh = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!IsStopped(owner) && !connection->IsClosed()) {
            if (parameters.refresh_endpoint && std::chrono::steady_clock::now() >= next_refresh) {
                static_cast<void>(RefreshEndpoint(endpoint, parameters));
                next_refresh = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (IsStopped(owner) || !connection->CanReconnect()) return;
        LOGW("event=iroh.connect outcome=network_lost action=readmit");
        // Admission must finish while the original application/runtime is still available.
        // An expired or rejected business session is never recreated behind the user's back.
        retry_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        if (!RetryDelay(owner)) return;
    }
}
}  // namespace px
