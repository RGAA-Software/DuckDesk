//
// Created by RGAA on 2023/12/18.
//

#include "rd_context.h"
#include <format>
#include "px_common_new/task_runtime.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/async_runtime.h"
#include "px_common_new/log.h"
#include "px_common_new/win32/dynamic_library.h"
#include "px_common_new/win32/win_helper.h"
#include "px_common_new/string_util.h"
#include "px_render/plugins/plugin_manager.h"
#include "px_render/plugin_interface/px_plugin_interface.h"
#include "asio2/asio2.hpp"

namespace px
{
    std::shared_ptr<RdContext> RdContext::Make() {
        return std::make_shared<RdContext>();
    }

    RdContext::RdContext() {
        async_runtime_ = PxAsyncRuntime::Create({.worker_threads = 2});
        async_runtime_->Start();
        msg_notifier_ = std::make_shared<MessageNotifier>(MessageNotifierOptions{
            .worker_threads = 2,
            .runtime = async_runtime_,
        });
        task_rt_ = std::make_shared<TaskRuntime>(8);

        stream_plugin_thread_ = Thread::Make("stream plugin thread", 128);
        stream_plugin_thread_->Poll();
        delay_timer_ = std::make_shared<asio2::timer>();
    }

    RdContext::~RdContext() {
        if (exiting_.exchange(true)) {
            return;
        }
        {
            std::lock_guard lock(ui_task_mutex_);
            std::queue<std::function<void()>> empty;
            ui_tasks_.swap(empty);
        }
        if (msg_notifier_) {
            msg_notifier_->Stop(MessageBusStopMode::kCancel);
        }
        if (delay_timer_) {
            delay_timer_->stop_all_timers();
            delay_timer_.reset();
        }
        if (task_rt_) {
            task_rt_->Exit();
        }
        if (stream_plugin_thread_) {
            stream_plugin_thread_->Exit();
        }
        if (async_runtime_) {
            async_runtime_->RequestDrain();
            async_runtime_->RequestStop();
            async_runtime_->Join();
            async_runtime_.reset();
        }
    }

    bool RdContext::Init() {
        return true;
    }

    std::shared_ptr<MessageNotifier> RdContext::GetMessageNotifier() {
        return msg_notifier_;
    }

    std::shared_ptr<PxAsyncRuntime> RdContext::GetAsyncRuntime() const {
        return async_runtime_;
    }

    std::shared_ptr<MessageListener> RdContext::CreateMessageListener(MessageExecutionLane lane) {
        return msg_notifier_->CreateListener(lane);
    }

    void RdContext::SetPluginManager(const std::shared_ptr<PluginManager>& pm) {
        plugin_manager_ = pm;
    }

    std::shared_ptr<PluginManager> RdContext::GetPluginManager() {
        return plugin_manager_.lock();
    }

    void RdContext::PostTask(std::function<void()>&& task) {
        if (exiting_ || !task_rt_ || !task) {
            return;
        }
        task_rt_->Post(SimpleThreadTask::Make(std::move(task)));
    }

    void RdContext::PostUITask(std::function<void()>&& task) {
        if (exiting_ || !task) {
            return;
        }
        std::lock_guard<std::mutex> lock(ui_task_mutex_);
        if (exiting_) {
            return;
        }
        ui_tasks_.push(std::move(task));
    }

    void RdContext::ExecutePendingUITasks() {
        if (exiting_) {
            return;
        }
        std::queue<std::function<void()>> local;
        {
            std::lock_guard<std::mutex> lock(ui_task_mutex_);
            local.swap(ui_tasks_);
        }
        while (!local.empty()) {
            if (exiting_) {
                return;
            }
            if (local.front()) {
                local.front()();
            }
            local.pop();
        }
    }

    void RdContext::PostDelayTask(std::function<void()>&& task, int delay) {
        if (exiting_ || !delay_timer_ || !task) {
            return;
        }
        const auto id = std::format("rd_delay_{}", ++delay_task_id_);
        const auto weak_self = weak_from_this();
        delay_timer_->start_timer(id, delay, 1,
            [weak_self, id, t = std::move(task)]() mutable {
                const auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                if (self->delay_timer_) {
                    self->delay_timer_->stop_timer(id);
                }
                self->PostUITask(std::move(t));
            });
    }

    void RdContext::PostStreamPluginTask(std::function<void()>&& task) {
        if (!exiting_ && stream_plugin_thread_ && task) {
            stream_plugin_thread_->Post(std::move(task));
        }
    }

    std::string RdContext::GetCurrentExeFolder() {
        return WinHelper::GetExeFolderPath();
    }

    void RdContext::DispatchAppEvent2Plugins(const std::shared_ptr<AppBaseEvent>& event) {
        const auto plugin_manager = plugin_manager_.lock();
        if (exiting_ || !plugin_manager || !event) {
            return;
        }
        plugin_manager->VisitAllPlugins([event](PxPluginInterface* plugin) { // NOLINT(gammaray-raw-pointer-boundary): plug-in visitor ABI
            plugin->DispatchAppEvent(event);
        });
    }

}
