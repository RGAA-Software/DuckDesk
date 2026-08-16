//
// Created by RGAA on 2023/12/18.
//

#include "rd_context.h"
#include <thread>
#include <chrono>
#include "px_common_new/task_runtime.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/log.h"
#include "px_common_new/win32/dynamic_library.h"
#include "px_common_new/win32/win_helper.h"
#include "px_common_new/string_util.h"
#include "px_render/plugins/plugin_manager.h"
#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px
{
    std::shared_ptr<RdContext> RdContext::Make() {
        return std::make_shared<RdContext>();
    }

    RdContext::RdContext() {
        msg_notifier_ = std::make_shared<MessageNotifier>();
        task_rt_ = std::make_shared<TaskRuntime>(8);

        stream_plugin_thread_ = Thread::Make("stream plugin thread", 128);
        stream_plugin_thread_->Poll();
    }

    RdContext::~RdContext() {
        exiting_ = true;
    }

    bool RdContext::Init() {
        return true;
    }

    std::shared_ptr<MessageNotifier> RdContext::GetMessageNotifier() {
        return msg_notifier_;
    }

    std::shared_ptr<MessageListener> RdContext::CreateMessageListener() {
        return msg_notifier_->CreateListener();
    }

    void RdContext::SetPluginManager(const std::shared_ptr<PluginManager>& pm) {
        plugin_manager_ = pm;
    }

    std::shared_ptr<PluginManager> RdContext::GetPluginManager() {
        return plugin_manager_;
    }

    void RdContext::PostTask(std::function<void()>&& task) {
        task_rt_->Post(SimpleThreadTask::Make(std::move(task)));
    }

    void RdContext::PostUITask(std::function<void()>&& task) {
        std::lock_guard<std::mutex> lock(ui_task_mutex_);
        ui_tasks_.push(std::move(task));
    }

    void RdContext::ExecutePendingUITasks() {
        std::queue<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(ui_task_mutex_);
            local.swap(ui_tasks_);
        }
        while (!local.empty()) {
            local.front()();
            local.pop();
        }
    }

    void RdContext::PostDelayTask(std::function<void()>&& task, int delay) {
        auto weak_self = weak_from_this();
        std::thread([weak_self, delay, t = std::move(task)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            self->PostUITask(std::move(t));
        }).detach();
    }

    void RdContext::PostStreamPluginTask(std::function<void()>&& task) {
        stream_plugin_thread_->Post(std::move(task));
    }

    std::string RdContext::GetCurrentExeFolder() {
        return WinHelper::GetExeFolderPath();
    }

    void RdContext::DispatchAppEvent2Plugins(const std::shared_ptr<AppBaseEvent>& event) {
        plugin_manager_->VisitAllPlugins([=, this](PxPluginInterface* plugin) {
            plugin->DispatchAppEvent(event);
        });
    }

}
