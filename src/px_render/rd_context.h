//
// Created by RGAA on 2023/12/18.
//

#ifndef TC_APPLICATION_CONTEXT_H
#define TC_APPLICATION_CONTEXT_H

#include <memory>
#include <atomic>
#include <queue>
#include <mutex>

#include "dexode/EventBus.hpp"
#include "px_common_new/message_notifier.h"
#include "px_common_new/thread.h"
#include "px_common_new/task_runtime.h"

namespace px
{

    class PluginManager;
    class TaskRuntime;
    class AppBaseEvent;

    class RdContext : public std::enable_shared_from_this<RdContext> {
    public:
        static std::shared_ptr<RdContext> Make();

        RdContext();
        ~RdContext();

        bool Init();
        void SetPluginManager(const std::shared_ptr<PluginManager>& pm);

        std::shared_ptr<MessageNotifier> GetMessageNotifier();
        std::shared_ptr<MessageListener> CreateMessageListener();
        std::shared_ptr<PluginManager> GetPluginManager();

        template<typename T>
        void SendAppMessage(const T& m) {
            task_rt_->Post(SimpleThreadTask::Make([=, this]() {
                if (msg_notifier_) {
                    msg_notifier_->SendAppMessage(m);
                }
            }));
        }

        void PostTask(std::function<void()>&& task);
        void PostUITask(std::function<void()>&& task);
        void ExecutePendingUITasks();
        void PostDelayTask(std::function<void()>&& task, int delay);
        void PostStreamPluginTask(std::function<void()>&& task);
        static std::string GetCurrentExeFolder();
        // dispatch app event
        void DispatchAppEvent2Plugins(const std::shared_ptr<AppBaseEvent>& event);

    private:
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<Thread> msg_thread_ = nullptr;
        std::shared_ptr<TaskRuntime> task_rt_ = nullptr;
        std::shared_ptr<PluginManager> plugin_manager_ = nullptr;
        std::shared_ptr<Thread> stream_plugin_thread_ = nullptr;
        std::atomic_bool exiting_ = false;

        std::mutex ui_task_mutex_;
        std::queue<std::function<void()>> ui_tasks_;
    };
}

#endif //TC_APPLICATION_CONTEXT_H
