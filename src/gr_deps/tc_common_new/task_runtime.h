//
// Created by RGAA on 2023/12/18.
//

#ifndef TC_APPLICATION_TASKRUNTIME_H
#define TC_APPLICATION_TASKRUNTIME_H

#include <unordered_map>

#include "thread.h"
#include "tc_common_new/snowflake_id.h"

#define MAX_TASK_PER_THREAD 10000000

namespace tc
{
    class TaskRuntime {
    public:

        explicit TaskRuntime(int num_threads = -1);
        ~TaskRuntime();

        // Return: task id
        uint64_t Post(const ThreadTaskPtr& task);
        // Return: task id
        uint64_t Post(ThreadTaskPtr&& task);
        // Remove Task if the target task is in Idle state
        bool RemoveTask(uint64_t task_id);
        // Exit
        void Exit();
        // First Thread
        std::shared_ptr<Thread> GetFirstThread();
        // Last Thread
        std::shared_ptr<Thread> GetLastThread();

        std::string Dump();

    private:

        std::pair<int, std::shared_ptr<Thread>> FindMostIdleThread();

    private:
        int num_threads_ = 0;
        // thread id => thread
        std::unordered_map<int, std::shared_ptr<Thread>> threads_;
    };
}


#endif //TC_APPLICATION_TASKRUNTIME_H
