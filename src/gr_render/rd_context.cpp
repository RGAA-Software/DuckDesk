//
// Created by RGAA on 2023/12/18.
//

#include "rd_context.h"
#include <thread>
#include <chrono>
#include "tc_common_new/task_runtime.h"
#include "tc_common_new/message_notifier.h"
#include "tc_common_new/log.h"
#include "tc_common_new/win32/dynamic_library.h"
#include "tc_common_new/win32/win_helper.h"
#include "tc_common_new/string_util.h"
#include "gr_render/plugins/plugin_manager.h"
#include "gr_render/plugin_interface/gr_plugin_interface.h"

typedef uint64_t (*FnGenNextGlobalId)();
FnGenNextGlobalId g_fn_gen_next_global_id = nullptr;

namespace tc
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
        auto exe_dir = WinHelper::GetExeFolderPath();
        auto id_generator_path = StringUtil::ToWString(exe_dir) + L"/tc_global_id_generator.dll";
        static std::shared_ptr<DynamicLibrary> s_id_generator_library;
        s_id_generator_library = std::make_shared<DynamicLibrary>(id_generator_path);
        if (!s_id_generator_library->IsLoaded()) {
            if (!s_id_generator_library->Load()) {
                LOGE("Load global id generator failed: {}, error: {}",
                     exe_dir,
                     s_id_generator_library->GetErrorString());
                return false;
            }
        }
        g_fn_gen_next_global_id = (FnGenNextGlobalId)s_id_generator_library->GetSymbol("GenNextGlobalId");
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
        plugin_manager_->VisitAllPlugins([=, this](GrPluginInterface* plugin) {
            plugin->DispatchAppEvent(event);
        });
    }

    uint64_t RdContext::GenNextNetworkIndex() {
        return g_fn_gen_next_global_id();
    }

}
