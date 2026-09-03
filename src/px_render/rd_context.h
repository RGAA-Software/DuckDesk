//
// Created by RGAA on 2023/12/18.
//

#ifndef TC_APPLICATION_CONTEXT_H
#define TC_APPLICATION_CONTEXT_H

#include <memory>
#include <atomic>
#include <queue>
#include <mutex>

#include "px_common_new/message_notifier.h"
#include "px_common_new/thread.h"
#include "px_common_new/task_runtime.h"

namespace asio2 {
    class timer;
}

namespace px
{
    namespace render {
        class EncodedMediaBus;
        class FrameCarrierProcessor;
        class FrameResizerProcessor;
        class FrameDebuggerObserver;
        class FileTransferService;
        class InputReplayService;
        class JoystickService;
        class MediaRecorderSink;
        class NetworkTransportHub;
        class VoiceCallService;
        class RenderCompositionRoot;
    }


    class RenderModuleRegistry;
    class TaskRuntime;
    class AppBaseEvent;
    class PxAsyncRuntime;

    class RdContext : public std::enable_shared_from_this<RdContext> {
    public:
        static std::shared_ptr<RdContext> Make();

        RdContext();
        ~RdContext();

        bool Init();
        void SetRenderModuleRegistry(const std::shared_ptr<RenderModuleRegistry>& pm);

        std::shared_ptr<MessageNotifier> GetMessageNotifier();
        std::shared_ptr<PxAsyncRuntime> GetAsyncRuntime() const;
        std::shared_ptr<MessageListener> CreateMessageListener(
            MessageExecutionLane lane = MessageExecutionLane::kControl);
        std::shared_ptr<RenderModuleRegistry> GetRenderModuleRegistry();
        void SetRenderCompositionRoot(
            const std::shared_ptr<render::RenderCompositionRoot>& root);
        std::shared_ptr<render::RenderCompositionRoot> GetRenderCompositionRoot();
        void SetFrameDebuggerObserver(
            const std::shared_ptr<render::FrameDebuggerObserver>& observer);
        std::shared_ptr<render::FrameDebuggerObserver> GetFrameDebuggerObserver();
        void SetEncodedMediaBus(
            const std::shared_ptr<render::EncodedMediaBus>& media_bus);
        std::shared_ptr<render::EncodedMediaBus> GetEncodedMediaBus();
        void SetMediaRecorderSink(
            const std::shared_ptr<render::MediaRecorderSink>& recorder);
        std::shared_ptr<render::MediaRecorderSink> GetMediaRecorderSink();
        void SetFrameCarrierProcessor(
            const std::shared_ptr<render::FrameCarrierProcessor>& processor);
        std::shared_ptr<render::FrameCarrierProcessor>
            GetFrameCarrierProcessor();
        void SetFrameResizerProcessor(
            const std::shared_ptr<render::FrameResizerProcessor>& processor);
        std::shared_ptr<render::FrameResizerProcessor>
            GetFrameResizerProcessor();
        void SetInputReplayService(
            const std::shared_ptr<render::InputReplayService>& service);
        std::shared_ptr<render::InputReplayService> GetInputReplayService();
        void SetJoystickService(
            const std::shared_ptr<render::JoystickService>& service);
        std::shared_ptr<render::JoystickService> GetJoystickService();
        void SetFileTransferService(
            const std::shared_ptr<render::FileTransferService>& service);
        std::shared_ptr<render::FileTransferService> GetFileTransferService();
        void SetNetworkTransportHub(
            const std::shared_ptr<render::NetworkTransportHub>& hub);
        std::shared_ptr<render::NetworkTransportHub> GetNetworkTransportHub();
        void SetVoiceCallService(
            const std::shared_ptr<render::VoiceCallService>& service);
        std::shared_ptr<render::VoiceCallService> GetVoiceCallService();

        template<typename T>
        void SendAppMessage(const T& m) {
            if (msg_notifier_) {
                (void)msg_notifier_->PublishAppMessage(m);
            }
        }

        void PostTask(std::function<void()>&& task);
        void PostUITask(std::function<void()>&& task);
        void ExecutePendingUITasks();
        void PostDelayTask(std::function<void()>&& task, int delay);
        void PostMediaTask(std::function<void()>&& task);
        static std::string GetCurrentExeFolder();
        // dispatch app event
        void DispatchAppEventToModules(const std::shared_ptr<AppBaseEvent>& event);

    private:
        std::shared_ptr<PxAsyncRuntime> async_runtime_ = nullptr;
        std::shared_ptr<MessageNotifier> msg_notifier_ = nullptr;
        std::shared_ptr<TaskRuntime> task_rt_ = nullptr;
        std::weak_ptr<RenderModuleRegistry> module_registry_;
        std::weak_ptr<render::RenderCompositionRoot> composition_root_;
        std::weak_ptr<render::FrameDebuggerObserver> frame_debugger_observer_;
        std::weak_ptr<render::EncodedMediaBus> encoded_media_bus_;
        std::weak_ptr<render::MediaRecorderSink> media_recorder_sink_;
        std::weak_ptr<render::FrameCarrierProcessor> frame_carrier_processor_;
        std::weak_ptr<render::FrameResizerProcessor> frame_resizer_processor_;
        std::weak_ptr<render::InputReplayService> input_replay_service_;
        std::weak_ptr<render::JoystickService> joystick_service_;
        std::weak_ptr<render::FileTransferService> file_transfer_service_;
        std::weak_ptr<render::NetworkTransportHub> network_transport_hub_;
        std::weak_ptr<render::VoiceCallService> voice_call_service_;
        std::shared_ptr<Thread> media_dispatch_thread_ = nullptr;
        std::shared_ptr<asio2::timer> delay_timer_ = nullptr;
        std::atomic_uint64_t delay_task_id_ = 0;
        std::atomic_bool exiting_ = false;

        std::mutex ui_task_mutex_;
        std::queue<std::function<void()>> ui_tasks_;
    };
}

#endif //TC_APPLICATION_CONTEXT_H
