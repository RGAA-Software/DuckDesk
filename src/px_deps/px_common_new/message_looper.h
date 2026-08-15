//
// Created by RGAA on 25/02/2025.
//

#ifndef GAMMARAYSERVER_MESSAGE_LOOPER_H
#define GAMMARAYSERVER_MESSAGE_LOOPER_H

#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <list>

namespace tc
{

    // MessageTask
    class MessageTask {
    public:
        virtual ~MessageTask() = default;
        virtual void Run() = 0;
    public:
        uint64_t task_id_ = 0;
    };

    // SimpleMessageTask
    class SimpleMessageTask : public MessageTask {
    public:
        SimpleMessageTask(std::function<void()>&& task) {
            task_ = task;
        }
        ~SimpleMessageTask() {}

        void Run() override {
            if (task_) {
                task_();
            }
        }

    private:
        std::function<void()> task_;
    };

    // MessageLooper
    class MessageLooper {
    public:
        MessageLooper(int32_t max_tasks);
        void Loop();
        void Exit();
        void Post(std::shared_ptr<MessageTask>&& task);
        void Post(std::function<void()>&& task);

    private:
        std::shared_ptr<std::thread> thread_;
        std::condition_variable take_var_;
        std::atomic_bool exit_ = false;

        std::mutex task_mtx_;
        std::list<std::shared_ptr<MessageTask>> tasks_;

        int32_t max_tasks_ = 0;

    };

}

#endif //GAMMARAYSERVER_MESSAGE_LOOPER_H
