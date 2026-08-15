//
// Created by RGAA on 2023/12/18.
//

#include "task_runtime.h"
#include "log.h"

namespace tc
{

    TaskRuntime::TaskRuntime(int num_threads) {
        SnowflakeId::initialize(0, 102);
        if (num_threads == -1) {
            num_threads = (int)std::thread::hardware_concurrency();
        }
        this->num_threads_ = num_threads;

        for (int idx = 0; idx < num_threads; idx++) {
            auto t = Thread::Make(std::format("runtime_{}", idx), MAX_TASK_PER_THREAD);
            t->Poll();
            threads_.insert({idx, t});
        }
        LOGI("TaskRuntime threads: {}", num_threads);
    }

    TaskRuntime::~TaskRuntime() {

    }

    // Return: task id
    uint64_t TaskRuntime::Post(const ThreadTaskPtr& task) {
        task->task_id_ = SnowflakeId::generate().implode();
        auto t = FindMostIdleThread();
        t.second->Post(task);
        LOGI("Post.1 task: {} in thread: {}", task->task_id_, t.first);
        return task->task_id_;
    }

    // Return: task id
    uint64_t TaskRuntime::Post(ThreadTaskPtr&& task) {
        task->task_id_ = SnowflakeId::generate().implode();
        auto t = FindMostIdleThread();
        t.second->Post(task);
        //LOGI("Post.2 task: {} in thread: {}", task->task_id_, t.first);
        return task->task_id_;
    }

    // Remove task
    bool TaskRuntime::RemoveTask(uint64_t task_id) {
        for(const auto& [tid, t] : threads_) {
            t->RemoveTask(task_id);
        }
        return true;
    }

    std::pair<int, std::shared_ptr<Thread>> TaskRuntime::FindMostIdleThread() {
        int task_size = MAX_TASK_PER_THREAD;
        std::pair<int, std::shared_ptr<Thread>> target_pair {-1, nullptr};
        for (const auto& pair : threads_) {
            if (pair.second->TaskSize() < task_size) {
                task_size = pair.second->TaskSize();
                target_pair = pair;
            }
        }
        return target_pair;
    }

    void TaskRuntime::Exit() {
        for (const auto& t : threads_) {
            t.second->Exit();
        }
    }

    std::string TaskRuntime::Dump() {
        std::stringstream ss;
        ss << "TaskRuntime: \n";
        ss << "  - Threads Number: " << num_threads_ << std::endl;
        ss << "  - Threads Tasks: " << std::endl;
        for (const auto& [k, v] : threads_) {
            ss << "    - Thread idx: " << k << ", tasks: " << v->TaskSize() << std::endl;
            auto tasks = v->GetTasks();
            for (auto& t : tasks) {
                ss << "      - Task idx: " << t->task_id_ << ", state: " << t->state_ << std::endl;
            }
        }
        return ss.str();
    }

    std::shared_ptr<Thread> TaskRuntime::GetFirstThread() {
        return threads_.at(0);
    }

    std::shared_ptr<Thread> TaskRuntime::GetLastThread() {
        return threads_.at(threads_.size()-1);
    }

}
