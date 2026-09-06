//
// Created by RGAA on 28/02/2025.
//

#ifndef PX_RELAY_WS_CLIENT_H
#define PX_RELAY_WS_CLIENT_H

#include <atomic>
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include "relay_callbacks.h"
#include "relay_net_client.h"
#include "relay_device_info.h"
#include "relay_client_sdk_param.h"
#include "px_common/file_transfer_send_result.h"

namespace asio2 {
    class ws_client;
    class timer;
}

namespace px
{
    class PxAsyncRuntime;
    class PxAsyncScope;
    class PxReconnectSupervisor;
    template<typename Client>
    class PxReconnectAdapterSlot;

    class RelayWsClient : public std::enable_shared_from_this<RelayWsClient>, public RelayNetClient {
    public:
        explicit RelayWsClient(const std::string& host, int port, const std::string& device_id,
                               const std::string& device_name, const std::string& stream_id,
                               const std::string& appkey, bool force_gdi, const std::string& remote_device_id,
                               const std::string& connection_ticket = {}, const std::string& connection_nonce = {},
                               const std::string& connection_ticket_device_id = {},
                               const std::string& connection_instance_id = {},
                               RelayTicketScope ticket_scope = RelayTicketScope::kLegacy,
                               std::shared_ptr<PxAsyncRuntime> runtime = {});
        ~RelayWsClient() override;
        void Start() override;
        void Stop() override;
        void PostBinaryMessage(const std::string& msg) override;
        void SyncDeviceId(const std::string& device_id) override;
        int64_t GetQueuingMsgCount() override;
        void SetDeviceNetInfo(const std::vector<px::RelayDeviceNetInfo>& info);
        bool IsAlive() override;
        [[nodiscard]] std::uint64_t ConnectionGeneration() const;
        void PostNetTask(std::function<void ()> &&task) override;
        [[nodiscard]] std::shared_ptr<FileTransferWritableSignal>
        AcquireFileTransferWritableSignal() override;

    private:
        void SendHello();
        void HeartBeat();
        void FinishStop();
        void ScheduleDeferredStop();

    private:
        std::string host_;
        int port_{0};
        std::string device_id_;
        std::string device_name_;
        std::string stream_id_;
        std::string appkey_;
        std::string remote_device_id_;
        std::string connection_ticket_;
        std::string connection_nonce_;
        std::string connection_ticket_device_id_;
        std::string connection_instance_id_;
        RelayTicketScope ticket_scope_{RelayTicketScope::kLegacy};
        bool force_gdi_ = false;
        std::shared_ptr<PxReconnectAdapterSlot<asio2::ws_client>> adapter_slot_{};
        std::shared_ptr<PxAsyncRuntime> async_runtime_{};
        std::shared_ptr<PxAsyncScope> connection_scope_{};
        std::shared_ptr<PxReconnectSupervisor> reconnect_supervisor_{};
        std::atomic_bool started_{false};
        std::atomic_bool exiting_{false};
        std::atomic_bool deferred_stop_scheduled_{false};
        std::atomic<int64_t> queuing_msg_count_ = 0;
        unsigned int post_thread_id_ = 0;
        std::vector<px::RelayDeviceNetInfo> net_info_;
        std::atomic<int64_t> send_index_ = 0;
        std::atomic<int64_t> heartbeat_index_{0};
        std::mutex send_mtx_;
        std::mutex writable_signal_mutex_;
        mutable std::mutex stop_mutex_;
        std::mutex operation_mutex_{};
        std::shared_ptr<FileTransferWritableSignal> writable_signal_;

        void NotifyFileTransferWritable();
        void NotifyFileTransferClosed();
    };

}

#endif //PX_RELAY_WS_CLIENT_H
