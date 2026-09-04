#include "async_runtime.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace px {
namespace {

using IoWorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

class RuntimeThreadJoiner final {
  public:
    static std::shared_ptr<RuntimeThreadJoiner> Instance() {
        static const auto instance = std::make_shared<RuntimeThreadJoiner>();
        return instance;
    }

    ~RuntimeThreadJoiner() {
        {
            std::lock_guard lock(state_->mutex);
            state_->stopping = true;
        }
        state_->condition.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void Submit(std::vector<std::thread> threads) {
        {
            std::lock_guard lock(state_->mutex);
            state_->batches.push_back(std::move(threads));
        }
        state_->condition.notify_one();
    }

  private:
    struct JoinerState {
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<std::vector<std::thread>> batches;
        bool stopping = false;
    };

  public:
    RuntimeThreadJoiner() : state_(std::make_shared<JoinerState>()) {
        const auto state = state_;
        worker_ = std::thread([state]() {
            for (;;) {
                std::vector<std::thread> batch;
                {
                    std::unique_lock lock(state->mutex);
                    state->condition.wait(lock, [state]() { return state->stopping || !state->batches.empty(); });
                    if (state->batches.empty() && state->stopping) {
                        return;
                    }
                    batch = std::move(state->batches.front());
                    state->batches.pop_front();
                }
                for (auto& thread : batch) {
                    if (thread.joinable()) {
                        thread.join();
                    }
                }
            }
        });
    }

  private:
    std::shared_ptr<JoinerState> state_;
    std::thread worker_;
};

} // namespace

class PxAsyncRuntime::State final {
  public:
    explicit State(std::size_t worker_thread_count)
        : worker_thread_count_(std::max<std::size_t>(1, worker_thread_count)), control_guard_(asio::make_work_guard(control_context_)),
          state_guard_(asio::make_work_guard(state_context_)), worker_guard_(asio::make_work_guard(worker_context_)) {}

