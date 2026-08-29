#ifndef PX_STREAM_LAUNCH_AUTH_WORKFLOW_H
#define PX_STREAM_LAUNCH_AUTH_WORKFLOW_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "px_common_new/async_result.h"
#include "px_common_new/async_runtime.h"
#include "px_console_client/console_user_app_api.h"
#include "px_console_client/console_user_device_api.h"

namespace px {

template<typename T>
struct StreamLaunchConsoleCall final {
    std::optional<T> value;
    px_console::ConsoleApiError error = px_console::ConsoleApiError::kInternalError;
    std::string server_message;

    static StreamLaunchConsoleCall Success(T result) {
        return StreamLaunchConsoleCall{.value = std::move(result)};
    }

    static StreamLaunchConsoleCall Failure(
        px_console::ConsoleApiError call_error,
        std::string message = {}) {
        return StreamLaunchConsoleCall{
            .error = call_error,
            .server_message = std::move(message),
        };
    }
};

enum class StreamLaunchTicketTarget {
    kDevice,
    kApplicationInstance,
};

struct StreamLaunchResolvedTicket final {
    px_console::ConsoleConnectionTicket ticket;
    std::string host;
    int port = 0;
    std::string remote_device_id;
    bool direct_probe_enabled = true;
};

struct StreamLaunchAuthRequest final {
    StreamLaunchTicketTarget target = StreamLaunchTicketTarget::kDevice;
    std::string device_id;
    std::string app_id;
    std::string instance_id;
    std::string client_nonce;
    std::vector<std::string> permissions;
    bool force_relay = false;
    bool force_direct_transport = false;
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(65);
};

struct StreamLaunchAuthPayload final {
    std::uint64_t generation = 0;
    std::string client_nonce;
    StreamLaunchResolvedTicket resolved;
    std::optional<px_console::ConsoleUserAppInstance> instance;
    bool direct_available = false;
};

struct StreamLaunchAuthHooks final {
    std::function<void(std::function<void()>)> post_blocking;
    std::function<StreamLaunchConsoleCall<px_console::ConsoleUserAppInstance>(
        const std::string&, const std::string&)> start_app;
    std::function<StreamLaunchConsoleCall<std::vector<px_console::ConsoleUserApplication>>()> query_apps;
    std::function<StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>(
        const std::string&, const std::string&, const std::vector<std::string>&)> issue_instance_ticket;
    std::function<StreamLaunchConsoleCall<px_console::ConsoleConnectionTicket>(
        const std::string&, const std::string&, const std::vector<std::string>&)> issue_device_ticket;
    std::function<PxResult<StreamLaunchResolvedTicket>(
        px_console::ConsoleConnectionTicket, StreamLaunchTicketTarget)> resolve_ticket;
    std::function<bool(const std::string&, int)> probe_direct;
};

using StreamLaunchAuthResult = PxResult<StreamLaunchAuthPayload>;
using StreamLaunchAuthCompletion =
    std::function<void(std::uint64_t, StreamLaunchAuthResult)>;

class StreamLaunchAuthWorkflow final {
public:
    static std::shared_ptr<StreamLaunchAuthWorkflow> Create(
        const std::shared_ptr<PxAsyncRuntime>& runtime);

    explicit StreamLaunchAuthWorkflow(std::shared_ptr<PxAsyncScope> scope);
    ~StreamLaunchAuthWorkflow();

    StreamLaunchAuthWorkflow(const StreamLaunchAuthWorkflow&) = delete;
    StreamLaunchAuthWorkflow& operator=(const StreamLaunchAuthWorkflow&) = delete;

    std::optional<std::uint64_t> Start(
        StreamLaunchAuthRequest request,
        StreamLaunchAuthHooks hooks,
        StreamLaunchAuthCompletion completion);
    void Stop();

    [[nodiscard]] bool IsCurrent(std::uint64_t generation) const;
    [[nodiscard]] PxAsyncScopeStatistics Statistics() const;

private:
    static PxAwaitable<void> Run(
        StreamLaunchAuthRequest request,
        StreamLaunchAuthHooks hooks,
        StreamLaunchAuthCompletion completion,
        std::shared_ptr<std::atomic_bool> cancelled,
        std::uint64_t generation);

    mutable std::mutex mutex_;
    std::shared_ptr<PxAsyncScope> scope_;
    std::shared_ptr<std::atomic_bool> active_cancellation_;
    std::uint64_t generation_ = 0;
    bool stopping_ = false;
};

} // namespace px

#endif // PX_STREAM_LAUNCH_AUTH_WORKFLOW_H
