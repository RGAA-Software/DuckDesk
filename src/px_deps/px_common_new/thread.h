#ifndef COMM_THREAD_H
#define COMM_THREAD_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <list>
#include <sstream>
#include <any>
#include <atomic>

namespace px
{

    typedef std::function<void()> VoidFunc;
    typedef std::function<std::any()> ExecFunc;
    typedef std::function<void(std::any)> CallbackFunc;

    enum ThreadTaskState {
        kIdle,
        kRunning,
        kReady,
    };

    class ThreadTask {
    public:
        ThreadTask() = default;
        virtual ~ThreadTask() = default;
        virtual void Run() = 0;
    public:
        uint64_t task_id_ = 0;
        ThreadTaskState state_ = ThreadTaskState::kIdle;
    };

    class SimpleThreadTask : public ThreadTask {
    public:

        static std::shared_ptr<SimpleThreadTask> Make(VoidFunc&& ef) {
            return std::make_shared<SimpleThreadTask>(std::move(ef), []() {});
        }

        static std::shared_ptr<SimpleThreadTask> Make(VoidFunc&& ef, VoidFunc&& cbk) {
            return std::make_shared<SimpleThreadTask>(std::move(ef), std::move(cbk));
        }

        SimpleThreadTask(VoidFunc&& ef, VoidFunc&& cbk) {
            exec_func_ = ef;
            callback_ = cbk;
        }

        void Run() override {
            if (exec_func_) {
                exec_func_();
            }
            if (callback_) {
                callback_();
            }
        }
    protected:
        VoidFunc exec_func_;
        VoidFunc callback_;
    };



    template <class ExecFunc, class CallbackFunc>
    class ReturnThreadTask : public ThreadTask {
    public:

        static std::shared_ptr<ReturnThreadTask> Make(ExecFunc ef, CallbackFunc cbk) {
            return std::make_shared<ReturnThreadTask>(ef, cbk);
        }

        ReturnThreadTask(ExecFunc ef, CallbackFunc cbk) {
            exec_func_ = ef;
            callback_ = cbk;
        }

        void Run() override {
            if (exec_func_) {
                auto ret = exec_func_();
                if (callback_) {
                    callback_(ret);
                }
            }
        }

    private:
        ExecFunc exec_func_;
        CallbackFunc callback_;

    };


    typedef std::shared_ptr<ThreadTask> ThreadTaskPtr;
    typedef std::function<void()>       OnceTask;

    class Thread : public std::enable_shared_from_this<Thread>
    {
    public:
        static std::shared_ptr<Thread> Make(const std::string& name, int max_task);
        static std::shared_ptr<Thread> MakeOnceTask(OnceTask&& task, const std::string& name, bool join = false);

        Thread() = delete;
        virtual ~Thread();

        void Poll();

        void Post(const ThreadTaskPtr& task);
        void Post(ThreadTaskPtr&& task);
        void Post(std::function<void()>&& task);
        bool RemoveTask(uint64_t task_id);
        bool TaskExists(uint64_t task_id);
        bool HasTask();
        int TaskSize();
        int MaxTaskSize();
        std::list<ThreadTaskPtr> GetTasks();
        void Exit();

        bool IsExit();
        bool IsLastTaskReturned();
        bool IsJoinable();
        void Join();
        void Clear();

        unsigned long ExecCount();

        // Not PID
        uint32_t GetGeneralId();
        // Thread id
        uint32_t GetTid();
        // Thread name
        std::string GetThreadName();

        void SetOnFrontTaskCallback(std::function<void(ThreadTaskPtr task_ptr)> callback) {
            on_front_task_callback_ = callback;
        }
    protected:
        explicit Thread(const std::string& name, int max_task = -1);
        Thread(OnceTask&& task, const std::string& name, bool join = true);
    private:
        void TaskLoop();

    private:

        std::mutex init_mtx_;
        bool init_{false};

        std::mutex task_mtx_;
        std::list<ThreadTaskPtr> tasks_;

        std::shared_ptr<std::thread>  thread_;

        std::condition_variable take_var_;

        std::atomic_bool exit_{false};
        std::atomic_bool exit_loop_{false};

        int max_tasks_ = -1;
        std::atomic_bool last_task_returned_{false};
        std::atomic_ulong task_exec_count_{0};

        std::string name_;
        uint32_t tid_ = 0;

        std::function<void(ThreadTaskPtr task_ptr)> on_front_task_callback_;

        uint32_t thread_id_ = 0;
    };


    typedef std::shared_ptr<Thread>  ThreadPtr;


}

#endif // THREAD_H
