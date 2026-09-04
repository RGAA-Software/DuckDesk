#ifndef PX_COMMON_NEW_RECONNECT_SUPERVISOR_H
#define PX_COMMON_NEW_RECONNECT_SUPERVISOR_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "async_result.h"
#include "async_runtime.h"
#include "reconnect_backoff.h"

namespace px {

class PxConnectionAttemptWorkflow;

struct PxReconnectSupervisorOptions final {
    std::string component{};
    std::chrono::milliseconds connection_timeout{std::chrono::seconds(10)};
    std::chrono::milliseconds adapter_stop_timeout{std::chrono::seconds(3)};
    PxReconnectBackoffOptions backoff{};
};

struct PxReconnectSupervisorStatistics final {
    std::uint64_t connection_attempts{0};
    std::uint64_t successful_connections{0};
    std::uint64_t reconnect_waits{0};
    std::uint64_t adapter_reset_failures{0};
    std::uint32_t consecutive_failures{0};
    std::uint64_t generation{0};
};

using PxReconnectStartAttempt = std::function<PxResult<void>(std::uint64_t)>;
using PxReconnectStopAttempt =
    std::function<PxAwaitable<PxResult<void>>(std::chrono::steady_clock::time_point)>;
using PxReconnectReadyHandler = std::function<void(std::uint64_t)>;
using PxReconnectTerminalHandler = std::function<void(std::uint64_t, const PxAsyncError&)>;

struct PxReconnectSupervisorHooks final {
    PxReconnectStartAttempt start_attempt{};
    PxReconnectStopAttempt stop_attempt{};
    PxReconnectReadyHandler on_ready{};
    PxReconnectTerminalHandler on_terminal{};
};

class PxReconnectSupervisor final {
  public:
    static std::shared_ptr<PxReconnectSupervisor> Create(
        const std::shared_ptr<PxAsyncRuntime>& runtime,
        PxReconnectSupervisorOptions options);

    PxReconnectSupervisor(
        PxReconnectSupervisorOptions options,
        std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
        std::shared_ptr<PxReconnectBackoff> backoff);
    ~PxReconnectSupervisor();

    PxReconnectSupervisor(const PxReconnectSupervisor&) = delete;
    PxReconnectSupervisor& operator=(const PxReconnectSupervisor&) = delete;

    [[nodiscard]] static PxAwaitable<void> Run(
        std::shared_ptr<PxReconnectSupervisor> supervisor,
        PxReconnectSupervisorHooks hooks);

    [[nodiscard]] bool MarkReady();
    [[nodiscard]] bool MarkDisconnected(PxAsyncError reason);
    [[nodiscard]] bool FailActive(PxAsyncError error);
    void Stop();

    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] bool IsStopping() const;
    [[nodiscard]] std::uint64_t Generation() const;
    [[nodiscard]] PxReconnectSupervisorStatistics Statistics() const;

  private:
    [[nodiscard]] static bool IsStopResult(const PxAsyncError& error);
    [[nodiscard]] static PxAwaitable<bool> ResetAdapterUntilStopped(
        const std::shared_ptr<PxReconnectSupervisor>& supervisor,
        const PxReconnectStopAttempt& stop_attempt);
    [[nodiscard]] PxReconnectBackoffStep NextBackoff();
    [[nodiscard]] PxResult<void> StartAttemptIfRunning(
        const PxReconnectStartAttempt& start_attempt,
        std::uint64_t generation);

    const PxReconnectSupervisorOptions options_;
    const std::shared_ptr<PxConnectionAttemptWorkflow> workflow_;
    const std::shared_ptr<PxReconnectBackoff> backoff_;
    mutable std::mutex lifecycle_mutex_;
    std::atomic_bool stopping_{false};
    std::atomic_uint64_t connection_attempts_{0};
    std::atomic_uint64_t successful_connections_{0};
    std::atomic_uint64_t reconnect_waits_{0};
    std::atomic_uint64_t adapter_reset_failures_{0};
    std::atomic_uint32_t consecutive_failures_{0};
};

} // namespace px

#endif // PX_COMMON_NEW_RECONNECT_SUPERVISOR_H
