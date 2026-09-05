#include "blocking_executor.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "async_runtime.h"

namespace px {

class PxBlockingExecutor::State final {
public:
    struct WorkItem {
        std::function<void()> task;
        std::chrono::steady_clock::time_point queued_at{};
    };

    explicit State(PxBlockingExecutorOptions options)
        : thread_count(std::max<std::size_t>(1, options.thread_count)), max_pending_tasks(std::max<std::size_t>(1, options.max_pending_tasks)) {}

    const std::size_t thread_count = 1;
    const std::size_t max_pending_tasks = 1;
    mutable std::mutex mutex;
    mutable std::condition_variable condition;
    std::deque<WorkItem> queue;
    std::vector<std::thread> threads;
    std::set<std::thread::id> worker_ids;
    PxBlockingExecutorStatistics statistics;
    bool started = false;
    bool accepting = true;
    bool stop_requested = false;
    PxBlockingShutdownMode shutdown_mode = PxBlockingShutdownMode::kDrain;
};

std::shared_ptr<PxBlockingExecutor> PxBlockingExecutor::Create(PxBlockingExecutorOptions options) {
    const auto executor = std::make_shared<PxBlockingExecutor>(options);
    executor->Start();
    return executor;
}

PxBlockingExecutor::PxBlockingExecutor(PxBlockingExecutorOptions options) : state_(std::make_shared<State>(options)) {}

PxBlockingExecutor::~PxBlockingExecutor() {
    RequestStop(PxBlockingShutdownMode::kCancelPending);
    Join();
}

void PxBlockingExecutor::Start() {
    const auto state = state_;
    std::lock_guard lock(state->mutex);
    if (state->started) {
        return;
    }
    state->started = true;
    try {
        for (std::size_t index = 0; index < state->thread_count; ++index) {
            state->threads.emplace_back([state]() {
                const auto worker_id = std::this_thread::get_id();
                {
                    std::lock_guard worker_lock(state->mutex);
                    state->worker_ids.insert(worker_id);
                }

                for (;;) {
                    State::WorkItem item;
                    {
                        std::unique_lock worker_lock(state->mutex);
                        state->condition.wait(worker_lock, [state]() { return state->stop_requested || !state->queue.empty(); });
                        if (state->stop_requested
                            && (state->shutdown_mode == PxBlockingShutdownMode::kCancelPending || state->queue.empty())) {
                            state->worker_ids.erase(worker_id);
                            state->condition.notify_all();
                            return;
                        }
                        item = std::move(state->queue.front());
                        state->queue.pop_front();
                        ++state->statistics.active;
                        state->statistics.pending = state->queue.size();
                    }

                    const auto started_at = std::chrono::steady_clock::now();
                    const auto queue_delay = started_at - item.queued_at;
                    bool failed = false;
                    try {
                        item.task();
                    } catch (...) {
                        failed = true;
                    }
                    const auto execution_time = std::chrono::steady_clock::now() - started_at;

                    {
                        std::lock_guard worker_lock(state->mutex);
                        --state->statistics.active;
                        ++state->statistics.completed;
                        state->statistics.failed += failed ? 1 : 0;
                        state->statistics.total_queue_delay += queue_delay;
                        state->statistics.total_execution_time += execution_time;
                        state->statistics.max_queue_delay = std::max(state->statistics.max_queue_delay, queue_delay);
                        state->statistics.max_execution_time = std::max(state->statistics.max_execution_time, execution_time);
                    }
                    state->condition.notify_all();
                }
            });
        }
    } catch (...) {
        state->accepting = false;
        state->stop_requested = true;
        state->shutdown_mode = PxBlockingShutdownMode::kCancelPending;
        state->condition.notify_all();
        throw;
    }
}

PxBlockingSubmitResult PxBlockingExecutor::TryPost(std::function<void()> task) {
    if (!task) {
        std::lock_guard lock(state_->mutex);
        ++state_->statistics.rejected;
        return PxBlockingSubmitResult::kInvalidTask;
    }

    {
        std::lock_guard lock(state_->mutex);
        if (!state_->accepting || state_->stop_requested) {
            ++state_->statistics.rejected;
            return PxBlockingSubmitResult::kStopped;
        }
        if (state_->queue.size() >= state_->max_pending_tasks) {
            ++state_->statistics.rejected;
            return PxBlockingSubmitResult::kQueueFull;
        }
        state_->queue.push_back(State::WorkItem{.task = std::move(task), .queued_at = std::chrono::steady_clock::now()});
        ++state_->statistics.submitted;
        state_->statistics.pending = state_->queue.size();
        state_->statistics.queue_high_watermark = std::max<std::uint64_t>(state_->statistics.queue_high_watermark, state_->queue.size());
    }
    state_->condition.notify_one();
    return PxBlockingSubmitResult::kAccepted;
}

void PxBlockingExecutor::RequestStop(PxBlockingShutdownMode mode) {
    {
        std::lock_guard lock(state_->mutex);
        state_->accepting = false;
        if (!state_->stop_requested || mode == PxBlockingShutdownMode::kCancelPending) {
            state_->stop_requested = true;
            state_->shutdown_mode = mode;
        }
        if (state_->shutdown_mode == PxBlockingShutdownMode::kCancelPending) {
            state_->statistics.cancelled += state_->queue.size();
            state_->queue.clear();
            state_->statistics.pending = 0;
        }
    }
    state_->condition.notify_all();
}

void PxBlockingExecutor::Join() {
    std::vector<std::thread> threads;
    bool called_from_worker = false;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->threads.empty()) {
            return;
        }
        called_from_worker = state_->worker_ids.contains(std::this_thread::get_id());
        threads.swap(state_->threads);
    }

    if (called_from_worker) {
        for (auto& thread : threads) {
            PxAsyncRuntime::DeferJoin(std::move(thread));
        }
        return;
    }
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

bool PxBlockingExecutor::WaitForIdle(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(state_->mutex);
    return state_->condition.wait_for(lock, timeout, [state = state_]() {
        return state->queue.empty() && state->statistics.active == 0;
    });
}

bool PxBlockingExecutor::IsWorkerThread() const {
    std::lock_guard lock(state_->mutex);
    return state_->worker_ids.contains(std::this_thread::get_id());
}

PxBlockingExecutorStatistics PxBlockingExecutor::GetStatistics() const {
    std::lock_guard lock(state_->mutex);
    return state_->statistics;
}

} // namespace px
