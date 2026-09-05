#ifndef PX_COMMON_NEW_ASYNC_RUNTIME_H
#define PX_COMMON_NEW_ASYNC_RUNTIME_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <asio2/external/asio.hpp>
#include <asio/version.hpp>

static_assert(ASIO_VERSION >= 103802, "px_common requires standalone Asio 1.38.2 or newer");

namespace px {

class PxBlockingExecutor;

template <typename T> using PxAwaitable = asio::awaitable<T>;

enum class PxAsyncLane {
    kControl,
    kState,
    kWorker,
};

struct PxAsyncRuntimeOptions {
    std::size_t worker_threads = 2;
    std::size_t blocking_threads = 2;
    std::size_t max_pending_blocking_tasks = 256;
};

class PxAsyncRuntime final {
  public:
    static std::shared_ptr<PxAsyncRuntime> Create(PxAsyncRuntimeOptions options = {});

    explicit PxAsyncRuntime(PxAsyncRuntimeOptions options);

    ~PxAsyncRuntime();

    PxAsyncRuntime(const PxAsyncRuntime&) = delete;
    PxAsyncRuntime& operator=(const PxAsyncRuntime&) = delete;

    bool Start();
    void RequestDrain();
    void RequestStop();
    void Join();

    [[nodiscard]] asio::any_io_executor Executor(PxAsyncLane lane) const;
    [[nodiscard]] std::shared_ptr<PxBlockingExecutor> BlockingExecutor() const;
    static void DeferJoin(std::thread thread);
    static void DeferJoin(std::jthread thread);
    [[nodiscard]] bool DeferBlocking(std::function<void()> task) const;
    [[nodiscard]] bool IsRuntimeThread() const;
    [[nodiscard]] bool IsControlThread() const;
    [[nodiscard]] bool IsStopping() const;

  private:
    class State;

    std::shared_ptr<State> state_;
};

struct PxAsyncScopeStatistics {
    std::uint64_t spawned = 0;
    std::uint64_t completed = 0;
    std::uint64_t rejected = 0;
    std::uint64_t failed = 0;
    std::uint64_t outstanding = 0;
};

class PxAsyncScope final {
  public:
    static std::shared_ptr<PxAsyncScope> Create(const std::shared_ptr<PxAsyncRuntime>& runtime, PxAsyncLane lane = PxAsyncLane::kWorker);

    PxAsyncScope(std::shared_ptr<PxAsyncRuntime> runtime, PxAsyncLane lane);

    ~PxAsyncScope();

    PxAsyncScope(const PxAsyncScope&) = delete;
    PxAsyncScope& operator=(const PxAsyncScope&) = delete;

    template <typename Factory> bool Spawn(std::string name, Factory&& factory) {
        return SpawnImpl(std::move(name), std::function<PxAwaitable<void>()>(std::forward<Factory>(factory)));
    }

    void BeginStop();
    [[nodiscard]] bool WaitFor(std::chrono::milliseconds timeout);
    [[nodiscard]] bool StopAndWait(std::chrono::milliseconds timeout);
    [[nodiscard]] bool IsAccepting() const;
    [[nodiscard]] bool IsScopeThread() const;
    [[nodiscard]] asio::any_io_executor Executor() const;
    [[nodiscard]] PxAsyncScopeStatistics GetStatistics() const;
    [[nodiscard]] std::vector<std::string> OutstandingTaskNames() const;

  private:
    class State;

    bool SpawnImpl(std::string name, std::function<PxAwaitable<void>()> factory);
    static void Complete(const std::shared_ptr<State>& state, std::uint64_t task_id, const std::exception_ptr& error);

    std::shared_ptr<PxAsyncRuntime> runtime_;
    std::shared_ptr<State> state_;
};

} // namespace px

#endif // PX_COMMON_NEW_ASYNC_RUNTIME_H
