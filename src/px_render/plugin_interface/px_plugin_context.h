//
// Created by RGAA on 19/11/2024.
//

#ifndef PX_RENDER_PLUGIN_CONTEXT_H
#define PX_RENDER_PLUGIN_CONTEXT_H

#include <functional>
#include <memory>
#include <string>
#include <atomic>

namespace asio2
{
    class timer;
}

namespace px
{

    class Thread;

    class PxPluginContext {
    public:
        explicit PxPluginContext(const std::string& plugin_name);
        ~PxPluginContext() = default;

        void OnDestroy();

        // tasks
        void PostWorkTask(std::function<void()>&& task);
        void PostUITask(std::function<void()>&& task);
        void PostDelayTask(std::function<void()>&& task, int delay);

        // timer
        void StartTimer(int millis, std::function<void()>&& cbk);

    private:
        std::shared_ptr<Thread> work_thread_ = nullptr;
        std::shared_ptr<asio2::timer> timer_ = nullptr;
        std::atomic<int> delay_task_id_ = 0;
    };

}

#endif //PX_RENDER_PLUGIN_CONTEXT_H
