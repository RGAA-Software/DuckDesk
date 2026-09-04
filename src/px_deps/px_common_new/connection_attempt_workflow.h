#ifndef PX_COMMON_NEW_CONNECTION_ATTEMPT_WORKFLOW_H
#define PX_COMMON_NEW_CONNECTION_ATTEMPT_WORKFLOW_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "async_operation.h"
#include "async_result.h"
#include "async_runtime.h"

namespace px {

struct PxConnectionAttemptReady final {
    std::uint64_t generation{0};
};

struct PxConnectionAttemptTicket final {
    std::uint64_t generation{0};
};

struct PxConnectionDisconnected final {
    std::uint64_t generation{0};
    PxAsyncError reason{};
};

using PxConnectionAttemptResult = PxResult<PxConnectionAttemptReady>;
using PxConnectionAttemptStartResult = PxResult<PxConnectionAttemptTicket>;
using PxConnectionDisconnectedResult = PxResult<PxConnectionDisconnected>;
using PxConnectionAttemptCompletion = std::function<void(PxConnectionAttemptResult)>;

class PxConnectionAttemptWorkflow final {
  public:
    static std::shared_ptr<PxConnectionAttemptWorkflow> Create(const std::shared_ptr<PxAsyncRuntime>& runtime,
                                                               std::chrono::milliseconds timeout = std::chrono::seconds(10));

    PxConnectionAttemptWorkflow(std::shared_ptr<PxAsyncScope> scope, std::chrono::milliseconds timeout);
    ~PxConnectionAttemptWorkflow();

    PxConnectionAttemptWorkflow(const PxConnectionAttemptWorkflow&) = delete;
    PxConnectionAttemptWorkflow& operator=(const PxConnectionAttemptWorkflow&) = delete;

    [[nodiscard]] PxConnectionAttemptStartResult StartAttempt();
    [[nodiscard]] static PxAwaitable<PxConnectionAttemptResult> WaitUntilReady(std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
                                                                               PxConnectionAttemptTicket ticket,
                                                                               std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] static PxAwaitable<PxConnectionDisconnectedResult> WaitUntilDisconnected(
        std::shared_ptr<PxConnectionAttemptWorkflow> workflow, PxConnectionAttemptTicket ticket);

    bool BeginAttempt(PxConnectionAttemptCompletion completion);
    bool MarkReady();
    bool MarkReady(std::uint64_t generation);
    bool MarkDisconnected(std::uint64_t generation, PxAsyncError reason);
    bool FailActive(PxAsyncError error);
    bool FailActive(std::uint64_t generation, PxAsyncError error);
    void Stop();

    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] std::uint64_t Generation() const;
    [[nodiscard]] PxAsyncScopeStatistics Statistics() const;

  private:
    using ReadyOperation = PxAsyncOneShot<PxConnectionAttemptReady>;
    using DisconnectedOperation = PxAsyncOneShot<PxConnectionDisconnected>;

    struct AttemptState final {
        std::shared_ptr<ReadyOperation> ready_operation;
        std::shared_ptr<DisconnectedOperation> disconnected_operation;
        PxConnectionAttemptTicket ticket{};
    };

    using AttemptStateResult = PxResult<AttemptState>;

    [[nodiscard]] AttemptStateResult StartAttemptState();

    static PxAwaitable<void> RunAttempt(std::shared_ptr<ReadyOperation> operation, std::chrono::steady_clock::time_point deadline,
                                        PxConnectionAttemptCompletion completion);

    mutable std::mutex mutex_;
    std::shared_ptr<PxAsyncScope> scope_;
    std::shared_ptr<ReadyOperation> active_ready_;
    std::shared_ptr<DisconnectedOperation> active_disconnected_;
    std::chrono::milliseconds timeout_{0};
    std::uint64_t generation_{0};
    bool ready_{false};
    bool stopping_{false};
};

} // namespace px

#endif // PX_COMMON_NEW_CONNECTION_ATTEMPT_WORKFLOW_H
