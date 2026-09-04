//
// Created by RGAA on 2024-04-20.
//

#ifndef PX_WS_PANEL_CLIENT_H
#define PX_WS_PANEL_CLIENT_H

#include <memory>
#include <string>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <asio2/asio2.hpp>

#include "px_common_new/async_runtime.h"

namespace px
{
    namespace render {
        class RenderCompositionRoot;
    }

    class Data;
    class RdContext;
    class RdStatistics;
    class MessageListener;
    class RdSettings;
    class RenderModuleRegistry;
    class PxConnectionAttemptWorkflow;
    class PxReconnectBackoff;
    struct PxConnectionAttemptTicket;
    template<typename T>
    class PxAsyncMailbox;

    class WsPanelClient : public std::enable_shared_from_this<WsPanelClient> {
    public:
        explicit WsPanelClient(const std::shared_ptr<RdContext>& ctx);
        ~WsPanelClient();
        void Start();
        void Exit();
        bool PostNetMessage(std::shared_ptr<Data> msg);
        bool Alive() const;

        void ReportMonitorChanged();

    private:
        void ReportStatistics();
        void SendStatisticsInternal();
        void SendPluginsInfoInternal();
        void ParseNetMessage(const std::string& msg);
        void ProcessCommandEnablePlugin(const std::string& plugin_id);
        void ProcessCommandDisablePlugin(const std::string& plugin_id);
        static PxAwaitable<void> RunIncomingMessageLoop(
            std::weak_ptr<WsPanelClient> weak_client,
            std::shared_ptr<PxAsyncMailbox<std::string>> mailbox);
        static PxAwaitable<void> RunConnectionLoop(
            std::weak_ptr<WsPanelClient> weak_client,
            std::shared_ptr<PxConnectionAttemptWorkflow> workflow,
            std::shared_ptr<PxReconnectBackoff> backoff,
            std::shared_ptr<asio2::ws_client> client,
            std::string host,
            int port,
            std::string path);

    private:
        std::shared_ptr<RdStatistics> statistics_{};
        std::reference_wrapper<RdSettings> settings_;
        std::shared_ptr<RdContext> context_{};
        std::shared_ptr<asio2::ws_client> client_{};
        std::shared_ptr<MessageListener> msg_listener_{};
        std::shared_ptr<MessageListener> state_msg_listener_{};
        std::shared_ptr<PxAsyncScope> async_scope_{};
        std::shared_ptr<PxConnectionAttemptWorkflow> connection_workflow_{};
        std::shared_ptr<PxReconnectBackoff> connection_backoff_{};
        std::shared_ptr<PxAsyncMailbox<std::string>> incoming_messages_{};
        std::shared_ptr<RenderModuleRegistry> module_registry_{};
        std::shared_ptr<render::RenderCompositionRoot> composition_root_{};
        std::atomic_int queuing_message_count_{0};
        std::atomic_uint64_t connection_generation_{0};
        std::atomic_bool started_{false};
        std::atomic_bool exiting_{false};
        // Stable across connection generations and changes when the render process restarts.
        std::string instance_id_{};
    };

}

#endif //PX_WS_PANEL_CLIENT_H
