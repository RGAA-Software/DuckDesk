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
    std::uint64_t generation = 0;
};

using PxConnectionAttemptResult = PxResult<PxConnectionAttemptReady>;
using PxConnectionAttemptCompletion =
    std::function<void(PxConnectionAttemptResult)>;

class PxConnectionAttemptWorkflow final {
public:
    static std::shared_ptr<PxConnectionAttemptWorkflow> Create(
        const std::shared_ptr<PxAsyncRuntime>& runtime,
        std::chrono::milliseconds timeout = std::chrono::seconds(10));

    PxConnectionAttemptWorkflow(
        std::shared_ptr<PxAsyncScope> scope,
        std::chrono::milliseconds timeout);
    ~PxConnectionAttemptWorkflow();

    PxConnectionAttemptWorkflow(const PxConnectionAttemptWorkflow&) = delete;
    PxConnectionAttemptWorkflow& operator=(const PxConnectionAttemptWorkflow&) = delete;

    bool BeginAttempt(PxConnectionAttemptCompletion completion);
    bool MarkReady();
    bool FailActive(PxAsyncError error);
    void Stop();

    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] std::uint64_t Generation() const;
    [[nodiscard]] PxAsyncScopeStatistics Statistics() const;

private:
    using Operation = PxAsyncOneShot<PxConnectionAttemptReady>;

    static PxAwaitable<void> RunAttempt(
        std::shared_ptr<Operation> operation,
        std::chrono::steady_clock::time_point deadline,
        PxConnectionAttemptCompletion completion);

    mutable std::mutex mutex_;
    std::shared_ptr<PxAsyncScope> scope_;
    std::shared_ptr<Operation> active_;
    std::chrono::milliseconds timeout_;
    std::uint64_t generation_ = 0;
    bool ready_ = false;
    bool stopping_ = false;
};

} // namespace px

#endif // PX_COMMON_NEW_CONNECTION_ATTEMPT_WORKFLOW_H
