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
#include "px_render/modules/render_module_registry.h"
#include "px_render/plugin_interface/px_plugin_interface.h"
#include "architecture/pipeline/encoded_media_bus.h"
#include "architecture/processors/frame_carrier_processor.h"
#include "architecture/processors/frame_resizer_processor.h"
#include "architecture/sinks/media_recorder_sink.h"
#include "architecture/services/input_replay_service.h"
#include "architecture/services/joystick_service.h"
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

        media_dispatch_thread_ = Thread::Make("media dispatch thread", 128);
        media_dispatch_thread_->Poll();
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
        if (media_dispatch_thread_) {
            media_dispatch_thread_->Exit();
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

    void RdContext::SetRenderModuleRegistry(const std::shared_ptr<RenderModuleRegistry>& pm) {
        module_registry_ = pm;
    }

    std::shared_ptr<RenderModuleRegistry> RdContext::GetRenderModuleRegistry() {
        return module_registry_.lock();
    }

    void RdContext::SetRenderCompositionRoot(
        const std::shared_ptr<render::RenderCompositionRoot>& root) {
        composition_root_ = root;
    }

    std::shared_ptr<render::RenderCompositionRoot>
    RdContext::GetRenderCompositionRoot() {
        return composition_root_.lock();
    }

    void RdContext::SetFrameDebuggerObserver(
        const std::shared_ptr<render::FrameDebuggerObserver>& observer) {
        frame_debugger_observer_ = observer;
    }

    std::shared_ptr<render::FrameDebuggerObserver>
    RdContext::GetFrameDebuggerObserver() {
        return frame_debugger_observer_.lock();
    }

    void RdContext::SetEncodedMediaBus(
        const std::shared_ptr<render::EncodedMediaBus>& media_bus) {
        encoded_media_bus_ = media_bus;
    }

    std::shared_ptr<render::EncodedMediaBus> RdContext::GetEncodedMediaBus() {
        return encoded_media_bus_.lock();
    }

    void RdContext::SetMediaRecorderSink(
        const std::shared_ptr<render::MediaRecorderSink>& recorder) {
        media_recorder_sink_ = recorder;
    }

    std::shared_ptr<render::MediaRecorderSink>
    RdContext::GetMediaRecorderSink() {
        return media_recorder_sink_.lock();
    }

    void RdContext::SetFrameCarrierProcessor(
        const std::shared_ptr<render::FrameCarrierProcessor>& processor) {
        frame_carrier_processor_ = processor;
    }

    std::shared_ptr<render::FrameCarrierProcessor>
    RdContext::GetFrameCarrierProcessor() {
        return frame_carrier_processor_.lock();
    }

    void RdContext::SetFrameResizerProcessor(
        const std::shared_ptr<render::FrameResizerProcessor>& processor) {
        frame_resizer_processor_ = processor;
    }

    std::shared_ptr<render::FrameResizerProcessor>
    RdContext::GetFrameResizerProcessor() {
        return frame_resizer_processor_.lock();
    }

    void RdContext::SetInputReplayService(
        const std::shared_ptr<render::InputReplayService>& service) {
        input_replay_service_ = service;
    }

    std::shared_ptr<render::InputReplayService>
    RdContext::GetInputReplayService() {
        return input_replay_service_.lock();
    }

    void RdContext::SetJoystickService(
        const std::shared_ptr<render::JoystickService>& service) {
        joystick_service_ = service;
    }

    std::shared_ptr<render::JoystickService>
    RdContext::GetJoystickService() {
        return joystick_service_.lock();
    }

    void RdContext::SetFileTransferService(
        const std::shared_ptr<render::FileTransferService>& service) {
        file_transfer_service_ = service;
    }

    std::shared_ptr<render::FileTransferService>
    RdContext::GetFileTransferService() {
        return file_transfer_service_.lock();
    }

    void RdContext::SetNetworkTransportHub(
        const std::shared_ptr<render::NetworkTransportHub>& hub) {
        network_transport_hub_ = hub;
    }

    std::shared_ptr<render::NetworkTransportHub>
    RdContext::GetNetworkTransportHub() {
        return network_transport_hub_.lock();
    }

    void RdContext::SetVoiceCallService(
        const std::shared_ptr<render::VoiceCallService>& service) {
        voice_call_service_ = service;
    }

    std::shared_ptr<render::VoiceCallService>
    RdContext::GetVoiceCallService() {
        return voice_call_service_.lock();
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

    void RdContext::PostMediaTask(std::function<void()>&& task) {
        if (!exiting_ && media_dispatch_thread_ && task) {
            media_dispatch_thread_->Post(std::move(task));
        }
    }

    std::string RdContext::GetCurrentExeFolder() {
        return WinHelper::GetExeFolderPath();
    }

    void RdContext::DispatchAppEventToModules(const std::shared_ptr<AppBaseEvent>& event) {
        const auto module_registry = module_registry_.lock();
        if (exiting_ || !module_registry || !event) {
            return;
        }
        module_registry->VisitAllModules([event](const std::shared_ptr<PxPluginInterface>& plugin) {
            plugin->DispatchAppEvent(event);
        });
    }

}
