#include "thread.h"

#include <utility>

#include "log.h"
#include "memory_stat.h"

namespace px
{
    class Thread::State : public std::enable_shared_from_this<Thread::State> {
    public:
        State(std::string name, int max_tasks)
            : max_tasks_(max_tasks), name_(std::move(name)) {
        }

        State(OnceTask task, std::string name)
            : once_task_(std::move(task)), name_(std::move(name)) {
        }

        ~State() {
            exit_.store(true, std::memory_order_release);
            take_var_.notify_all();
            ReapThread();
        }

        void StartLoop() {
            {
                std::lock_guard lock(init_mtx_);
                if (init_) {
                    return;
                }
                init_ = true;
            }

            auto self = shared_from_this();
            {
                std::lock_guard lock(thread_mtx_);
                thread_ = std::make_shared<std::thread>([self]() {
                    self->SetNativeThreadId();
                    self->TaskLoop();
                });
                thread_id_ = GeneralIdLocked();
            }
        }

        void StartOnce(bool join) {
            {
                std::lock_guard lock(init_mtx_);
                if (init_) {
                    return;
                }
                init_ = true;
            }

            auto self = shared_from_this();
            {
                std::lock_guard lock(thread_mtx_);
                thread_ = std::make_shared<std::thread>([self]() {
                    self->SetNativeThreadId();
                    auto task = std::move(self->once_task_);
                    if (task) {
                        try {
                            task();
                        }
                        catch (const std::exception& error) {
                            LOGE("Uncaught exception in once thread task '{}': {}",
                                 self->name_, error.what());
                        }
                        catch (...) {
                            LOGE("Uncaught non-standard exception in once thread task '{}'",
                                 self->name_);
                        }
                    }
                    self->last_task_returned_.store(true, std::memory_order_release);
                    self->exit_loop_.store(true, std::memory_order_release);
                });
                thread_id_ = GeneralIdLocked();
            }
            if (join) {
                Join();
            }
        }

        void Post(ThreadTaskPtr task) {
            if (!task || exit_.load(std::memory_order_acquire)) {
                return;
            }

            ThreadTaskPtr removed_task;
            std::function<void(ThreadTaskPtr)> removed_callback;
            {
                std::lock_guard lock(task_mtx_);
                if (exit_.load(std::memory_order_relaxed)) {
                    return;
                }
                if (max_tasks_ > 0 && static_cast<int>(tasks_.size()) >= max_tasks_) {
                    removed_task = std::move(tasks_.front());
                    tasks_.pop_front();
                    removed_callback = on_front_task_callback_;
                }
                tasks_.push_back(std::move(task));
            }
            take_var_.notify_one();
            if (removed_callback && removed_task) {
                removed_callback(std::move(removed_task));
            }
        }

        bool RemoveTask(uint64_t task_id) {
            std::lock_guard lock(task_mtx_);
            for (auto iterator = tasks_.begin(); iterator != tasks_.end(); ++iterator) {
                if ((*iterator)->task_id_ == task_id) {
                    tasks_.erase(iterator);
                    return true;
                }
            }
            return false;
        }

        bool TaskExists(uint64_t task_id) {
            std::lock_guard lock(task_mtx_);
            for (const auto& task : tasks_) {
                if (task->task_id_ == task_id) {
                    return true;
                }
            }
            return false;
        }

        std::list<ThreadTaskPtr> GetTasks() {
            std::lock_guard lock(task_mtx_);
            return tasks_;
        }

        int TaskSize() {
            std::lock_guard lock(task_mtx_);
            return static_cast<int>(tasks_.size());
        }

        void Clear() {
            std::lock_guard lock(task_mtx_);
            tasks_.clear();
        }

        void Exit() {
            exit_.store(true, std::memory_order_release);
            take_var_.notify_all();
            if (!Join()) {
                LOGW("Thread {} requested Exit from itself; lifetime-safe reaping is deferred.",
                     name_);
            }
            exit_loop_.store(true, std::memory_order_release);
        }

        bool Join() {
            std::shared_ptr<std::thread> thread;
            {
                std::lock_guard lock(thread_mtx_);
                if (!thread_ || !thread_->joinable()) {
                    return true;
                }
                if (thread_->get_id() == std::this_thread::get_id()) {
                    return false;
                }
                thread = std::move(thread_);
            }
            thread->join();
            return true;
        }

        bool IsJoinable() const {
            std::lock_guard lock(thread_mtx_);
            return thread_ && thread_->joinable();
        }

        uint32_t GeneralId() const {
            std::lock_guard lock(thread_mtx_);
            return thread_id_;
        }

        void SetOnFrontTaskCallback(std::function<void(ThreadTaskPtr)> callback) {
            std::lock_guard lock(task_mtx_);
            on_front_task_callback_ = std::move(callback);
        }

        bool IsExit() const {
            return exit_loop_.load(std::memory_order_acquire);
        }

        bool IsLastTaskReturned() const {
            return last_task_returned_.load(std::memory_order_acquire);
        }

        unsigned long ExecCount() const {
            return task_exec_count_.load(std::memory_order_relaxed);
        }

        int MaxTaskSize() const {
            return max_tasks_;
        }

        uint32_t Tid() const {
            return tid_.load(std::memory_order_acquire);
        }

        std::string Name() const {
            return name_;
        }

