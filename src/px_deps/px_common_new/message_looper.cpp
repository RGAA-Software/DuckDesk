//
// Created by RGAA on 25/02/2025.
//

#include "message_looper.h"

namespace tc
{

    MessageLooper::MessageLooper(int32_t max_tasks) {
        max_tasks_ = max_tasks;
    }

    void MessageLooper::Loop() {
        for (;;) {
            if (exit_) {
                return;
            }
            {
                std::unique_lock<std::mutex> lk(task_mtx_);
                take_var_.wait(lk, [this]() {
                    return !tasks_.empty() || exit_;
                });
            }

            std::shared_ptr<MessageTask> task = nullptr;
            {
                std::lock_guard<std::mutex> guard(task_mtx_);
                if (!tasks_.empty()) {
                    task = tasks_.front();
                }
                if (task) {
                    tasks_.remove(task);
                }
            }
            if (task) {
                task->Run();
            }
        }
    }

    void MessageLooper::Exit() {
        exit_ = true;
        take_var_.notify_all();
    }

    void MessageLooper::Post(std::shared_ptr<MessageTask>&& task) {
        if (exit_) {return;}
        std::lock_guard<std::mutex> guard(task_mtx_);
        if (max_tasks_ > 0 && (int)tasks_.size() >= max_tasks_) {
            tasks_.pop_front();
        }
        tasks_.push_back(task);
        take_var_.notify_all();
    }

    void MessageLooper::Post(std::function<void()>&& task) {
        auto target_task = std::make_shared<SimpleMessageTask>(std::move(task));
        this->Post(target_task);
    }

}