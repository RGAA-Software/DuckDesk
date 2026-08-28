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
#include "px_common_new/file_transfer_send_result.h"

namespace asio2 {
    class ws_client;
    class timer;
}

namespace px
{

    class RelayWsClient : public std::enable_shared_from_this<RelayWsClient>, public RelayNetClient {
    public:
        explicit RelayWsClient(const std::string& host, int port, const std::string& device_id,
                               const std::string& device_name, const std::string& stream_id,
                               const std::string& appkey, bool force_gdi, const std::string& remote_device_id,
                               const std::string& connection_ticket = {}, const std::string& connection_nonce = {});
        ~RelayWsClient() override;
        void Start() override;
        void Stop() override;
        void PostBinaryMessage(const std::string& msg) override;
        void SyncDeviceId(const std::string& device_id) override;
        int64_t GetQueuingMsgCount() override;
        void SetDeviceNetInfo(const std::vector<px::RelayDeviceNetInfo>& info);
        bool IsAlive() override;
        void PostNetTask(std::function<void ()> &&task) override;
        [[nodiscard]] std::shared_ptr<FileTransferWritableSignal>
        AcquireFileTransferWritableSignal() override;

    private:
        void SendHello();
        void HeartBeat();

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
        bool force_gdi_ = false;
        std::shared_ptr<asio2::ws_client> client_ = nullptr;
        std::atomic<int64_t> queuing_msg_count_ = 0;
        unsigned int post_thread_id_ = 0;
        std::vector<px::RelayDeviceNetInfo> net_info_;
        std::atomic<int64_t> send_index_ = 0;
        std::mutex send_mtx_;
        std::mutex writable_signal_mutex_;
        std::shared_ptr<FileTransferWritableSignal> writable_signal_;

        void NotifyFileTransferWritable();
        void NotifyFileTransferClosed();
    };

}

#endif //PX_RELAY_WS_CLIENT_H