    private:
        void TaskLoop() {
            for (;;) {
                ThreadTaskPtr task;
                {
                    std::unique_lock lock(task_mtx_);
                    auto self = shared_from_this();
                    take_var_.wait(lock, [self]() {
                        return !self->tasks_.empty() ||
                               self->exit_.load(std::memory_order_acquire);
                    });
                    if (exit_.load(std::memory_order_acquire)) {
                        LOGI("Ok, thread exit: {}", name_);
                        last_task_returned_.store(true, std::memory_order_release);
                        exit_loop_.store(true, std::memory_order_release);
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                }

                if (task) {
                    task->state_ = ThreadTaskState::kRunning;
                    try {
                        task->Run();
                    }
                    catch (const std::exception& error) {
                        LOGE("Uncaught exception in thread task '{}', task_id={}: {}",
                             name_, task->task_id_, error.what());
                    }
                    catch (...) {
                        LOGE("Uncaught non-standard exception in thread task '{}', task_id={}",
                             name_, task->task_id_);
                    }
                    task->state_ = ThreadTaskState::kReady;
                    task_exec_count_.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        void SetNativeThreadId() {
#ifdef WIN32
            tid_.store(GetCurrentThreadId(), std::memory_order_release);
#endif
        }

        uint32_t GeneralIdLocked() const {
            if (!thread_) {
                return 0;
            }
            return static_cast<uint32_t>(std::hash<std::thread::id>{}(thread_->get_id()));
        }

        void ReapThread() {
            std::shared_ptr<std::thread> thread;
            {
                std::lock_guard lock(thread_mtx_);
                thread = std::move(thread_);
            }
            if (!thread || !thread->joinable()) {
                return;
            }
            if (thread->get_id() == std::this_thread::get_id()) {
                std::thread([thread = std::move(thread)]() { thread->join(); }).detach();
            }
            else {
                thread->join();
            }
        }

        mutable std::mutex init_mtx_;
        bool init_ = false;
        mutable std::mutex task_mtx_;
        std::list<ThreadTaskPtr> tasks_;
        std::condition_variable take_var_;
        mutable std::mutex thread_mtx_;
        std::shared_ptr<std::thread> thread_;
        OnceTask once_task_;
        std::atomic_bool exit_{false};
        std::atomic_bool exit_loop_{false};
        int max_tasks_ = -1;
        std::atomic_bool last_task_returned_{false};
        std::atomic_ulong task_exec_count_{0};
        std::string name_;
        std::atomic_uint32_t tid_{0};
        std::function<void(ThreadTaskPtr)> on_front_task_callback_;
        uint32_t thread_id_ = 0;
    };

    ThreadPtr Thread::Make(const std::string& name, int max_task) {
        struct MakeSharedEnabler final : Thread {
            MakeSharedEnabler(const std::string& thread_name, int maximum_tasks)
                : Thread(thread_name, maximum_tasks) {
            }
        };
        return std::make_shared<MakeSharedEnabler>(name, max_task);
    }

    std::shared_ptr<Thread> Thread::MakeOnceTask(
        OnceTask&& task, const std::string& name, bool join) {
        struct MakeSharedEnabler final : Thread {
            MakeSharedEnabler(OnceTask&& once_task, const std::string& thread_name, bool should_join)
                : Thread(std::move(once_task), thread_name, should_join) {
            }
        };
        auto thread = std::make_shared<MakeSharedEnabler>(std::move(task), name, join);
        thread->StartOnceTask(join);
        return thread;
    }

    Thread::Thread(const std::string& name, int max_task)
        : state_(std::make_shared<State>(name, max_task)) {
    }

    Thread::Thread(OnceTask&& task, const std::string& name, bool)
        : state_(std::make_shared<State>(std::move(task), name)) {
    }

    Thread::~Thread() {
        const auto thread_id = state_ ? state_->GeneralId() : 0;
        Exit();
        MemoryStat::Instance()->RemoveThread(thread_id);
    }

    void Thread::StartOnceTask(bool join) {
        state_->StartOnce(join);
    }

    void Thread::Poll() {
        state_->StartLoop();
        MemoryStat::Instance()->AddThread(state_->GeneralId(), shared_from_this());
    }

    void Thread::Post(const ThreadTaskPtr& task) {
        state_->Post(task);
    }

    void Thread::Post(ThreadTaskPtr&& task) {
        state_->Post(std::move(task));
    }

    void Thread::Post(std::function<void()>&& task) {
        Post(SimpleThreadTask::Make(std::move(task)));
    }

    bool Thread::RemoveTask(uint64_t task_id) {
        return state_->RemoveTask(task_id);
    }

    bool Thread::TaskExists(uint64_t task_id) {
        return state_->TaskExists(task_id);
    }

    bool Thread::HasTask() {
        return TaskSize() > 0;
    }

    int Thread::TaskSize() {
        return state_->TaskSize();
    }

    int Thread::MaxTaskSize() {
        return state_->MaxTaskSize();
    }

    std::list<ThreadTaskPtr> Thread::GetTasks() {
        return state_->GetTasks();
    }

    void Thread::Exit() {
        if (state_) {
            state_->Exit();
        }
    }

    bool Thread::IsExit() {
        return !state_ || state_->IsExit();
    }

    bool Thread::IsLastTaskReturned() {
        return !state_ || state_->IsLastTaskReturned();
    }

    bool Thread::IsJoinable() {
        return state_ && state_->IsJoinable();
    }

    void Thread::Join() {
        if (state_) {
            state_->Join();
        }
    }

    void Thread::Clear() {
        if (state_) {
            state_->Clear();
        }
    }

    unsigned long Thread::ExecCount() {
        return state_ ? state_->ExecCount() : 0;
    }

    uint32_t Thread::GetGeneralId() {
        return state_ ? state_->GeneralId() : 0;
    }

    uint32_t Thread::GetTid() {
        return state_ ? state_->Tid() : 0;
    }

    std::string Thread::GetThreadName() {
        return state_ ? state_->Name() : std::string{};
    }

    void Thread::SetOnFrontTaskCallback(
        std::function<void(ThreadTaskPtr task_ptr)> callback) {
        state_->SetOnFrontTaskCallback(std::move(callback));
    }
}