    asio::io_context control_context_;
    asio::io_context state_context_;
    asio::io_context worker_context_;
    std::size_t worker_thread_count_ = 1;
    std::optional<IoWorkGuard> control_guard_;
    std::optional<IoWorkGuard> state_guard_;
    std::optional<IoWorkGuard> worker_guard_;
    mutable std::mutex mutex_;
    std::set<std::thread::id> thread_ids_;
    std::thread::id control_thread_id_{};
    std::vector<std::thread> threads_;
    std::atomic_bool started_{false};
    std::atomic_bool stopping_{false};
    std::atomic_bool work_released_{false};
};

std::shared_ptr<PxAsyncRuntime> PxAsyncRuntime::Create(PxAsyncRuntimeOptions options) {
    return std::make_shared<PxAsyncRuntime>(options);
}

PxAsyncRuntime::PxAsyncRuntime(PxAsyncRuntimeOptions options) : state_(std::make_shared<State>(options.worker_threads)) {}

PxAsyncRuntime::~PxAsyncRuntime() {
    RequestStop();
    Join();
}

bool PxAsyncRuntime::Start() {
    bool expected = false;
    if (!state_->started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return false;
    }
    if (state_->stopping_.load(std::memory_order_acquire)) {
        return false;
    }

    const auto state = state_;
    const auto make_runner = [state](PxAsyncLane lane) {
        return [state, lane]() {
            const auto id = std::this_thread::get_id();
            {
                std::lock_guard lock(state->mutex_);
                state->thread_ids_.insert(id);
                if (lane == PxAsyncLane::kControl) {
                    state->control_thread_id_ = id;
                }
            }

            switch (lane) {
            case PxAsyncLane::kControl:
                state->control_context_.run();
                break;
            case PxAsyncLane::kState:
                state->state_context_.run();
                break;
            case PxAsyncLane::kWorker:
                state->worker_context_.run();
                break;
            }

            std::lock_guard lock(state->mutex_);
            state->thread_ids_.erase(id);
            if (lane == PxAsyncLane::kControl) {
                state->control_thread_id_ = {};
            }
        };
    };

    std::lock_guard lock(state_->mutex_);
    state_->threads_.emplace_back(make_runner(PxAsyncLane::kControl));
    state_->threads_.emplace_back(make_runner(PxAsyncLane::kState));
    for (std::size_t index = 0; index < state_->worker_thread_count_; ++index) {
        state_->threads_.emplace_back(make_runner(PxAsyncLane::kWorker));
    }
    return true;
}

void PxAsyncRuntime::RequestDrain() {
    bool expected = false;
    if (!state_->work_released_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    std::lock_guard lock(state_->mutex_);
    state_->control_guard_.reset();
    state_->state_guard_.reset();
    state_->worker_guard_.reset();
}

void PxAsyncRuntime::RequestStop() {
    bool expected = false;
    if (!state_->stopping_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }
    RequestDrain();
    state_->control_context_.stop();
    state_->state_context_.stop();
    state_->worker_context_.stop();
}

void PxAsyncRuntime::Join() {
    std::vector<std::thread> threads;
    bool called_from_runtime = false;
    {
        std::lock_guard lock(state_->mutex_);
        if (state_->threads_.empty()) {
            return;
        }
        called_from_runtime = state_->thread_ids_.contains(std::this_thread::get_id());
        threads.swap(state_->threads_);
    }

    if (called_from_runtime) {
        RuntimeThreadJoiner::Instance()->Submit(std::move(threads));
        return;
    }
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

asio::any_io_executor PxAsyncRuntime::Executor(PxAsyncLane lane) const {
    switch (lane) {
    case PxAsyncLane::kControl:
        return state_->control_context_.get_executor();
    case PxAsyncLane::kState:
        return state_->state_context_.get_executor();
    case PxAsyncLane::kWorker:
        return state_->worker_context_.get_executor();
    }
    return state_->worker_context_.get_executor();
}

void PxAsyncRuntime::DeferJoin(std::jthread thread) {
    if (!thread.joinable()) {
        return;
    }
    const auto owned_thread = std::make_shared<std::jthread>(std::move(thread));
    std::vector<std::thread> joiners{};
    joiners.emplace_back([owned_thread]() {
        if (owned_thread->joinable()) {
            owned_thread->join();
        }
    });
    RuntimeThreadJoiner::Instance()->Submit(std::move(joiners));
}

bool PxAsyncRuntime::DeferBlocking(std::function<void()> task) const {
    if (!task) {
        return false;
    }
    std::vector<std::thread> threads{};
    threads.emplace_back([task = std::move(task)]() mutable { task(); });
    RuntimeThreadJoiner::Instance()->Submit(std::move(threads));
    return true;
}

bool PxAsyncRuntime::IsRuntimeThread() const {
    std::lock_guard lock(state_->mutex_);
    return state_->thread_ids_.contains(std::this_thread::get_id());
}

bool PxAsyncRuntime::IsControlThread() const {
    std::lock_guard lock(state_->mutex_);
    return state_->control_thread_id_ == std::this_thread::get_id();
}

bool PxAsyncRuntime::IsStopping() const {
    return state_->stopping_.load(std::memory_order_acquire);
}

class PxAsyncScope::State final {
  public:
    struct Task {
        std::string name;
        std::shared_ptr<asio::cancellation_signal> cancellation;
    };

    explicit State(asio::any_io_executor executor) : executor_(asio::make_strand(std::move(executor))) {}

    asio::any_io_executor executor_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool accepting_ = true;
    std::uint64_t next_task_id_ = 1;
    std::map<std::uint64_t, Task> tasks_;
    PxAsyncScopeStatistics statistics_;
};

std::shared_ptr<PxAsyncScope> PxAsyncScope::Create(const std::shared_ptr<PxAsyncRuntime>& runtime, PxAsyncLane lane) {
    if (!runtime) {
        return {};
    }
    return std::make_shared<PxAsyncScope>(runtime, lane);
}

PxAsyncScope::PxAsyncScope(std::shared_ptr<PxAsyncRuntime> runtime, PxAsyncLane lane)
    : runtime_(std::move(runtime)), state_(std::make_shared<State>(runtime_->Executor(lane))) {}

PxAsyncScope::~PxAsyncScope() {
    BeginStop();
    if (!IsScopeThread()) {
        static_cast<void>(WaitFor(std::chrono::seconds(5)));
    }
}

bool PxAsyncScope::SpawnImpl(std::string name, std::function<PxAwaitable<void>()> factory) {
    if (!factory || runtime_->IsStopping()) {
        std::lock_guard lock(state_->mutex_);
        ++state_->statistics_.rejected;
        return false;
    }

    std::lock_guard lock(state_->mutex_);
    if (!state_->accepting_) {
        ++state_->statistics_.rejected;
        return false;
    }

    const auto task_id = state_->next_task_id_++;
    const auto cancellation = std::make_shared<asio::cancellation_signal>();
    std::optional<PxAwaitable<void>> task;
    try {
        task.emplace(factory());
    } catch (...) {
        ++state_->statistics_.failed;
        ++state_->statistics_.rejected;
        return false;
    }

    state_->tasks_.emplace(task_id, State::Task{std::move(name), cancellation});
    ++state_->statistics_.spawned;
    state_->statistics_.outstanding = state_->tasks_.size();
    const auto state = state_;
    asio::co_spawn(state_->executor_, std::move(*task),
                   asio::bind_cancellation_slot(cancellation->slot(),
                                                [state, task_id](std::exception_ptr error) { PxAsyncScope::Complete(state, task_id, error); }));
    return true;
}

void PxAsyncScope::Complete(const std::shared_ptr<State>& state, std::uint64_t task_id, const std::exception_ptr& error) {
    {
        std::lock_guard lock(state->mutex_);
        if (state->tasks_.erase(task_id) == 0) {
            return;
        }
        ++state->statistics_.completed;
        if (error) {
            ++state->statistics_.failed;
        }
        state->statistics_.outstanding = state->tasks_.size();
    }
    state->condition_.notify_all();
}

void PxAsyncScope::BeginStop() {
    std::vector<std::shared_ptr<asio::cancellation_signal>> cancellations;
    {
        std::lock_guard lock(state_->mutex_);
        if (!state_->accepting_) {
            return;
        }
        state_->accepting_ = false;
        cancellations.reserve(state_->tasks_.size());
        for (const auto& [task_id, task] : state_->tasks_) {
            static_cast<void>(task_id);
            cancellations.push_back(task.cancellation);
        }
    }

    for (const auto& cancellation : cancellations) {
        asio::post(state_->executor_, [cancellation]() { cancellation->emit(asio::cancellation_type::all); });
    }
    state_->condition_.notify_all();
}

bool PxAsyncScope::WaitFor(std::chrono::milliseconds timeout) {
    if (IsScopeThread()) {
        return false;
    }
    std::unique_lock lock(state_->mutex_);
    return state_->condition_.wait_for(lock, timeout, [state = state_]() { return state->tasks_.empty(); });
}

bool PxAsyncScope::StopAndWait(std::chrono::milliseconds timeout) {
    BeginStop();
    return WaitFor(timeout);
}

bool PxAsyncScope::IsAccepting() const {
    std::lock_guard lock(state_->mutex_);
    return state_->accepting_;
}

bool PxAsyncScope::IsScopeThread() const {
    return runtime_->IsRuntimeThread();
}

asio::any_io_executor PxAsyncScope::Executor() const {
    return state_->executor_;
}

PxAsyncScopeStatistics PxAsyncScope::GetStatistics() const {
    std::lock_guard lock(state_->mutex_);
    return state_->statistics_;
}

std::vector<std::string> PxAsyncScope::OutstandingTaskNames() const {
    std::vector<std::string> names;
    std::lock_guard lock(state_->mutex_);
    names.reserve(state_->tasks_.size());
    for (const auto& [task_id, task] : state_->tasks_) {
        static_cast<void>(task_id);
        names.push_back(task.name);
    }
    return names;
}

} // namespace px
