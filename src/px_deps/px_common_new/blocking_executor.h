#ifndef PX_COMMON_NEW_BLOCKING_EXECUTOR_H
#define PX_COMMON_NEW_BLOCKING_EXECUTOR_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace px {

struct PxBlockingExecutorOptions {
    std::size_t thread_count = 2;
    std::size_t max_pending_tasks = 256;
};

enum class PxBlockingSubmitResult {
    kAccepted,
    kInvalidTask,
    kQueueFull,
    kStopped,
};

enum class PxBlockingShutdownMode {
    kDrain,
    kCancelPending,
};

struct PxBlockingExecutorStatistics {
    std::uint64_t submitted = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t rejected = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t active = 0;
    std::uint64_t pending = 0;
    std::uint64_t queue_high_watermark = 0;
    std::chrono::nanoseconds total_queue_delay{};
    std::chrono::nanoseconds total_execution_time{};
    std::chrono::nanoseconds max_queue_delay{};
    std::chrono::nanoseconds max_execution_time{};
};

class PxBlockingExecutor final {
public:
    static std::shared_ptr<PxBlockingExecutor> Create(PxBlockingExecutorOptions options = {});

    explicit PxBlockingExecutor(PxBlockingExecutorOptions options);
    ~PxBlockingExecutor();

    PxBlockingExecutor(const PxBlockingExecutor&) = delete;
    PxBlockingExecutor& operator=(const PxBlockingExecutor&) = delete;

    [[nodiscard]] PxBlockingSubmitResult TryPost(std::function<void()> task);
    void RequestStop(PxBlockingShutdownMode mode = PxBlockingShutdownMode::kDrain);
    void Join();
    [[nodiscard]] bool WaitForIdle(std::chrono::milliseconds timeout) const;
    [[nodiscard]] bool IsWorkerThread() const;
    [[nodiscard]] PxBlockingExecutorStatistics GetStatistics() const;

private:
    class State;

    void Start();

    std::shared_ptr<State> state_;
};

} // namespace px

#endif // PX_COMMON_NEW_BLOCKING_EXECUTOR_H
